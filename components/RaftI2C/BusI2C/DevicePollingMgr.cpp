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

void DevicePollingMgr::taskService(uint32_t timeNowMs)
{
    // See if any devices need polling
    std::vector<BusI2CRequestRec> busReqRecs;
    if (_busStatusMgr.getPendingIdentPollRequestsForOneDevice(timeNowMs, busReqRecs))
    {
        // Get the address and slot
        if (busReqRecs.size() == 0)
            return;
        RaftI2CAddrAndSlot addrAndSlot = busReqRecs[0].getAddrAndSlot();

        // Check if a bus extender slot is specified
        if (addrAndSlot.slotPlus1 > 0)
            _busExtenderMgr.enableOneSlot(addrAndSlot.slotPlus1);

        // Prep poll req data
        pollResultPrepare();

        // Loop through the requests
        bool allResultsOk = true;
        for (auto& busReqRec : busReqRecs)
        {
            // Perform the polling
            std::vector<uint8_t> readData;
            auto rslt = _busI2CReqSyncFn(&busReqRec, &readData);
            if (rslt != RaftI2CCentralIF::ACCESS_RESULT_OK)
            {
                allResultsOk = false;
                break;
            }

            // Add to data aggregator
            pollResultAdd(readData);

#ifdef DEBUG_POLL_RESULT
            String sss;
            Raft::getHexStrFromBytes(readData.data(), readData.size(), sss);
            LOG_I(MODULE_PREFIX, "Polling device at %s readData %s rslt %s", 
                            busReqRec.getAddrAndSlot().toString().c_str(),
                            sss.c_str(),
                            RaftI2CCentralIF::getAccessResultStr(rslt));
#endif
        }

        // Store the poll result if all requests succeeded
        if (allResultsOk)
            _busStatusMgr.pollResultStore(addrAndSlot, _pollDataResult);

        // Restore the bus extender(s) if necessary
        if (addrAndSlot.slotPlus1 > 0)
            _busExtenderMgr.setAllChannels(true);

    }
}
