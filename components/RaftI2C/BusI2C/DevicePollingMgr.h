/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Device Polling Manager
//
// Rob Dobson 2024
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "RaftJson.h"
#include "BusI2CRequestRec.h"
#include "BusStatusMgr.h"

class DevicePollingMgr
{
public:
    // Constructor
    DevicePollingMgr(BusStatusMgr& busStatusMgr, BusExtenderMgr& BusExtenderMgr, BusI2CReqSyncFn busI2CReqSyncFn);

    // Setup
    void setup(const RaftJsonIF& config);

    // Service from I2C task
    void taskService(uint32_t timeNowMs);

    // Poll result handling
    void pollResultPrepare()
    {
        // Store the current time in ms in the poll data result
        uint32_t timeNowMs = millis();
        _pollDataResult.resize(sizeof(timeNowMs));
        Raft::setBEUint32(_pollDataResult.data(), 0, timeNowMs);
    }
    void pollResultAdd(std::vector<uint8_t>& readData)
    {
        _pollDataResult.insert(_pollDataResult.end(), readData.begin(), readData.end());
    }

private:

    // Bus status manager
    BusStatusMgr& _busStatusMgr;

    // Bus extender manager
    BusExtenderMgr& _busExtenderMgr;

    // I2C request sync function
    BusI2CReqSyncFn _busI2CReqSyncFn;

    // Poll data result
    std::vector<uint8_t> _pollDataResult; 


};
