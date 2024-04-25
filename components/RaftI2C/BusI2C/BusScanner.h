/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// I2C Bus Scanner
//
// Rob Dobson 2020-2024
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <vector>
#include "RaftJson.h"
#include "BusI2CConsts.h"
#include "BusStatusMgr.h"
#include "BusExtenderMgr.h"
#include "RaftI2CCentralIF.h"
#include "BusI2CRequestRec.h"
#include "DeviceIdentMgr.h"

// #define DEBUG_DISABLE_INITIAL_FAST_SCAN
// #define DEBUG_SCANNING_SWEEP_TIME

class BusScanner {

public:
    BusScanner(BusStatusMgr& busStatusMgr, BusExtenderMgr& BusExtenderMgr,
                DeviceIdentMgr& deviceIdentMgr, BusI2CReqSyncFn busI2CReqSyncFn);
    ~BusScanner();
    void setup(const RaftJsonIF& config);
    void service();
    void requestScan(bool enableSlowScan, bool requestFastScan);

    ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Check if a scan is pending
    /// @return true if a scan is pending
    bool isScanPending(uint32_t curTimeMs);

    ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Service called from I2C task
    /// @param curTimeUs Current time in microseconds
    /// @param maxTimeInLoopUs Maximum time allowed in this loop
    /// @return true if fast scanning in progress
    bool taskService(uint64_t curTimeUs, uint64_t maxTimeInLoopUs);

    // Scan period
    static const uint32_t I2C_BUS_SLOW_SCAN_DEFAULT_PERIOD_MS = 5;

private:
    // Scanning state
    enum ScanState {
        SCAN_STATE_IDLE,
        SCAN_STATE_SCAN_EXTENDERS,
        SCAN_STATE_MAIN_BUS,
        SCAN_STATE_SCAN_FAST,
        SCAN_STATE_SCAN_SLOW
    };
    ScanState _scanState = SCAN_STATE_IDLE;

    const char* getScanStateStr(ScanState scanState)
    {
        switch (scanState)
        {
            case SCAN_STATE_IDLE: return "IDLE";
            case SCAN_STATE_SCAN_EXTENDERS: return "SCAN_EXTENDERS";
            case SCAN_STATE_MAIN_BUS: return "MAIN_BUS";
            case SCAN_STATE_SCAN_FAST: return "SCAN_FAST";
            case SCAN_STATE_SCAN_SLOW: return "SCAN_SLOW";
        }
        return "UNKNOWN";
    }

    // Scanning state repeat count
    uint16_t _scanStateRepeatCount = 0;
    uint16_t _scanStateRepeatCountMax = 0;

    // Scanning
    uint32_t _scanLastMs = 0;
    uint32_t _slowScanPeriodMs = I2C_BUS_SLOW_SCAN_DEFAULT_PERIOD_MS;
    uint16_t _scanNextAddr = I2C_BUS_ADDRESS_MIN;
    uint16_t _scanAddressesCurrentList = 0;
    uint16_t _scanNextSlotArrayIdxPlus1 = 0;

#ifdef DEBUG_SCANNING_SWEEP_TIME
    uint32_t _debugScanSweepStartMs = 0;
#endif
    
    // Scan priority
    std::vector<std::vector<RaftI2CAddrType>> _scanPriorityLists;
    
    // Scanning priority state
    class ScanPriorityRec {
    public:
        uint16_t count = 0;
        uint16_t maxCount = 0;
        uint16_t scanListIndex = 0;
    };
    std::vector<ScanPriorityRec> _scanPriorityRecs;

    // Enable slow scanning
    bool _slowScanEnabled = true;

    // Status manager
    BusStatusMgr& _busStatusMgr;

    // Bus extender manager
    BusExtenderMgr& _busExtenderMgr;

    // Device ident manager
    DeviceIdentMgr& _deviceIdentMgr;

    // Bus i2c request function (synchronous)
    BusI2CReqSyncFn _busI2CReqSyncFn = nullptr;

    // Helpers
    RaftI2CCentralIF::AccessResultCode scanOneAddress(uint32_t addr);
    void updateBusElemState(uint32_t addr, uint32_t slotPlus1, RaftI2CCentralIF::AccessResultCode accessResult);

    ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Set current address and get slot to scan next
    /// @param addr (out) Address
    /// @param slotPlus1 (out) Slot number (1-based)
    /// @param sweepCompleted (out) Sweep completed
    /// @param onlyMainBus Only main bus (don't scan extenders)
    /// @param ignorePriorities Ignore priorities - simply scan all addresses (and slots) equally
    /// @return True if valid
    bool getAddrAndGetSlotToScanNext(uint32_t& addr, uint32_t& slotPlus1, bool& sweepCompleted, 
                bool onlyMainBus, bool ignorePriorities);
};
