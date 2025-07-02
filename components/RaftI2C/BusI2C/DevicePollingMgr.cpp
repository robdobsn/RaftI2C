/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Device Polling Manager
//
// Rob Dobson 2024
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "DevicePollingMgr.h"
#include "BusI2CAddrAndSlot.h"

// #define DEBUG_POLL_REQUEST
// #define DEBUG_POLL_RESULT
// #define DEBUG_POLL_RESULT_SPECIFIC_ADDRESS 0x15

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
        BusI2CAddrAndSlot addrAndSlot = BusI2CAddrAndSlot::fromBusElemAddrType(address);

        // Get the next request index
        uint32_t nextReqIdx = pollInfo.partialPollNextReqIdx;

#ifdef DEBUG_POLL_REQUEST
        LOG_I(MODULE_PREFIX, "taskService poll %s (%04x)", addrAndSlot.toString().c_str(), address);
#endif

        // Enable the slot
        auto rslt = _busMultiplexers.enableOneSlot(addrAndSlot.slotNum);
        if (rslt != RAFT_OK)
            return;

        // Prep poll result data
        std::vector<uint8_t> pollDataResult;

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

            // Perform the request
            std::vector<uint8_t> readData;
            auto rslt = _busReqSyncFn(&busReqRec, &readData);

#ifdef DEBUG_POLL_RESULT
#ifdef DEBUG_POLL_RESULT_SPECIFIC_ADDRESS
            if (addrAndSlot.i2cAddr == DEBUG_POLL_RESULT_SPECIFIC_ADDRESS)
#endif
            {
                String writeDataHexStr;
                Raft::getHexStrFromBytes(busReqRec.getWriteData(), busReqRec.getWriteDataLen(), writeDataHexStr);
                String readDataHexStr;
                Raft::getHexStrFromBytes(readData.data(), readData.size(), readDataHexStr);
                LOG_I(MODULE_PREFIX, "taskService poll addr %s (%04x) writeData %s readData %s rslt %s", 
                                addrAndSlot.toString().c_str(),
                                address,
                                writeDataHexStr.c_str(),
                                readDataHexStr.c_str(),
                                Raft::getRetCodeStr(rslt));
            }
#endif

            if (rslt != RAFT_OK)
            {
                allResultsOkAndComplete = false;
                break;
            }

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
            _busStatusMgr.handlePollResult(0, timeNowUs, address, pollDataResult, &pollInfo, 0);

        // Restore the bus multiplexers if necessary
        _busMultiplexers.disableAllSlots(false);
    }
}
