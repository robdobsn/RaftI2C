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

#ifdef DEBUG_POLL_TIMING
static const uint32_t POLL_TIMING_REPORT_INTERVAL_US = 5000000; // 5 seconds
#endif

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Constructor
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

DevicePollingMgr::DevicePollingMgr(BusStatusMgr& busStatusMgr, BusMultiplexers& BusMultiplexers, BusReqSyncFn busI2CReqSyncFn) :
    _busStatusMgr(busStatusMgr),
    _busMultiplexers(BusMultiplexers),
    _busReqSyncFn(busI2CReqSyncFn)
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

        // Restore the bus multiplexers if necessary
        _busMultiplexers.disableAllSlots(false);
    }
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
