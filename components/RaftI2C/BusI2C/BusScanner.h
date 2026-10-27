/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// I2C Bus Scanner
//
// Rob Dobson 2020-2024
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <vector>
#include <atomic>
#include "RaftJson.h"
#include "BusI2CConsts.h"
#include "BusStatusMgr.h"
#include "BusI2CElemTracker.h"
#include "BusMultiplexers.h"
#include "RaftI2CCentralIF.h"
#include "DeviceIdentMgr.h"
#include "BusScanSchedule.h"

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

    /// @brief Get the number of addresses probed so far (a running count, for activity statistics)
    uint32_t getScanProbeCount() const
    {
        return _scanProbeCount;
    }

    ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Service called from I2C task
    /// @param curTimeUs Current time in microseconds
    /// @param maxFastTimeInLoopUs Maximum time allowed in this loop when fast scanning
    /// @param maxSlowTimeInLoopUs Maximum time allowed in this loop when slow scanning
    /// @return true if fast scanning in progress
    bool taskService(uint64_t curTimeUs, uint64_t maxFastTimeInLoopUs, uint64_t maxSlowTimeInLoopUs);

    // Scan period
    static const uint32_t I2C_BUS_SLOW_SCAN_DEFAULT_PERIOD_MS = 5;

    /// @brief Set the share of the bus that polling needs (from the poll intervals), which sets the scan share
    void setPollLoadPct(uint32_t pollLoadPct)
    {
        _pollLoadPct = pollLoadPct;
    }

    /// @brief Scanning status as JSON (list sizes, background addresses, share, probe counts)
    String getScanStatsJson() const;

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

    static const char* getScanStateStr(BusScanMode scanState)
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
    
    // Scan boost addresses (config "scanBoost") and the device type records generation and identification hook
    // claims the lists were built from
    std::vector<uint32_t> _scanBoostAddrs;
    std::vector<uint32_t> _boostAddrs;          // config and runtime boost addresses, as the lists were built
    uint32_t _scanListsGeneration = 0;
    uint32_t _scanClaimsEpoch = 0;
    void loadScanLists();

    // Order of probes in the fast and slow modes: priority lists of addresses something can claim, and a slow
    // background sweep of those nothing can (BusScanSchedule.h)
    BusScanSchedule _schedule;
    static const uint32_t BACKGROUND_SWEEP_DEFAULT_MS = 10000;

    // An online device polled at this interval or faster isn't scanned (its polls show it is there); one polled less
    // often is scanned as well, so that its removal is seen quickly
    static const uint32_t PRESENCE_FROM_POLLS_MAX_INTERVAL_US = 1000000;
    uint32_t _backgroundSweepMs = BACKGROUND_SWEEP_DEFAULT_MS;
    bool isAddrClaimable(uint32_t addr) const;
    uint32_t slotNumFromPos(uint32_t pos) const;
    uint32_t _numListAddrs[3] = {};
    uint32_t _numBackgroundAddrs = 0;

    // Elements that have answered once: probed again on the next pass, so a new device comes online without
    // waiting for the scan to come round again
    std::vector<std::pair<uint8_t, uint8_t>> _confirmQueue;
    static const uint32_t CONFIRM_QUEUE_MAX = 8;

    // Share of bus time for scanning in the slow mode: scanShareMaxPct on a quiet bus, falling with the poll load to
    // scanShareMinPct. Time is earned as a credit and spent by each pass
    std::atomic<uint32_t> _pollLoadPct{0};
    static const uint32_t SCAN_SHARE_MIN_DEFAULT_PCT = 10;
    static const uint32_t SCAN_SHARE_MAX_DEFAULT_PCT = 30;
    static constexpr int64_t SCAN_CREDIT_MAX_US = 20000;
    uint32_t _scanShareMinPct = SCAN_SHARE_MIN_DEFAULT_PCT;
    uint32_t _scanShareMaxPct = SCAN_SHARE_MAX_DEFAULT_PCT;
    int64_t _scanCreditUs = 0;
    uint64_t _scanCreditLastUs = 0;
    uint32_t getScanSharePct() const;
    void updateScanCredit(uint64_t timeNowUs);

    // Fast mode (after requestScan(fast)) ends after its sweeps or this long, whichever is first
    static const uint32_t FAST_SCAN_MAX_DEFAULT_MS = 2000;
    uint32_t _fastScanMaxMs = FAST_SCAN_MAX_DEFAULT_MS;
    uint32_t _fastScanStartMs = 0;

    // Slot weights (how often each multiplexer slot is scanned, relative to an empty slot's 1): slots with an
    // online device, and slots where something changed recently, are scanned more often. Updated periodically
    static const uint32_t SLOT_KNOWN_WEIGHT_DEFAULT = 2;
    static const uint32_t SLOT_CHANGED_WEIGHT_DEFAULT = 3;
    static const uint32_t SLOT_CHANGED_DEFAULT_MS = 10000;
    static const uint32_t SLOT_WEIGHTS_UPDATE_MS = 500;
    uint32_t _slotKnownWeight = SLOT_KNOWN_WEIGHT_DEFAULT;
    uint32_t _slotChangedWeight = SLOT_CHANGED_WEIGHT_DEFAULT;
    uint32_t _slotChangedMs = SLOT_CHANGED_DEFAULT_MS;
    uint32_t _slotWeightsLastMs = 0;
    uint32_t _numSlotsKnown = 0;
    uint32_t _numSlotsChanged = 0;
    void updateSlotWeights();

    // Multiplexer supervision: a multiplexer address is probed every muxCheckMs (in turn, on the slot each is reached
    // through) so a multiplexer coming or going is noticed in a few probes - otherwise its address is only probed
    // when the scan comes round to it, which can take many seconds
    static const uint32_t MUX_CHECK_DEFAULT_MS = 100;
    uint32_t _muxCheckMs = MUX_CHECK_DEFAULT_MS;
    uint32_t _muxCheckLastMs = 0;
    uint32_t _muxCheckIdx = 0;
    uint32_t _muxCheckProbeCount = 0;

    // General calls: bytes sent to the general call address (0x00) for devices that stay off the bus until they get
    // one (DeviceTypeRecord::generalCall, e.g. the CaliPile's 0x04 to load its address), from the enabled records.
    // In the fast and slow scan modes they are sent on one slot at a time in turn (a slot also passes the main bus;
    // with no slots, on the main bus), a full round taking generalCallRoundMs - so a device plugged in later is
    // woken within about that time
    static constexpr uint32_t GENERAL_CALL_ROUND_DEFAULT_MS = 2000;
    static constexpr uint32_t GENERAL_CALL_MIN_INTERVAL_MS = 50;
    std::vector<std::vector<uint8_t>> _generalCalls;
    uint32_t _generalCallRoundMs = GENERAL_CALL_ROUND_DEFAULT_MS;
    uint32_t _generalCallLastMs = 0;
    uint32_t _generalCallPos = 0;
    uint32_t _generalCallCount = 0;
    void serviceGeneralCalls();

    // Slot left selected by the last probe (0 none), so consecutive probes on a slot don't rewrite the multiplexer
    uint32_t _scanSlotSelected = 0;

    // Statistics
    uint32_t _muxRescanCount = 0;
    uint32_t _backgroundProbeCount = 0;
    uint32_t _confirmProbeCount = 0;
    uint32_t _mainBusCheckCount = 0;
    
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

    // Enable slow scanning (set from any task by requestScan)
    std::atomic<bool> _slowScanEnabled{true};

    // Fast scan requested (set from any task by requestScan and consumed by the I2C task)
    std::atomic<bool> _fastScanRequested{false};

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

    /// @brief Handle a scan request (called on the I2C task only)
    void handleScanRequest();

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
    // Number of addresses probed (running count)
    uint32_t _scanProbeCount = 0;


    // Debug
    static constexpr const char* MODULE_PREFIX = "I2CBusScanner";    
};
