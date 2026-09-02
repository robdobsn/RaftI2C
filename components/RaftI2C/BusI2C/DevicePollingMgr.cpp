/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Device Polling Manager
//
// Rob Dobson 2024
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// #define DEBUG_POLL_REQUEST
// #define DEBUG_POLL_RESULT
// #define DEBUG_POLL_RESULT_SPECIFIC_ADDRESS 0x6a
// #define DEBUG_POLL_TIMING
// #define DEBUG_DYNAMIC_READ_LEN

#include "DevicePollingMgr.h"
#include "BusI2CAddrAndSlot.h"
#include "RaftUtils.h"
#include "RaftI2CCentralIF.h"
#include "MiniHDLC.h"   // CRC-16/CCITT-FALSE for poll response integrity

// Interval between poll-integrity summary reports (only logged when non-zero)
static const uint64_t CRC_STATS_REPORT_INTERVAL_US = 30000000;   // 30 seconds

#ifdef DEBUG_POLL_TIMING
static const uint32_t POLL_TIMING_REPORT_INTERVAL_US = 5000000; // 5 seconds
#endif

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Constructor
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

DevicePollingMgr::DevicePollingMgr(BusStatusMgr& busStatusMgr, BusMultiplexers& BusMultiplexers, BusReqSyncFn busI2CReqSyncFn,
                                   RaftI2CCentralIF* pI2CCentral) :
    _busStatusMgr(busStatusMgr),
    _busMultiplexers(BusMultiplexers),
    _busReqSyncFn(busI2CReqSyncFn),
    _pI2CCentral(pI2CCentral)
{
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////   
// Setup
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void DevicePollingMgr::setup(const RaftJsonIF& config)
{
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Service from I2C task
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void DevicePollingMgr::taskService(uint64_t timeNowUs)
{
    // See if any devices need polling
    DevicePollingInfo pollInfo;
    if (_busStatusMgr.getPendingIdentPoll(timeNowUs, pollInfo))
    {
        // Get the address and slot
        if (pollInfo.pollReqs.size() == 0)
            return;
        BusElemAddrType address = pollInfo.pollReqs[0].getAddress();
        BusI2CAddrAndSlot addrAndSlot(address);

        // Get the next request index
        uint32_t nextReqIdx = pollInfo.partialPollNextReqIdx;

#ifdef DEBUG_POLL_REQUEST
        LOG_I(MODULE_PREFIX, "taskService pollreq %s (%04x)", addrAndSlot.toString().c_str(), address);
#endif

        // Enable the slot
        auto rslt = _busMultiplexers.enableOneSlot(addrAndSlot.getSlotNum());
        if (rslt != RAFT_OK)
            return;

        // Switch bus frequency if device specifies a custom poll speed
        // and (no slot mask set OR this device's slot is in the mask)
        bool speedChanged = false;
        uint32_t savedBusFreqHz = 0;
        if (pollInfo.pollBusHz != 0 && _pI2CCentral &&
            (pollInfo.pollBusHzSlotMask == 0 || (pollInfo.pollBusHzSlotMask & (1ULL << addrAndSlot.getSlotNum()))))
        {
            savedBusFreqHz = _pI2CCentral->getBusFrequency();
            if (pollInfo.pollBusHz != savedBusFreqHz)
                speedChanged = _pI2CCentral->setBusFrequency(pollInfo.pollBusHz);
        }

#ifdef DEBUG_POLL_TIMING
        // Get timing stats for this address
        PollTimingStats& timing = getTimingStats(address);
        uint64_t txnStartUs = micros();
#endif

        // Prep poll result data
        std::vector<uint8_t> pollDataResult;

        // Track per-operation results for dynamic read length expressions
        std::vector<std::vector<uint8_t>> perOpResults;

        // If this is the start of the poll then setup the timestamp
        if (nextReqIdx == 0)
        {
            // Setup the timestamp
            pollDataResult.resize(DevicePollingInfo::POLL_RESULT_TIMESTAMP_SIZE);
            uint32_t timeNowPollUnits = timeNowUs / DevicePollingInfo::POLL_RESULT_RESOLUTION_US;
            if (DevicePollingInfo::POLL_RESULT_TIMESTAMP_SIZE == 2)
                Raft::setBEUInt16(pollDataResult.data(), 0, timeNowPollUnits & 0xffff);
            else if (DevicePollingInfo::POLL_RESULT_TIMESTAMP_SIZE == 4)
                Raft::setBEUInt32(pollDataResult.data(), 0, timeNowPollUnits);
        }

        // Loop through the requests starting at the next request index
        bool allResultsOkAndComplete = true;
        for (uint32_t i = nextReqIdx; i < pollInfo.pollReqs.size(); i++)
        {
            // Get the request record
            BusRequestInfo& busReqRec = pollInfo.pollReqs[i];

            // If this request has a dynamic read length, evaluate it now
            if (busReqRec.hasDynamicReadLen())
            {
                uint16_t computedLen = busReqRec.getReadReqLen(perOpResults);
                busReqRec.setReadReqLen(computedLen);
#ifdef DEBUG_DYNAMIC_READ_LEN
                LOG_I(MODULE_PREFIX, "dynReadLen addr %04x op %d computedLen %d", address, i, computedLen);
#endif
                // Skip the I2C transaction if computed read length is 0 (e.g. FIFO empty)
                if (computedLen == 0)
                {
                    perOpResults.push_back(std::vector<uint8_t>());
                    continue;
                }
            }

            // Perform the request
            std::vector<uint8_t> readData;
            auto rslt = _busReqSyncFn(&busReqRec, &readData);

#ifdef DEBUG_POLL_RESULT
#ifdef DEBUG_POLL_RESULT_SPECIFIC_ADDRESS
            if (addrAndSlot.getI2CAddr() == DEBUG_POLL_RESULT_SPECIFIC_ADDRESS)
#endif
            {
                String writeDataHexStr;
                Raft::getHexStrFromBytes(busReqRec.getWriteData(), busReqRec.getWriteDataLen(), writeDataHexStr);
                String readDataHexStr;
                Raft::getHexStrFromBytes(readData.data(), readData.size(), readDataHexStr);
                LOG_I(MODULE_PREFIX, "taskService pollrslt addr %s (%04x) writeData %s readData %s rslt %s", 
                                addrAndSlot.toString().c_str(),
                                address,
                                writeDataHexStr.c_str(),
                                readDataHexStr.c_str(),
                                Raft::getRetCodeStr(rslt));
            }
#endif

            if (rslt != RAFT_OK)
            {
                bool isOnline = false;
                _busStatusMgr.updateBusElemState(address, false, isOnline);
                allResultsOkAndComplete = false;
                break;
            }

            // Check the response integrity trailer if this device carries one. A
            // response that cannot be validated is discarded rather than decoded -
            // dropping a sample is always better than reporting a wrong measurement.
            // The device stays online: this is data corruption, not absence.
            if (!validatePollResponse(pollInfo, busReqRec, address, readData))
            {
                allResultsOkAndComplete = false;
                break;
            }

            // Store this operation's result for use by later dynamic read expressions
            perOpResults.push_back(readData);

            // Append the received data to the poll result data
            pollDataResult.insert(pollDataResult.end(), readData.begin(), readData.end());

            // Check if the request contains a pause after send and this is not the last request
            uint32_t pauseAfterSendMs = busReqRec.getBarAccessForMsAfterSend();
            if (pauseAfterSendMs > 0 && i < pollInfo.pollReqs.size() - 1)
            {
                // Update the ident poll info with the pause after send and received data
                _busStatusMgr.handlePollResult(i+1, timeNowUs, address, pollDataResult, &pollInfo, pauseAfterSendMs);
                allResultsOkAndComplete = false;
                break;
            }
        }

        // Store the poll result if all requests succeeded
        if (allResultsOkAndComplete)
        {
            _busStatusMgr.handlePollResult(0, timeNowUs, address, pollDataResult, &pollInfo, 0);
        }

#ifdef DEBUG_POLL_TIMING
        // Record poll timing
        uint64_t txnEndUs = micros();
        uint64_t txnDurationUs = txnEndUs - txnStartUs;
        if (timing.lastPollStartUs != 0)
            timing.cumulIntervalUs += (txnStartUs - timing.lastPollStartUs);
        timing.lastPollStartUs = txnStartUs;
        timing.cumulTransactionUs += txnDurationUs;
        timing.pollCount++;

        // Extract FIFO word count from poll result (bytes 2-3 after 2-byte timestamp)
        if (pollDataResult.size() >= 4 + DevicePollingInfo::POLL_RESULT_TIMESTAMP_SIZE)
        {
            uint32_t statusOffset = DevicePollingInfo::POLL_RESULT_TIMESTAMP_SIZE;
            uint16_t wc = ((pollDataResult[statusOffset + 1] & 0x0F) << 8) | pollDataResult[statusOffset];
            timing.cumulFifoWords += wc;
        }

        // Periodic timing report
        if (timing.pollCount > 0 && Raft::isTimeout(txnEndUs, timing.lastReportTimeUs, POLL_TIMING_REPORT_INTERVAL_US))
        {
            uint32_t avgIntervalUs = timing.pollCount > 1 ? timing.cumulIntervalUs / (timing.pollCount - 1) : 0;
            uint32_t avgTxnUs = timing.cumulTransactionUs / timing.pollCount;
            float fifoSamplesPerSec = timing.cumulIntervalUs > 0 ? 
                (timing.cumulFifoWords / 6.0f) * 1000000.0f / timing.cumulIntervalUs : 0;
            LOG_I(MODULE_PREFIX, "pollTiming addr=0x%03x polls=%d avgInterval=%duS avgTxn=%duS fifoRate=%.1fHz totalFifoWords=%d",
                  address, timing.pollCount, avgIntervalUs, avgTxnUs, fifoSamplesPerSec, timing.cumulFifoWords);
            timing.cumulTransactionUs = 0;
            timing.cumulIntervalUs = 0;
            timing.cumulFifoWords = 0;
            timing.pollCount = 0;
            timing.lastReportTimeUs = txnEndUs;
        }
#endif

        // Restore bus frequency if it was changed
        if (speedChanged && _pI2CCentral)
            _pI2CCentral->setBusFrequency(savedBusFreqHz);

        // Restore the bus multiplexers if necessary
        _busMultiplexers.disableAllSlots(false);
    }

    // Periodically report poll-response integrity. Silent unless something has
    // actually failed a CRC - a healthy bus produces no output at all.
    if (_crcStats.failed || _crcStats.dropped)
    {
        if (Raft::isTimeout(timeNowUs, _crcStatsLastReportUs, CRC_STATS_REPORT_INTERVAL_US))
        {
            _crcStatsLastReportUs = timeNowUs;
            LOG_W(MODULE_PREFIX, "pollIntegrity checked %d failed %d recovered %d dropped %d (%.3f%% of checks failed)",
                        _crcStats.checked, _crcStats.failed, _crcStats.recovered, _crcStats.dropped,
                        _crcStats.checked ? (100.0 * _crcStats.failed / _crcStats.checked) : 0.0);
        }
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Validate a poll response carrying a CRC trailer, re-reading if the device supports it
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool DevicePollingMgr::validatePollResponse(const DevicePollingInfo& pollInfo, BusRequestInfo& busReqRec,
                                            BusElemAddrType address, std::vector<uint8_t>& readData)
{
    // Nothing to do unless this device declares a CRC
    if (pollInfo.pollCrcType != DevicePollingInfo::POLL_CRC_16_CCITT)
        return true;

    // The trailer occupies the last pollCrcTrailerLen bytes; everything before it is
    // the declared length L. The CRC covers bytes 0..L inclusive - the response, the
    // device's zero padding, and the seq byte at offset L.
    const uint32_t trailerLen = pollInfo.pollCrcTrailerLen;
    if ((trailerLen < 3) || (readData.size() < trailerLen + 1))
    {
        _crcStats.dropped++;
        LOG_W(MODULE_PREFIX, "crcCheck addr %04x response too short (%d bytes, trailer %d)",
                    address, (int)readData.size(), (int)trailerLen);
        return false;
    }
    const uint32_t declaredLen = readData.size() - trailerLen;

    _crcStats.checked++;

    // Try the response we have, then up to pollCrcRetries re-reads
    for (uint32_t attempt = 0; ; attempt++)
    {
        const uint16_t expected = (uint16_t)MiniHDLC::computeCRC16(readData.data(), declaredLen + 1);
        const uint16_t actual = ((uint16_t)readData[declaredLen + 1] << 8) | readData[declaredLen + 2];
        if (expected == actual)
        {
            if (attempt > 0)
                _crcStats.recovered++;
            // Back to producing good data - clear the recovery backstop counter
            if (uint32_t* pDrops = getConsecutiveDropCount(address))
                *pDrops = 0;
            // Strip the trailer so the decode sees exactly what it saw before
            readData.resize(declaredLen);
            return true;
        }

        if (attempt == 0)
            _crcStats.failed++;

        if (attempt >= pollInfo.pollCrcRetries)
            break;

        // Ask the device to re-send the response it just sent. This matters when the
        // original read was destructive (a FIFO pop happens at address match, before
        // any byte is clocked) so a plain re-read would return the *next* response.
        BusRequestInfo rereadReq(BUS_REQ_TYPE_POLL,
                address,
                DevicePollingInfo::DEV_IDENT_POLL_CMD_ID,
                pollInfo.pollCrcRereadCmd.size(),
                pollInfo.pollCrcRereadCmd.data(),
                busReqRec.getReadReqLen(),
                0,
                NULL,
                NULL);
        std::vector<uint8_t> rereadData;
        if (_busReqSyncFn(&rereadReq, &rereadData) != RAFT_OK)
            break;
        if (rereadData.size() != readData.size())
            break;
        readData = rereadData;
    }

    _crcStats.dropped++;
#ifdef DEBUG_POLL_CRC
    String hexStr;
    Raft::getHexStrFromBytes(readData.data(), readData.size(), hexStr);
    LOG_W(MODULE_PREFIX, "crcCheck addr %04x FAILED after %d retries data %s",
                address, (int)pollInfo.pollCrcRetries, hexStr.c_str());
#endif

    // Recovery backstop: a device producing nothing valid for a sustained run is not
    // suffering noise, it has stopped working - most often an RSAO that reset into its
    // bootloader, which the master would otherwise never restart because START_APP is
    // only sent during detection. Write the record's recovery command and start again.
    if (pollInfo.pollCrcRecoverAfter > 0)
    {
        uint32_t* pDrops = getConsecutiveDropCount(address);
        if (pDrops)
        {
            (*pDrops)++;
            if (*pDrops >= pollInfo.pollCrcRecoverAfter)
            {
                *pDrops = 0;
                BusRequestInfo recoverReq(BUS_REQ_TYPE_POLL,
                        address,
                        DevicePollingInfo::DEV_IDENT_POLL_CMD_ID,
                        pollInfo.pollCrcRecoverCmd.size(),
                        pollInfo.pollCrcRecoverCmd.data(),
                        0,
                        0,
                        NULL,
                        NULL);
                std::vector<uint8_t> noData;
                _busReqSyncFn(&recoverReq, &noData);
                _crcStats.recoveries++;
                LOG_W(MODULE_PREFIX, "pollIntegrity addr %04x no valid response for %d polls - sent recovery command",
                            address, (int)pollInfo.pollCrcRecoverAfter);
            }
        }
    }
    return false;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Get or claim the consecutive-drop counter for an address (recovery backstop)
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

uint32_t* DevicePollingMgr::getConsecutiveDropCount(BusElemAddrType address)
{
    for (uint32_t i = 0; i < MAX_RECOVERY_ENTRIES; i++)
    {
        if (_recoveryStates[i].inUse && (_recoveryStates[i].address == address))
            return &_recoveryStates[i].consecutiveDrops;
    }
    for (uint32_t i = 0; i < MAX_RECOVERY_ENTRIES; i++)
    {
        if (!_recoveryStates[i].inUse)
        {
            _recoveryStates[i].inUse = true;
            _recoveryStates[i].address = address;
            _recoveryStates[i].consecutiveDrops = 0;
            return &_recoveryStates[i].consecutiveDrops;
        }
    }
    return nullptr;
}

#ifdef DEBUG_POLL_TIMING
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Get or create timing stats entry for an address
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

DevicePollingMgr::PollTimingStats& DevicePollingMgr::getTimingStats(BusElemAddrType address)
{
    for (uint32_t i = 0; i < _timingEntryCount; i++)
    {
        if (_timingAddresses[i] == address)
            return _timingStats[i];
    }
    if (_timingEntryCount < MAX_TIMING_ENTRIES)
    {
        _timingAddresses[_timingEntryCount] = address;
        _timingStats[_timingEntryCount] = PollTimingStats();
        return _timingStats[_timingEntryCount++];
    }
    // Fallback: reuse last slot
    return _timingStats[MAX_TIMING_ENTRIES - 1];
}
#endif
