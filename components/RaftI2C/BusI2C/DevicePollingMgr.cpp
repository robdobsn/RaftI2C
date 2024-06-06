/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Device Polling Manager
//
// Rob Dobson 2024
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "DevicePollingMgr.h"

#define DEBUG_POLL_RESULT

#ifdef DEBUG_POLL_RESULT
static const char* MODULE_PREFIX = "DevicePollingMgr";
#endif

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Constructor
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

DevicePollingMgr::DevicePollingMgr(BusStatusMgr& busStatusMgr, BusExtenderMgr& BusExtenderMgr, BusI2CReqSyncFn busI2CReqSyncFn) :
    _busStatusMgr(busStatusMgr),
    _busExtenderMgr(BusExtenderMgr),
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

        // Check if a bus extender slot can be set (if required)
        auto rslt = _busExtenderMgr.enableOneSlot(addrAndSlot.slotPlus1);
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
            if (rslt != RaftI2CCentralIF::ACCESS_RESULT_OK)
            {
                allResultsOk = false;
                break;
            }

            // Add to data aggregator
            pollResultAdd(pollInfo, readData);

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
        }

        // Store the poll result if all requests succeeded
        if (allResultsOk)
            _busStatusMgr.pollResultStore(timeNowUs, pollInfo, addrAndSlot, _pollDataResult);

        // Restore the bus extender(s) if necessary
        _busExtenderMgr.disableAllSlots(false);
    }
}
