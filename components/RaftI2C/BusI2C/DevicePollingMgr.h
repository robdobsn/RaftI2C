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

class RaftI2CCentralIF;

class DevicePollingMgr
{
public:
    // Constructor
    DevicePollingMgr(BusStatusMgr& busStatusMgr, BusMultiplexers& BusMultiplexers, BusReqSyncFn busI2CReqSyncFn,
                     RaftI2CCentralIF* pI2CCentral);

    // Set I2C central interface (call after central is created if not available at construction time)
    void setI2CCentral(RaftI2CCentralIF* pI2CCentral) { _pI2CCentral = pI2CCentral; }

    // Setup
    void setup(const RaftJsonIF& config);

    // Service from I2C task
    void taskService(uint64_t timeNowUs);

private:

    // Bus status manager
    BusStatusMgr& _busStatusMgr;

    // Bus multiplexers
    BusMultiplexers& _busMultiplexers;

    // I2C request sync function
    BusReqSyncFn _busReqSyncFn;

    // I2C central interface (for bus frequency control)
    RaftI2CCentralIF* _pI2CCentral = nullptr;

#ifdef DEBUG_POLL_TIMING
    // Poll timing diagnostics per address
    struct PollTimingStats {
        uint64_t lastPollStartUs = 0;
        uint64_t cumulTransactionUs = 0;
        uint64_t cumulIntervalUs = 0;
        uint32_t cumulFifoWords = 0;
        uint32_t pollCount = 0;
        uint64_t lastReportTimeUs = 0;
    };
    static const uint32_t MAX_TIMING_ENTRIES = 8;
    BusElemAddrType _timingAddresses[MAX_TIMING_ENTRIES] = {};
    PollTimingStats _timingStats[MAX_TIMING_ENTRIES];
    uint32_t _timingEntryCount = 0;
    PollTimingStats& getTimingStats(BusElemAddrType address);
#endif

    // Debug
    static constexpr const char* MODULE_PREFIX = "RaftI2CDevPollMgr";    
};
