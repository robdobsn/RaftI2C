/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Device Polling Manager
//
// Rob Dobson 2024
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "DevicePollingMgr.h"

// #define DEBUG_POLL_REQUEST
// #define DEBUG_POLL_RESULT

#ifdef DEBUG_POLL_RESULT
static const char* MODULE_PREFIX = "DevicePollingMgr";
#endif

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Constructor
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

DevicePollingMgr::DevicePollingMgr(BusStatusMgr& busStatusMgr, BusMultiplexers& BusMultiplexers, BusI2CReqSyncFn busI2CReqSyncFn) :
    _busStatusMgr(busStatusMgr),
    _busMultiplexers(BusMultiplexers),
    _busI2CReqSyncFn(busI2CReqSyncFn)
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
        BusI2CAddrAndSlot addrAndSlot = BusI2CAddrAndSlot::fromCompositeAddrAndSlot(pollInfo.pollReqs[0].getAddressUint32());

#ifdef DEBUG_POLL_REQUEST
        LOG_I(MODULE_PREFIX, "taskService poll %s (%04x)", addrAndSlot.toString().c_str(), pollInfo.pollReqs[0].getAddressUint32());
#endif

        // Enable the slot
        auto rslt = _busMultiplexers.enableOneSlot(addrAndSlot.slotNum);
        if (rslt != RaftI2CCentralIF::ACCESS_RESULT_OK)
            return;

        // Prep poll req data
        pollResultPrepare(timeNowUs, pollInfo);

        // Loop through the requests
        bool allResultsOk = true;
        for (auto& busReqRec : pollInfo.pollReqs)
        {
            // Perform the polling
            std::vector<uint8_t> readData;
            BusI2CRequestRec reqRec(busReqRec);
            auto rslt = _busI2CReqSyncFn(&reqRec, &readData);

#ifdef DEBUG_POLL_RESULT
            String writeDataHexStr;
            Raft::getHexStrFromBytes(busReqRec.getWriteData(), busReqRec.getWriteDataLen(), writeDataHexStr);
            String readDataHexStr;
            Raft::getHexStrFromBytes(readData.data(), readData.size(), readDataHexStr);
            LOG_I(MODULE_PREFIX, "taskService poll %s writeData %s readData %s rslt %s", 
                            reqRec.getAddrAndSlot().toString().c_str(),
                            writeDataHexStr.c_str(),
                            readDataHexStr.c_str(),
                            RaftI2CCentralIF::getAccessResultStr(rslt));
#endif

            if (rslt != RaftI2CCentralIF::ACCESS_RESULT_OK)
            {
                allResultsOk = false;
                break;
            }

            // Add to data aggregator
            pollResultAdd(pollInfo, readData);
        }

        // Store the poll result if all requests succeeded
        if (allResultsOk)
            _busStatusMgr.pollResultStore(timeNowUs, pollInfo, addrAndSlot, _pollDataResult);

        // Restore the bus multiplexers if necessary
        _busMultiplexers.disableAllSlots(false);
    }
}
