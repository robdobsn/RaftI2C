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

    /// @brief Cumulative poll-response integrity counters (all devices)
    /// Zero unless a device type record declares pollInfo.crc. See RaftI2C
    /// devdocs/i2c-poll-data-integrity-crc-plan.md
    struct CrcStats
    {
        uint32_t checked = 0;       // responses with a CRC that were validated
        uint32_t failed = 0;        // first-read CRC failures
        uint32_t recovered = 0;     // failures fixed by a re-read
        uint32_t dropped = 0;       // failures not fixed - response discarded
        uint32_t recoveries = 0;    // recovery commands sent (pollInfo.crc.recover)
    };
    const CrcStats& getCrcStats() const { return _crcStats; }

private:

    // Validate (and if necessary re-read) a poll response carrying a CRC trailer.
    // On success readData is truncated to the declared length, i.e. the trailer is
    // stripped, so the decode and the record's resp.b are unaffected by the CRC.
    // Returns false if the response could not be validated and must be discarded.
    bool validatePollResponse(const DevicePollingInfo& pollInfo, BusRequestInfo& busReqRec,
                              BusElemAddrType address, std::vector<uint8_t>& readData);

    // Poll-response integrity counters
    CrcStats _crcStats;
    uint64_t _crcStatsLastReportUs = 0;

    // Consecutive dropped responses per address, for the recovery backstop. Small
    // fixed table - only devices that declare pollInfo.crc ever appear here.
    static const uint32_t MAX_RECOVERY_ENTRIES = 8;
    struct RecoveryState
    {
        BusElemAddrType address = 0;
        uint32_t consecutiveDrops = 0;
        bool inUse = false;
    };
    RecoveryState _recoveryStates[MAX_RECOVERY_ENTRIES];

    // Get (or claim) the consecutive-drop counter for an address; nullptr if the table
    // is full.
    uint32_t* getConsecutiveDropCount(BusElemAddrType address);

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
