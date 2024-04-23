/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// I2C Bus Scanner
//
// Rob Dobson 2020-2024
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "RaftUtils.h"
#include "BusScanner.h"
#include "BusI2CRequestRec.h"

// #define DEBUG_BUS_SCANNER
// #define DEBUG_SHOW_BUS_SCAN_GET_NEXT_RESULT
// #define DEBUG_MOVE_TO_NORMAL_SCANNING
// #define DEBUG_SCANNING_SWEEP_TIME

static const char* MODULE_PREFIX = "BusScanner";

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Constructor
BusScanner::BusScanner(BusStatusMgr& busStatusMgr, BusExtenderMgr& busExtenderMgr, 
                DeviceIdentMgr& deviceIdentMgr, BusI2CReqSyncFn busI2CReqSyncFn) :
    _busStatusMgr(busStatusMgr),
    _busExtenderMgr(busExtenderMgr),
    _deviceIdentMgr(deviceIdentMgr),
    _busI2CReqSyncFn(busI2CReqSyncFn)
{
    _scanLastMs = millis();
#ifdef DEBUG_DISABLE_INITIAL_FAST_SCAN
    _fastScanPendingCount = 0;
#endif
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Destructor
BusScanner::~BusScanner()
{
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Setup
void BusScanner::setup(const RaftJsonIF& config)
{
    // Bus scan period
    _slowScanPeriodMs = config.getLong("busScanPeriodMs", I2C_BUS_SLOW_SCAN_DEFAULT_PERIOD_MS);

    // Debug
    LOG_I(MODULE_PREFIX, "setup busScanPeriodMs %d", _slowScanPeriodMs);

    // Get scan priority lists
    DeviceTypeRecords::getScanPriorityLists(_scanPriorityLists);

    // Scan boost
    std::vector<String> scanBoostAddrStrs;
    config.getArrayElems("scanBoost", scanBoostAddrStrs);
    if (scanBoostAddrStrs.size() != 0)
    {
        // Resize if necessary
        if (_scanPriorityLists.size() == 0)
            _scanPriorityLists.resize(1);

        // Scan boost addresses
        for (uint32_t i = 0; i < scanBoostAddrStrs.size(); i++)
        {
            uint32_t addr = strtoul(scanBoostAddrStrs[i].c_str(), nullptr, 0);
            _scanPriorityLists[0].push_back(addr);
        }
    }

    // Scan state records
    _scanPriorityRecs.clear();
    uint32_t listTotal = 0;
    for (uint32_t i = 0; i < _scanPriorityLists.size(); i++)
    {
        // Set maxCount to square of index + 1
        ScanPriorityRec scanStateRec;
        int scanListLenDivisor = _scanPriorityLists[i].size() == 0 ? 1 : _scanPriorityLists[i].size();
        scanStateRec.maxCount = i == 0 ? 0 : (i + 1) * (i + 1) * listTotal / scanListLenDivisor + 1;
        _scanPriorityRecs.push_back(scanStateRec);
        listTotal += _scanPriorityLists[i].size();
    }
    
    // Scanner reset
    _scanState = SCAN_STATE_IDLE;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Service
void BusScanner::service()
{
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Service called from I2C task
/// @return true if fast scanning in progress
bool BusScanner::taskService()
{
    // Service bus extender manager
    _busExtenderMgr.taskService();

    // Time of last scan
    _scanLastMs = millis();

    // Check scan state
    switch(_scanState)
    {
        case SCAN_STATE_IDLE:
        {
            // Init vars and go to scan extenders
            _scanCurAddr = _busExtenderMgr.getMinAddr();
            _scanNextSlotArrayIdx = 0;
            _scanLastMs = 0;
            _scanState = SCAN_STATE_SCAN_EXTENDERS;
            _scanStateRepeatCount = 0;
            _scanStateRepeatCountMax = BusStatusMgr::I2C_ADDR_RESP_COUNT_FAIL_MAX+1;
            return true;
        }
        case SCAN_STATE_SCAN_EXTENDERS:
        case SCAN_STATE_MAIN_BUS:
        {
            // Scan main bus elements
            RaftI2CCentralIF::AccessResultCode rslt = scanOneAddress(_scanCurAddr);
            _busExtenderMgr.elemStateChange(_scanCurAddr, rslt == RaftI2CCentralIF::ACCESS_RESULT_OK);
            updateBusElemState(_scanCurAddr, 0, rslt);
            
            // Check if all addresses scanned
            _scanCurAddr++;
            if (_scanCurAddr > (_scanState == SCAN_STATE_SCAN_EXTENDERS ? _busExtenderMgr.getMaxAddr() : I2C_BUS_ADDRESS_MAX))
            {
                _scanStateRepeatCount++;
                if (_scanStateRepeatCount >= _scanStateRepeatCountMax)
                {
                    _scanState = (_scanState == SCAN_STATE_SCAN_EXTENDERS ? SCAN_STATE_MAIN_BUS : SCAN_STATE_SCAN_FAST);
#ifdef DEBUG_MOVE_TO_NORMAL_SCANNING
                    LOG_I(MODULE_PREFIX, "taskService %s", _scanState == SCAN_STATE_MAIN_BUS ? "scanning main bus" : "fast scanning main bus and slots");
#endif
                    _scanCurAddr = I2C_BUS_ADDRESS_MIN;
                    _scanStateRepeatCount = 0;
                    _scanStateRepeatCountMax = BusStatusMgr::I2C_ADDR_RESP_COUNT_FAIL_MAX+1;
                    _scanSweepStartMs = millis();
                }
                else
                {
                    _scanCurAddr = (_scanState == SCAN_STATE_SCAN_EXTENDERS ? _busExtenderMgr.getMinAddr() : I2C_BUS_ADDRESS_MIN);
                }
            }
            return true;
        }
        
        case SCAN_STATE_SCAN_FAST:
        case SCAN_STATE_SCAN_SLOW:
        {
            // Find the next address to scan
            uint32_t addr = 0;
            uint32_t slotPlus1 = 0;
            bool sweepCompleted = false;
            if (!getAddrAndGetSlotToScanNext(addr, slotPlus1, false, sweepCompleted, true))
                return false;

            // Enable slot if required
            _busExtenderMgr.enableOneSlot(slotPlus1);

            // Handle the scan
            RaftI2CCentralIF::AccessResultCode rslt = scanOneAddress(addr);
            updateBusElemState(addr, slotPlus1, rslt);

            // Clear the slot if necessary
            if (slotPlus1 > 0)
            {
                _busExtenderMgr.hardwareReset();
            }

            // Check if we have completed a sweep
            if (sweepCompleted)
            {
#ifdef DEBUG_SCANNING_SWEEP_TIME
                uint32_t sweepTimeMs = Raft::timeElapsed(millis(), _scanSweepStartMs);
                LOG_I(MODULE_PREFIX, "taskService sweep completed time %dms", sweepTimeMs);
#endif
                _scanStateRepeatCount++;
                if (_scanStateRepeatCount >= _scanStateRepeatCountMax)
                {
#ifdef DEBUG_MOVE_TO_NORMAL_SCANNING
                    LOG_I(MODULE_PREFIX, "taskService slow scanning main bus and slots");
#endif
                    _scanState = SCAN_STATE_SCAN_SLOW;
                    _scanStateRepeatCount = 0;
                }
                _scanSweepStartMs = millis();
            }
            return _scanState != SCAN_STATE_SCAN_SLOW;
        }
    }
    return false;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Check if a scan is pending
/// @return true if a scan is pending
bool BusScanner::isScanPending()
{
    switch(_scanState)
    {
        case SCAN_STATE_IDLE:
        case SCAN_STATE_SCAN_EXTENDERS:
        case SCAN_STATE_MAIN_BUS:
        case SCAN_STATE_SCAN_FAST:
            return true;
        case SCAN_STATE_SCAN_SLOW:
            return _slowScanEnabled && ((_slowScanPeriodMs == 0) || (Raft::isTimeout(millis(), _scanLastMs, _slowScanPeriodMs)));
    }
    return false;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Set current address and get slot to scan next
/// @param addr (out) Address
/// @param slotPlus1 (out) Slot number (1-based)
/// @param onlyMainBus Only main bus (don't scan extenders)
/// @param sweepCompleted (out) Sweep completed
/// @param mediumPrioritySweepCompleted Test if medium priority sweep complete (as opposed to full sweep)
/// @return True if valid
bool BusScanner::getAddrAndGetSlotToScanNext(uint32_t& addr, uint32_t& slotPlus1, bool onlyMainBus, 
            bool& sweepCompleted, bool mediumPrioritySweepCompleted)
{
    // Check if already scanning slots on current address
    sweepCompleted = false;
    if (!onlyMainBus && (_scanNextSlotArrayIdx > 0))
    {
        // Check if slots remain
        const std::vector<uint8_t> busExtenderSlots = _busExtenderMgr.getBusExtenderSlots();
        if (_scanNextSlotArrayIdx < busExtenderSlots.size())
        {
            // Return this slot
            slotPlus1 = busExtenderSlots[_scanNextSlotArrayIdx];

            // Move to next slot
            _scanNextSlotArrayIdx++;
            if (_scanNextSlotArrayIdx >= busExtenderSlots.size())
                _scanNextSlotArrayIdx = 0;
            addr = _scanCurAddr;
#ifdef DEBUG_SHOW_BUS_SCAN_GET_NEXT_RESULT
            LOG_I(MODULE_PREFIX, "getAddrAndGetSlotToScanNext slots addr %02x slot %d", addr, slotPlus1);
#endif
            return true;
        }
    }

    // Reset slot index
    _scanNextSlotArrayIdx = 0;

    // Check scan priority list valid
    if (_scanPriorityLists.size() == 0)
        return false;

    // Iterate through scan priority lists in order from lowest to highest priority
    for (uint32_t j = _scanPriorityLists.size(); j >= 1; j--)
    {
        // Get index
        uint32_t idx = j - 1;

        // Increment count and check if max reached
        _scanPriorityRecs[idx].count++;
        if (_scanPriorityRecs[idx].count > _scanPriorityRecs[idx].maxCount)
        {
            _scanPriorityRecs[idx].count = 0;

            // Reset scan list index if required
            if (_scanPriorityRecs[idx].scanListIndex >= _scanPriorityLists[idx].size())
                _scanPriorityRecs[idx].scanListIndex = 0;

            // Get address at current scan list index (if there is one)
            if (_scanPriorityRecs[idx].scanListIndex < _scanPriorityLists[idx].size())
            {
                // Get address
                addr = _scanPriorityLists[idx][_scanPriorityRecs[idx].scanListIndex];
                _scanCurAddr = addr;
                _scanPriorityRecs[idx].scanListIndex++;
                slotPlus1 = 0;
                _scanNextSlotArrayIdx = _busStatusMgr.isAddrFoundOnMainBus(addr) ? 0 : 1;

                // Check if returning last address of lowest-priority list (note that this uses the fact that scanListIndex hasn't yet been wrapped)
                if (mediumPrioritySweepCompleted)
                    sweepCompleted = (j + 1 == _scanPriorityLists.size()) && (_scanPriorityRecs[idx].scanListIndex == _scanPriorityLists[idx].size());
                else
                    sweepCompleted = (j == _scanPriorityLists.size()) && (_scanPriorityRecs[idx].scanListIndex == _scanPriorityLists[idx].size());
#ifdef DEBUG_SHOW_BUS_SCAN_GET_NEXT_RESULT
                LOG_I(MODULE_PREFIX, "getAddrAndGetSlotToScanNext other addr %02x slot %d isOnMainBus %s", 
                            addr, slotPlus1, _scanNextSlotArrayIdx == 0 ? "true" : "false");
#endif
                return true;
            }
        }
    }

#ifdef DEBUG_SHOW_BUS_SCAN_GET_NEXT_RESULT
    LOG_I(MODULE_PREFIX, "getAddrAndGetSlotToScanNext NOTHING");
#endif

    // Nothing to return
    return false;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Request a bus scan
/// @param enableSlowScan Enable slow scan
/// @param requestFastScan Request fast scan
void BusScanner::requestScan(bool enableSlowScan, bool requestFastScan)
{
    // Perform fast scans to detect online/offline status
    if (requestFastScan)
    {
        _scanState = SCAN_STATE_SCAN_FAST;
        _scanStateRepeatCount = 0;
        _scanStateRepeatCountMax = BusStatusMgr::I2C_ADDR_RESP_COUNT_FAIL_MAX+1;
    }
    _slowScanEnabled = enableSlowScan;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Scan one address
/// @param addr Address
/// @return Access result code
RaftI2CCentralIF::AccessResultCode BusScanner::scanOneAddress(uint32_t addr)
{
    BusI2CAddrAndSlot addrAndSlot(addr, 0);
    BusI2CRequestRec reqRec(BUS_REQ_TYPE_SLOW_SCAN,
                addrAndSlot,
                0, 0, 
                nullptr,
                0, 
                0,
                nullptr, 
                this);
    return _busI2CReqSyncFn(&reqRec, nullptr);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Update bus element state
/// @param addr Address
/// @param slot Slot
/// @param accessResult Access result code
void BusScanner::updateBusElemState(uint32_t addr, uint32_t slot, RaftI2CCentralIF::AccessResultCode accessResult)
{
    // Update bus element state
    bool isOnline = false;
    bool isChange = _busStatusMgr.updateBusElemState(BusI2CAddrAndSlot(addr, slot), 
                    accessResult == RaftI2CCentralIF::ACCESS_RESULT_OK, isOnline);

#ifdef DEBUG_BUS_SCANNER
    LOG_I(MODULE_PREFIX, "updateBusElemState addr %02x slot %d accessResult %d isOnline %d isChange %d", 
                addr, slot, accessResult, isOnline, isChange);
#endif

    // Change to online so start device identification
    if (isChange && isOnline)
    {
        // Attempt to identify the device
        DeviceStatus deviceStatus;
        _deviceIdentMgr.identifyDevice(BusI2CAddrAndSlot(addr, slot), deviceStatus);

        // Set device status into bus status manager for this address
        _busStatusMgr.setBusElemDeviceStatus(BusI2CAddrAndSlot(addr, slot), deviceStatus);
    }
}
