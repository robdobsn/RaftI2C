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
#include "BusI2CElemTracker.h"
#include "BusMultiplexers.h"
#include "RaftI2CCentralIF.h"
#include "DeviceIdentMgr.h"

// #define DEBUG_SCANNING_SWEEP_TIME

class BusScanner {

public:
    BusScanner(BusStatusMgr& busStatusMgr, 
            BusI2CElemTracker& busElemTracker, 
            BusMultiplexers& BusMultiplexers,
            BusIOExpanders& busIOExpanders,
            DeviceIdentMgr& deviceIdentMgr, 
            BusReqSyncFn busI2CReqSyncFn);
    ~BusScanner();
    void setup(const RaftJsonIF& config);
    void loop();
    void requestScan(bool enableSlowScan, bool requestFastScan);

    ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Check if a scan is pending
    /// @return true if a scan is pending
    bool isScanPending(uint32_t curTimeMs);

    ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Service called from I2C task
    /// @param curTimeUs Current time in microseconds
    /// @param maxFastTimeInLoopUs Maximum time allowed in this loop when fast scanning
    /// @param maxSlowTimeInLoopUs Maximum time allowed in this loop when slow scanning
    /// @return true if fast scanning in progress
    bool taskService(uint64_t curTimeUs, uint64_t maxFastTimeInLoopUs, uint64_t maxSlowTimeInLoopUs);

    // Scan period
    static const uint32_t I2C_BUS_SLOW_SCAN_DEFAULT_PERIOD_MS = 5;

private:
    // Scanning mode
    enum BusScanMode {
        SCAN_MODE_IDLE,
        SCAN_MODE_MAIN_BUS_MUX_ONLY,
        SCAN_MODE_MAIN_BUS,
        SCAN_MODE_SCAN_FAST,
        SCAN_MODE_SCAN_SLOW
    };
    BusScanMode _scanMode = SCAN_MODE_IDLE;

    const char* getScanStateStr(BusScanMode scanState)
    {
        switch (scanState)
        {
            case SCAN_MODE_IDLE: return "IDLE";
            case SCAN_MODE_MAIN_BUS_MUX_ONLY: return "MAIN_MUX";
            case SCAN_MODE_MAIN_BUS: return "MAIN_BUS";
            case SCAN_MODE_SCAN_FAST: return "SCAN_FAST";
            case SCAN_MODE_SCAN_SLOW: return "SCAN_SLOW";
        }
        return "UNKNOWN";
    }

    // Scanning state repeat count
    uint16_t _scanStateRepeatCount = 0;
    uint16_t _scanStateRepeatMax = BusAddrStatus::ADDR_RESP_COUNT_FAIL_MAX_DEFAULT+1;

    // Scanning
    uint32_t _scanLastMs = 0;
    uint32_t _slowScanPeriodMs = I2C_BUS_SLOW_SCAN_DEFAULT_PERIOD_MS;
    uint16_t _scanAddressesCurrentList = 0;
    
    // Scan priority
    std::vector<std::vector<BusElemAddrType>> _scanPriorityLists;
    
    // Scanning priority state - three MUST be exactly the same number
    // of scan priority recs as there are scan priority lists
    class ScanPriorityRec {
    public:
        uint16_t count = 0;
        uint16_t maxCount = 0;
        uint16_t scanListIndex = 0;
        uint16_t scanSlotNum = 0;
#ifdef DEBUG_SCANNING_SWEEP_TIME
        uint32_t _debugScanSweepStartMs = 0;
#endif

    };
    std::vector<ScanPriorityRec> _scanPriorityRecs;

    // Scan priority counts
    static constexpr uint16_t SCAN_PRIORITY_COUNTS[] = { 1, 3, 9 };

    // Enable slow scanning
    bool _slowScanEnabled = true;

    // Status manager
    BusStatusMgr& _busStatusMgr;

    // Bus element tracker (keeps track of whether element found on the main bus or on an extender)
    BusI2CElemTracker& _busElemTracker;

    // Bus multiplexers
    BusMultiplexers& _busMultiplexers;

    // Bus IO Expanders
    BusIOExpanders& _busIOExpanders;

    // Device ident manager
    DeviceIdentMgr& _deviceIdentMgr;

    // Bus i2c request function (synchronous)
    BusReqSyncFn _busReqSyncFn = nullptr;

    /// @brief Set scan mode
    /// @param scanMode Scan mode
    void setScanMode(BusScanMode scanMode, uint32_t maxRepeat = BusAddrStatus::ADDR_RESP_COUNT_FAIL_MAX_DEFAULT+1);

    /// @brief Scan one address and slot
    /// @param addr Address
    /// @param slot Slot
    /// @param failedToEnableSlot (out) Failed to enable slot
    /// @return Access result code
    RaftRetCode scanOneAddress(uint32_t addr, uint32_t slot, bool& failedToEnableSlot);

    // Helpers
    void updateBusElemState(uint32_t addr, uint32_t slotNum, RaftRetCode accessResult);

    /// @brief Set current address and get slot to scan next (based on scan mode)
    /// @param addr (out) Address
    /// @param slotNum (out) Slot number (1-based)
    /// @param sweepCompleted (out) Sweep completed
    /// @param ignorePriorities Ignore priorities - simply scan all addresses (and slots) equally
    /// @return True if valid
    bool getAddrAndSlotToScanNext(uint32_t& addr, uint32_t& slotNum, bool& sweepCompleted, bool ignorePriorities);

    /// @brief Get address from scan list index
    /// @param scanRec Scan priority record
    /// @param ignorePriorities Ignore priorities - simply scan all addresses (and slots) equally
    /// @param indexWrap index has wrapped around
    /// @return Address to scan
    BusElemAddrType getAddrFromScanListIndex(ScanPriorityRec& scanRec, bool ignorePriorities, bool& indexWrap);

    /// @brief Get slot number from slot index
    /// @param scanRec scan priority record
    /// @param sweepCompleted (out) sweep completed
    /// @param addressesOnSlotDone addresses on slot done
    /// @return Slot number
    uint32_t getSlotNumFromSlotIdx(ScanPriorityRec& scanRec, bool& sweepCompleted, bool addressesOnSlotDone);

    // Debug
    static constexpr const char* MODULE_PREFIX = "I2CBusScanner";    
};
