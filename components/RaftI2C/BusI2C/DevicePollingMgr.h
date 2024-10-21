/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Device Polling Manager
//
// Rob Dobson 2024
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "RaftJson.h"
#include "BusStatusMgr.h"
#include "BusMultiplexers.h"

class DevicePollingMgr
{
public:
    // Constructor
    DevicePollingMgr(BusStatusMgr& busStatusMgr, BusMultiplexers& BusMultiplexers, BusReqSyncFn busI2CReqSyncFn);

    // Setup
    void setup(const RaftJsonIF& config);

    // Service from I2C task
    void taskService(uint64_t timeNowUs);

    // Poll result handling
    void pollResultPrepare(uint64_t timeNowUs, const DevicePollingInfo& pollInfo)
    {
        // Set buffer size
        _pollDataResult.resize(pollInfo.pollResultSizeIncTimestamp);
        _pPollDataResult = _pollDataResult.data();
        
        // Store the current time in ms in the poll data result
        uint32_t timeNowPollUnits = timeNowUs / DevicePollingInfo::POLL_RESULT_RESOLUTION_US;
        if (DevicePollingInfo::POLL_RESULT_TIMESTAMP_SIZE == 2)
            Raft::setBEUint16(_pPollDataResult, 0, timeNowPollUnits & 0xffff);
        else if (DevicePollingInfo::POLL_RESULT_TIMESTAMP_SIZE == 4)
            Raft::setBEUint32(_pPollDataResult, 0, timeNowPollUnits);

        // Move the pointer to the start of the data
        _pPollDataResult += DevicePollingInfo::POLL_RESULT_TIMESTAMP_SIZE;
    }
    void pollResultAdd(const DevicePollingInfo& pollInfo, std::vector<uint8_t>& readData)
    {
        // Add the data to the poll data result
        if (_pPollDataResult + readData.size() <= _pollDataResult.data() + _pollDataResult.size())
        {
            memcpy(_pPollDataResult, readData.data(), readData.size());
            _pPollDataResult += readData.size();
        }
    }

private:

    // Bus status manager
    BusStatusMgr& _busStatusMgr;

    // Bus multiplexers
    BusMultiplexers& _busMultiplexers;

    // I2C request sync function
    BusReqSyncFn _busReqSyncFn;

    // Poll data result
    std::vector<uint8_t> _pollDataResult;
    uint8_t* _pPollDataResult = nullptr;

    // Debug
    static constexpr const char* MODULE_PREFIX = "RaftI2CDevPollMgr";    
};
