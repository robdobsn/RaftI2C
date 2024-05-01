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
// #define DEBUG_MOVE_TO_NORMAL_SCANNING
// #define DEBUG_SCAN_PRIORITY_LISTS
// #define DEBUG_SCAN_SEQUENCE
// #define DEBUG_SCAN_SEQUENCE_PRIORITY_LISTS
// #define DEBUG_NO_VALID_ADDRESS
// #define DEBUG_CANT_ENABLE_SLOT

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

    // Ensure there is at least one scan priority list
    if (_scanPriorityLists.size() == 0)
        _scanPriorityLists.resize(1);

    // Scan boost
    std::vector<String> scanBoostAddrStrs;
    config.getArrayElems("scanBoost", scanBoostAddrStrs);
    if (scanBoostAddrStrs.size() != 0)
    {
        // Scan boost addresses
        for (uint32_t i = 0; i < scanBoostAddrStrs.size(); i++)
        {
            uint32_t addr = strtoul(scanBoostAddrStrs[i].c_str(), nullptr, 0);
            // Add to the highest priority list
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

        // Set maxCount to the scan priority count if specified
        if (i < sizeof(SCAN_PRIORITY_COUNTS)/sizeof(SCAN_PRIORITY_COUNTS[0]))
            scanStateRec.maxCount = SCAN_PRIORITY_COUNTS[i];
        else
            scanStateRec.maxCount = (i+1) * (i+1);

        // Add the record
        _scanPriorityRecs.push_back(scanStateRec);
        listTotal += _scanPriorityLists[i].size();

#ifdef DEBUG_SCAN_PRIORITY_LISTS
        LOG_I(MODULE_PREFIX, "setup scanPriorityList %d addrListSize %d total %d maxCount %d", 
                            i, _scanPriorityLists[i].size(), listTotal, scanStateRec.maxCount);
#endif
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
/// @param maxFastTimeInLoopUs Maximum time allowed in this loop when fast scanning
/// @param maxSlowTimeInLoopUs Maximum time allowed in this loop when slow scanning
/// @return true if fast scanning in progress
bool BusScanner::taskService(uint64_t curTimeUs, uint64_t maxFastTimeInLoopUs, uint64_t maxSlowTimeInLoopUs)
{
    // Time of last scan
    uint32_t curTimeMs = curTimeUs / 1000;
    _scanLastMs = curTimeMs;
    uint64_t scanLoopStartTimeUs = micros();
    bool sweepCompleted = false;

#ifdef DEBUG_SCANNING_SWEEP_TIME
    uint32_t startingScanAddressList = _scanAddressesCurrentList;
#endif

    // Check scan state
    switch(_scanState)
    {
        case SCAN_STATE_IDLE:
        {
            // Init vars and go to scan extenders
            _scanAddressesCurrentList = 0;
            _scanPriorityRecs[_scanAddressesCurrentList].scanListIndex = 0;
            _scanPriorityRecs[_scanAddressesCurrentList].scanSlotIndexPlus1 = 0;
            _scanState = SCAN_STATE_SCAN_EXTENDERS;
            _scanStateRepeatCount = 0;
            break;
        }
        case SCAN_STATE_SCAN_EXTENDERS:
        case SCAN_STATE_MAIN_BUS:
        {
            // Disable all slots
            _busExtenderMgr.disableAllSlots(false);

            // Scan loop for extenders and main bus
            while (true)
            {
                // Get address to scan
                uint32_t addr = 0;
                uint32_t slotPlus1 = 0;
                if (!getAddrAndGetSlotToScanNext(addr, slotPlus1, sweepCompleted, true, _scanState == SCAN_STATE_SCAN_EXTENDERS, true))
                    break;

                // Scan main bus elements - simple linear scanning with all slots turned off
                RaftI2CCentralIF::AccessResultCode rslt = scanOneAddress(addr);
                _busExtenderMgr.elemStateChange(addr, rslt == RaftI2CCentralIF::ACCESS_RESULT_OK);
                updateBusElemState(addr, 0, rslt);

                // Check sweep completed or timeout
                if (sweepCompleted || Raft::isTimeout(micros(), scanLoopStartTimeUs, maxFastTimeInLoopUs))
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
            bool addrIsValid = false;

            // Scan loop
            while (true)
            {
                // Get next address to scan
                addrIsValid = getAddrAndGetSlotToScanNext(addr, slotPlus1, sweepCompleted, false, false, false);
                if (!addrIsValid)
                {
#ifdef DEBUG_NO_VALID_ADDRESS
                    LOG_I(MODULE_PREFIX, "taskService %s no valid address", getScanStateStr(_scanState));
#endif
                    break;
                }

                // Only scan the main bus for addresses already known to be on the main bus - otherwise
                // they will incorrectly appear multiple times on slots
                if ((slotPlus1 != 0) && _busStatusMgr.isAddrFoundOnMainBus(addr))
                    continue;

                // Enable the slot (if there is one)
                auto rslt = _busExtenderMgr.enableOneSlot(slotPlus1);
                if (rslt == RaftI2CCentralIF::ACCESS_RESULT_OK)
                {
                    // Handle the scan
                    rslt = scanOneAddress(addr);
                    updateBusElemState(addr, slotPlus1, rslt);
                }
                else if (rslt == RaftI2CCentralIF::ACCESS_RESULT_BUS_STUCK)
                {
                    // Update state for all elements to offline and indicate that the bus is failing
                    _busStatusMgr.informBusStuck();

#ifdef DEBUG_CANT_ENABLE_SLOT
                    LOG_I(MODULE_PREFIX, "taskService %s bus stuck attempting to set slotPlus1 %d", 
                                getScanStateStr(_scanState), slotPlus1);
#endif
                    break;
                }

                // Check sweepComplete or timeout
                if (sweepCompleted || Raft::isTimeout(micros(), scanLoopStartTimeUs, _scanState == SCAN_STATE_SCAN_FAST ? maxFastTimeInLoopUs : maxSlowTimeInLoopUs))
                    break;
            }

            // Disable all slots
            _busExtenderMgr.disableAllSlots(false);
            break;
        }
    }

    // Check if a sweep has completed
    if (sweepCompleted)
    {
#ifdef DEBUG_SCANNING_SWEEP_TIME
        if (startingScanAddressList < _scanPriorityRecs.size())
        {
            uint32_t sweepTimeMs = Raft::timeElapsed(curTimeMs, _scanPriorityRecs[startingScanAddressList]._debugScanSweepStartMs);
            LOG_I(MODULE_PREFIX, "taskService %s priority %d sweep completed time %dms (next priority %d)", 
                        getScanStateStr(_scanState), startingScanAddressList, sweepTimeMs, _scanAddressesCurrentList);
            _scanPriorityRecs[startingScanAddressList]._debugScanSweepStartMs = millis();
        }
#endif

        _scanStateRepeatCount++;
        if (_scanStateRepeatCount >= _scanStateRepeatMax)
        {
            switch (_scanState)
            {
                case SCAN_STATE_SCAN_EXTENDERS: _scanState = SCAN_STATE_MAIN_BUS; break;
                case SCAN_STATE_MAIN_BUS: _scanState = SCAN_STATE_SCAN_FAST; break;
                case SCAN_STATE_SCAN_FAST: _scanState = SCAN_STATE_SCAN_SLOW; break;
                default: break;
            }

            // Reset counts
            _scanStateRepeatCount = 0;
        }
    }
    return _scanState != SCAN_STATE_SCAN_SLOW;
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
/// @param onlyMainBus Only main bus (don't scan slots)
/// @param onlyExtenderAddrs Only return extender addresses (only valid if ignorePriorities is true)
/// @param ignorePriorities Ignore priorities - simply scan all addresses (and slots) equally
/// @return True if valid
bool BusScanner::getAddrAndGetSlotToScanNext(uint32_t& addr, uint32_t& slotPlus1, bool& sweepCompleted,
            bool onlyMainBus, bool onlyExtenderAddrs, bool ignorePriorities)
{
    // Flag for addresses on a slot done
    bool addressesOnSlotDone = false;

    // Check for simple scanning sequence
    bool useSimpleScanningSequence = ignorePriorities;

    // Check if we need to switch to simple scanning because of a lack of priority lists, etc
    if (!useSimpleScanningSequence)
    {
        if ((_scanAddressesCurrentList >= _scanPriorityLists.size()) ||
            (_scanPriorityLists[_scanAddressesCurrentList].size() == 0))
        {
            // Revert to simple scanning
            useSimpleScanningSequence = true;
        }
    }

    // Check priority scanning still ok
    if (!useSimpleScanningSequence)
    {
        // Get address corresponding to index and increment index
        addr = getAddrFromScanListIndex(_scanPriorityRecs[_scanAddressesCurrentList], SCAN_INDEX_PRIORITY_LIST_INDEX, addressesOnSlotDone);

        // See if we have finished the list
        if (addressesOnSlotDone)
        {
            // Find the next list that is ready to process
            for (int i = 0; i < _scanPriorityLists.size(); i++)
            {
                // Get the next list
                _scanAddressesCurrentList++;
                if (_scanAddressesCurrentList >= _scanPriorityLists.size())
                    _scanAddressesCurrentList = 0;

                // Check the list isn't empty
                if (_scanPriorityLists[_scanAddressesCurrentList].size() == 0)
                    continue;

                // Increment the count and check if this list is ready to process
                _scanPriorityRecs[_scanAddressesCurrentList].count++;
#ifdef DEBUG_SCAN_SEQUENCE_PRIORITY_LISTS
                LOG_I(MODULE_PREFIX, "getAddrAndGetSlotToScanNext %s %s %s incrementing list %d count %d maxCount %d",
                            _scanPriorityRecs[_scanAddressesCurrentList].count >= _scanPriorityRecs[_scanAddressesCurrentList].maxCount ? "breaking" : "looping",
                            onlyMainBus ? "main-bus" : "inc-slots", 
                            ignorePriorities ? "ignore-priorities" : "priority",
                            _scanAddressesCurrentList, _scanPriorityRecs[_scanAddressesCurrentList].count, 
                            _scanPriorityRecs[_scanAddressesCurrentList].maxCount);
#endif
                // Check if we've found a list ready to process
                if (_scanPriorityRecs[_scanAddressesCurrentList].count >= _scanPriorityRecs[_scanAddressesCurrentList].maxCount)
                    break;
            }

            // Reset the list index and count
            _scanPriorityRecs[_scanAddressesCurrentList].scanListIndex = 0;
            _scanPriorityRecs[_scanAddressesCurrentList].count = 0;
        }
    }

    // Check if we need to use a simple scanning sequence
    if (useSimpleScanningSequence)
    {
        // Ensure highest-priority record is used
        _scanAddressesCurrentList = 0;

        // Simply loop through addresses
        addr = getAddrFromScanListIndex(_scanPriorityRecs[_scanAddressesCurrentList], onlyExtenderAddrs ? SCAN_INDEX_EXTENDERS_ONLY : SCAN_INDEX_I2C_ADDRESSES, addressesOnSlotDone);
    }

    // Get the slot to scan
    slotPlus1 = getSlotPlus1FromSlotIndex(_scanPriorityRecs[_scanAddressesCurrentList], sweepCompleted, onlyMainBus, addressesOnSlotDone);

#ifdef DEBUG_SCAN_SEQUENCE
    LOG_I(MODULE_PREFIX, "getAddrAndGetSlotToScanNext %s slots addr %02x slotPlus1 %d sweepCompleted %s addressesOnSlotDone %s onlyMainBus %s onlyExtenderAddrs %s ignorePriorities %s", 
                getScanStateStr(_scanState),
                addr, slotPlus1,
                sweepCompleted ? "Y" : "N",
                addressesOnSlotDone ? "Y" : "N",
                onlyMainBus ? "Y" : "N",
                onlyExtenderAddrs ? "Y" : "N",
                ignorePriorities ? "Y" : "N");
#endif

    return true;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get address from scan list index
/// @param scanRec Scan priority record
/// @param scanMode current scanning mode
/// @param indexWrap index has wrapped around
/// @return Address to scan
uint32_t BusScanner::getAddrFromScanListIndex(ScanPriorityRec& scanRec, ScanIndexMode scanMode, bool& indexWrap)
{
    indexWrap = false;
    switch(scanMode)
    {
        case SCAN_INDEX_EXTENDERS_ONLY:
        {
            uint32_t extMinAddr = _busExtenderMgr.getMinAddr();
            uint32_t extMaxAddr = _busExtenderMgr.getMaxAddr();
            if (scanRec.scanListIndex >= (extMaxAddr - extMinAddr + 1))
                scanRec.scanListIndex = 0;
            uint32_t addr = extMinAddr + scanRec.scanListIndex;
            scanRec.scanListIndex++;
            if (scanRec.scanListIndex >= (extMaxAddr - extMinAddr + 1))
            {
                scanRec.scanListIndex = 0;
                indexWrap = true;
            }
            return addr;
        }
        case SCAN_INDEX_I2C_ADDRESSES:
        {
            if (scanRec.scanListIndex >= (I2C_BUS_ADDRESS_MAX - I2C_BUS_ADDRESS_MIN + 1))
                scanRec.scanListIndex = 0;
            uint32_t addr = I2C_BUS_ADDRESS_MIN + scanRec.scanListIndex;
            scanRec.scanListIndex++;
            if (scanRec.scanListIndex >= (I2C_BUS_ADDRESS_MAX - I2C_BUS_ADDRESS_MIN + 1))
            {
                scanRec.scanListIndex = 0;
                indexWrap = true;
            }
            return addr;
        }
        case SCAN_INDEX_PRIORITY_LIST_INDEX:
        {
            // Get the current list and priority record
            std::vector<RaftI2CAddrType>& scanList = _scanPriorityLists[_scanAddressesCurrentList];
            ScanPriorityRec& scanRec = _scanPriorityRecs[_scanAddressesCurrentList];

            // Check the index is valid
            if (scanRec.scanListIndex >= scanList.size())
                scanRec.scanListIndex = 0;

            // Check the list is valid
            if (scanList.size() == 0)
                return I2C_BUS_ADDRESS_MIN;

            // Get address corresponding to index and increment index
            uint32_t addr = scanList[scanRec.scanListIndex];
            scanRec.scanListIndex++;
            if (scanRec.scanListIndex >= scanList.size())
            {
                scanRec.scanListIndex = 0;
                indexWrap = true;
            }
            return addr;
        }
    }
    return I2C_BUS_ADDRESS_MIN;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get slot from slot index
/// @param scanRec Scan priority record
/// @param sweepCompleted Sweep completed
/// @param onlyMainBus Only main bus
/// @param addressesOnSlotDone Addresses on slot done
/// @return Slot to scan
uint32_t BusScanner::getSlotPlus1FromSlotIndex(ScanPriorityRec& scanRec, bool& sweepCompleted, bool onlyMainBus, bool addressesOnSlotDone)
{
    // Check if we are only scanning the main bus
    if (onlyMainBus)
    {
        scanRec.scanSlotIndexPlus1 = 0;
        if (addressesOnSlotDone)
        {
            sweepCompleted = true;
        }
        return 0;
    }

    // We need to ensure that the main bus is scanned more frequently than the slots - so use indices 0 and 1 for the main bus here
    // and then the slots are from 2 onwards
    // Check slot index is valid
    if (scanRec.scanSlotIndexPlus1 > _busExtenderMgr.getSlotIndices().size() + 1)
        scanRec.scanSlotIndexPlus1 = 0;

    // Get the slotPlus1
    uint32_t slotPlus1 = scanRec.scanSlotIndexPlus1 < 2 ? 0 : _busExtenderMgr.getSlotIndices()[scanRec.scanSlotIndexPlus1-2] + 1;

    // Check if we are done with addresses on a slot
    if (addressesOnSlotDone)
    {
        scanRec.scanSlotIndexPlus1++;
        // Check for wrap around
        if (scanRec.scanSlotIndexPlus1 > _busExtenderMgr.getSlotIndices().size() + 1)
        {
            scanRec.scanSlotIndexPlus1 = 0;
            sweepCompleted = true;
        }
    }
    return slotPlus1;
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
        _scanStateRepeatMax = BusStatusMgr::I2C_ADDR_RESP_COUNT_FAIL_MAX+1;
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
