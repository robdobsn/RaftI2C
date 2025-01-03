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

#ifdef DEBUG_POLL_REQUEST
        LOG_I(MODULE_PREFIX, "taskService poll %s (%04x)", addrAndSlot.toString().c_str(), address);
#endif

        // Enable the slot
        auto rslt = _busMultiplexers.enableOneSlot(addrAndSlot.slotNum);
        if (rslt != RAFT_OK)
            return;

        // Prep poll req data
        pollResultPrepare(timeNowUs, pollInfo);

        // Loop through the requests
        bool allResultsOk = true;
        for (auto& busReqRec : pollInfo.pollReqs)
        {
            // Perform the polling
            std::vector<uint8_t> readData;
            BusRequestInfo reqRec(busReqRec);
            auto rslt = _busReqSyncFn(&reqRec, &readData);

#ifdef DEBUG_POLL_RESULT
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
#endif

            if (rslt != RAFT_OK)
            {
                allResultsOk = false;
                break;
            }

            // Add to data aggregator
            pollResultAdd(pollInfo, readData);
        }

        // Store the poll result if all requests succeeded
        if (allResultsOk)
            _busStatusMgr.handlePollResult(timeNowUs, address, _pollDataResult, &pollInfo);

        // Restore the bus multiplexers if necessary
        _busMultiplexers.disableAllSlots(false);
    }
}
