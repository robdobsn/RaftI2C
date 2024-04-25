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
    _scanLastMs = 0;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Service
void BusScanner::service()
{
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Service called from I2C task
/// @param curTimeUs Current time in microseconds
/// @param maxTimeInLoopUs Maximum time allowed in this loop
/// @return true if fast scanning in progress
bool BusScanner::taskService(uint64_t curTimeUs, uint64_t maxTimeInLoopUs)
{
    // Time of last scan
    uint32_t curTimeMs = curTimeUs / 1000;
    _scanLastMs = curTimeMs;
    uint64_t scanLoopStartTimeUs = micros();

    // Check scan state
    switch(_scanState)
    {
        case SCAN_STATE_IDLE:
        {
            // Init vars and go to scan extenders
            _scanNextAddr = _busExtenderMgr.getMinAddr();
            _scanNextSlotArrayIdxPlus1 = 0;
            _scanState = SCAN_STATE_SCAN_EXTENDERS;
            _scanStateRepeatCount = 0;
            _scanStateRepeatCountMax = BusStatusMgr::I2C_ADDR_RESP_COUNT_FAIL_MAX+1;
            break;
        }
        case SCAN_STATE_SCAN_EXTENDERS:
        case SCAN_STATE_MAIN_BUS:
        {
            // Scan loop for extenders and main bus
            while (true)
            {
#ifdef DEBUG_SHOW_BUS_SCAN_GET_NEXT_RESULT
                LOG_I(MODULE_PREFIX, "taskService %s addr %02x", 
                            _scanState == SCAN_STATE_SCAN_EXTENDERS ? "extenders" : "main-bus",
                            _scanNextAddr);
#endif

                // Scan main bus elements - simple linear scanning with all slots turned off
                RaftI2CCentralIF::AccessResultCode rslt = scanOneAddress(_scanNextAddr);
                _busExtenderMgr.elemStateChange(_scanNextAddr, rslt == RaftI2CCentralIF::ACCESS_RESULT_OK);
                updateBusElemState(_scanNextAddr, 0, rslt);
                
                // Check if all addresses scanned
                _scanNextAddr++;
                if (_scanNextAddr > (_scanState == SCAN_STATE_SCAN_EXTENDERS ? _busExtenderMgr.getMaxAddr() : I2C_BUS_ADDRESS_MAX))
                {
                    _scanStateRepeatCount++;
                    if (_scanStateRepeatCount >= _scanStateRepeatCountMax)
                    {
                        _scanState = (_scanState == SCAN_STATE_SCAN_EXTENDERS ? SCAN_STATE_MAIN_BUS : SCAN_STATE_SCAN_FAST);
    #ifdef DEBUG_MOVE_TO_NORMAL_SCANNING
                        LOG_I(MODULE_PREFIX, "taskService %s", _scanState == SCAN_STATE_MAIN_BUS ? "scanning main bus" : "fast scanning main bus and slots");
    #endif
                        _scanNextAddr = I2C_BUS_ADDRESS_MIN;
                        _scanStateRepeatCount = 0;
                        _scanStateRepeatCountMax = BusStatusMgr::I2C_ADDR_RESP_COUNT_FAIL_MAX+1;
#ifdef DEBUG_SCANNING_SWEEP_TIME                 
                        _debugScanSweepStartMs = curTimeMs;
#endif
                    }
                    else
                    {
                        _scanNextAddr = (_scanState == SCAN_STATE_SCAN_EXTENDERS ? _busExtenderMgr.getMinAddr() : I2C_BUS_ADDRESS_MIN);
                    }
                }

                // Check if time to quite looping
                if (Raft::isTimeout(micros(), scanLoopStartTimeUs, maxTimeInLoopUs))
                    break;
            }
            break;
        }
        
        case SCAN_STATE_SCAN_FAST:
        case SCAN_STATE_SCAN_SLOW:
        {
            // Find the next address to scan - scan based on scan frequency tables
            uint32_t addr = 0;
            uint32_t slotPlus1 = 0;
            uint32_t lastSlotPlus1 = UINT32_MAX;
            bool sweepCompleted = false;

            // Scan loop
            while (true)
            {
                // Get next address to scan
                if (!getAddrAndGetSlotToScanNext(addr, slotPlus1, sweepCompleted, false, false))
                    break;

                // Skip addresses on slots when the address is known to be on the main bus
                if (slotPlus1 == 0 || !_busStatusMgr.isAddrFoundOnMainBus(addr))
                {

#ifdef DEBUG_SHOW_BUS_SCAN_GET_NEXT_RESULT
                    LOG_I(MODULE_PREFIX, "taskService %s slots addr %02x slotPlus1 %d sweepCompleted %s", 
                                _scanState == SCAN_STATE_SCAN_SLOW ? "slow" : "fast",
                                addr, slotPlus1,
                                sweepCompleted ? "Y" : "N");
#endif

                    // Check if slot needs to be set
                    if ((lastSlotPlus1 == UINT32_MAX) || (lastSlotPlus1 != slotPlus1))
                    {
                        // Enable slot if required
                        if (slotPlus1 == 0)
                            _busExtenderMgr.disableAllSlots();
                        else if (!_busExtenderMgr.enableOneSlot(slotPlus1))
                            break;
                    }
                    lastSlotPlus1 = slotPlus1;

                    // Handle the scan
                    RaftI2CCentralIF::AccessResultCode rslt = scanOneAddress(addr);
                    updateBusElemState(addr, slotPlus1, rslt);
                }

                // Check if time to quite looping
                if (Raft::isTimeout(micros(), scanLoopStartTimeUs, maxTimeInLoopUs))
                    break;
            }

            // Clear the slot if necessary
            if (slotPlus1 > 0)
            {
                _busExtenderMgr.disableAllSlots();
            }

            // Check if we have completed a sweep
            if (sweepCompleted)
            {
#ifdef DEBUG_SCANNING_SWEEP_TIME
                uint32_t sweepTimeMs = Raft::timeElapsed(curTimeMs, _debugScanSweepStartMs);
                LOG_I(MODULE_PREFIX, "taskService %s sweep completed time %dms", getScanStateStr(_scanState), sweepTimeMs);
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
#ifdef DEBUG_SCANNING_SWEEP_TIME
                _debugScanSweepStartMs = curTimeMs;
#endif
            }
            return _scanState != SCAN_STATE_SCAN_SLOW;
        }
    }
    return false;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Check if a scan is pending
/// @return true if a scan is pending
bool BusScanner::isScanPending(uint32_t curTimeMs)
{
    switch(_scanState)
    {
        case SCAN_STATE_IDLE:
        case SCAN_STATE_SCAN_EXTENDERS:
        case SCAN_STATE_MAIN_BUS:
        case SCAN_STATE_SCAN_FAST:
            return true;
        case SCAN_STATE_SCAN_SLOW:
            return _slowScanEnabled && ((_slowScanPeriodMs == 0) || (Raft::isTimeout(curTimeMs, _scanLastMs, _slowScanPeriodMs)));
    }
    return false;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Set current address and get slot to scan next
/// @param addr (out) Address
/// @param slotPlus1 (out) Slot number (1-based)
/// @param sweepCompleted (out) Sweep completed
/// @param onlyMainBus Only main bus (don't scan extenders)
/// @param ignorePriorities Ignore priorities - simply scan all addresses (and slots) equally
/// @return True if valid
bool BusScanner::getAddrAndGetSlotToScanNext(uint32_t& addr, uint32_t& slotPlus1, bool& sweepCompleted,
            bool onlyMainBus, bool ignorePriorities)
{
    // Flag for addresses on a slot done
    bool addressesOnSlotDone = false;

    // Return the next address and slotPlus1
    addr = _scanNextAddr;
    slotPlus1 = 0;
    if ((_scanNextSlotArrayIdxPlus1 != 0) && (_scanNextSlotArrayIdxPlus1-1 < _busExtenderMgr.getBusExtenderSlots().size()))
        slotPlus1 = _busExtenderMgr.getBusExtenderSlots()[_scanNextSlotArrayIdxPlus1 - 1];

    // Check if ignoring priorities
    // if (ignorePriorities)
    {
        // Simply loop through addresses - check end of the address range
        _scanNextAddr++;
        if (_scanNextAddr > I2C_BUS_ADDRESS_MAX)
        {
            // Bump the slot if we're scanning slots
            _scanNextAddr = I2C_BUS_ADDRESS_MIN;
            addressesOnSlotDone = true;
        }
    }

    // // Check if position in current list is valid
    // if ((_scanAddressesCurrentList >= _scanPriorityLists.size()) || onlyMainBus)
    //     _scanAddressesCurrentList = 0;

    // Check if addresses on slot done
    if (addressesOnSlotDone)
    {
        if (!onlyMainBus)
        {
            // Move to next slot
            _scanNextSlotArrayIdxPlus1++;
#ifdef DEBUG_SHOW_BUS_SCAN_GET_NEXT_RESULT
            LOG_I(MODULE_PREFIX, "getAddrAndGetSlotToScanNext nextSlotPlus1 %d numBusExtenderSlots %d", 
                        _scanNextSlotArrayIdxPlus1, _busExtenderMgr.getBusExtenderSlots().size());
#endif
            if (_scanNextSlotArrayIdxPlus1 > _busExtenderMgr.getBusExtenderSlots().size())
            {
                _scanNextSlotArrayIdxPlus1 = 0;
                sweepCompleted = true;
            }
        }
        else
        {
            _scanNextSlotArrayIdxPlus1 = 0;
            sweepCompleted = true;
        }
    }
    return true;
}



// #ifdef SCAN_ORDER_SLOTS_THEN_ADDRESSES

//     // Check if already scanning slots on current address
//     if (!onlyMainBus && (_scanNextSlotArrayIdx > 0))
//     {
//         // Check if slots remain
//         const std::vector<uint8_t> busExtenderSlots = _busExtenderMgr.getBusExtenderSlots();
//         if (_scanNextSlotArrayIdx < busExtenderSlots.size())
//         {
//             // Return this slot
//             slotPlus1 = busExtenderSlots[_scanNextSlotArrayIdx];

//             // Move to next slot
//             _scanNextSlotArrayIdx++;
//             if (_scanNextSlotArrayIdx >= busExtenderSlots.size())
//                 _scanNextSlotArrayIdx = 0;
//             addr = _scanNextAddr;
// #ifdef DEBUG_SHOW_BUS_SCAN_GET_NEXT_RESULT
//             LOG_I(MODULE_PREFIX, "getAddrAndGetSlotToScanNext slots addr %02x slot %d", addr, slotPlus1);
// #endif
//             return true;
//         }
//     }

//     // Reset slot index
//     _scanNextSlotArrayIdx = 0;

//     // Check scan priority list valid
//     if (_scanPriorityLists.size() == 0)
//         return false;
// #endif

//     // Iterate through scan priority lists in order from lowest to highest priority
//     for (uint32_t j = _scanPriorityLists.size(); j >= 1; j--)
//     {
//         // Get index
//         uint32_t idx = j - 1;

//         // Increment count and check if max reached
//         _scanPriorityRecs[idx].count++;
//         if (_scanPriorityRecs[idx].count > _scanPriorityRecs[idx].maxCount)
//         {
//             _scanPriorityRecs[idx].count = 0;

//             // Reset scan list index if required
//             if (_scanPriorityRecs[idx].scanListIndex >= _scanPriorityLists[idx].size())
//                 _scanPriorityRecs[idx].scanListIndex = 0;

//             // Get address at current scan list index (if there is one)
//             if (_scanPriorityRecs[idx].scanListIndex < _scanPriorityLists[idx].size())
//             {
//                 // Get address
//                 addr = _scanPriorityLists[idx][_scanPriorityRecs[idx].scanListIndex];
//                 _scanNextAddr = addr;
//                 _scanPriorityRecs[idx].scanListIndex++;
//                 slotPlus1 = 0;
//                 _scanNextSlotArrayIdx = _busStatusMgr.isAddrFoundOnMainBus(addr) ? 0 : 1;

//                 // Check if returning last address of two lowest-priority lists (note that this uses the fact that scanListIndex hasn't yet been wrapped)
//                 bool mediumPrioritySweepDone = (j + 1 == _scanPriorityLists.size()) && (_scanPriorityRecs[idx].scanListIndex == _scanPriorityLists[idx].size());
//                 bool lowestPrioritySweepDone = (j == _scanPriorityLists.size()) && (_scanPriorityRecs[idx].scanListIndex == _scanPriorityLists[idx].size());
                
// #ifdef SCAN_ORDER_SLOTS_THEN_ADDRESSES
//                 if (testForMediumSweepComplete ? mediumPrioritySweepDone : lowestPrioritySweepDone)
//                 {
//                     sweepCompleted = true;
//                     return true;
//                 }
// #else
//                 // Increment slot index
//                 const std::vector<uint8_t> busExtenderSlots = _busExtenderMgr.getBusExtenderSlots();
//                 if (_scanNextSlotArrayIdx < busExtenderSlots.size())
//                 {
//                     // Return this slot
//                     slotPlus1 = busExtenderSlots[_scanNextSlotArrayIdx];

//                     // Move to next slot
//                     _scanNextSlotArrayIdx++;
//                     if (_scanNextSlotArrayIdx >= busExtenderSlots.size())
//                         _scanNextSlotArrayIdx = 0;
//                     addr = _scanNextAddr;
// #ifdef DEBUG_SHOW_BUS_SCAN_GET_NEXT_RESULT
//                     LOG_I(MODULE_PREFIX, "getAddrAndGetSlotToScanNext slots addr %02x slot %d", addr, slotPlus1);
// #endif
//                     return true;
//                 }

//                 if (lowestPrioritySweepDone)
//                 {
//                     sweepCompleted = true;
//                     return true;
//                 }
// #endif
// #ifdef DEBUG_SHOW_BUS_SCAN_GET_NEXT_RESULT
//                 LOG_I(MODULE_PREFIX, "getAddrAndGetSlotToScanNext other addr %02x slot %d isOnMainBus %s", 
//                             addr, slotPlus1, _scanNextSlotArrayIdx == 0 ? "true" : "false");
// #endif
//                 return true;
//             }
//         }
//     }

// #endif // SCAN_ORDER_SLOTS_THEN_ADDRESSES

// #ifdef DEBUG_SHOW_BUS_SCAN_GET_NEXT_RESULT
//     LOG_I(MODULE_PREFIX, "getAddrAndGetSlotToScanNext NOTHING");
// #endif

//     // Nothing to return
//     return false;
// }

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
