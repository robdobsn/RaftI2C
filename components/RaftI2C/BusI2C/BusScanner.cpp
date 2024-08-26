/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// I2C Bus Scanner
//
// Rob Dobson 2020-2024
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "RaftUtils.h"
#include "BusScanner.h"
#include "BusRequestInfo.h"
#include "DeviceTypeRecords.h"

// #define DEBUG_ELEM_STATUS_UPDATE
// #define DEBUG_MOVE_TO_NORMAL_SCANNING
// #define DEBUG_SCAN_PRIORITY_LISTS
// #define DEBUG_SCAN_SEQUENCE
// #define DEBUG_SCAN_SEQUENCE_PRIORITY_LISTS
// #define DEBUG_NO_VALID_ADDRESS
// #define DEBUG_CANT_ENABLE_SLOT
// #define DEBUG_FORCE_SIMPLE_LINEAR_SCANNING
// #define DEBUG_LIMIT_SCAN_ADDRS_TO_LIST 0x6a, 0x70
// #define DEBUG_SCAN_MODE

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Constructor
BusScanner::BusScanner(BusStatusMgr& busStatusMgr, BusMultiplexers& busMultiplexers, 
                DeviceIdentMgr& deviceIdentMgr, BusI2CReqSyncFn busI2CReqSyncFn) :
    _busStatusMgr(busStatusMgr),
    _busMultiplexers(busMultiplexers),
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
    setScanMode(SCAN_MODE_IDLE);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Service
void BusScanner::loop()
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
    switch(_scanMode)
    {
        case SCAN_MODE_IDLE:
        {
            // Init vars and go to scan multiplexers
            setScanMode(SCAN_MODE_MAIN_BUS_MUX_ONLY);

            // Disable all slots (only works if a reset pin is defined since we don't know about
            // multiplexer addresses at this stage - but could be helpful if a soft reset occurs)
            _busMultiplexers.disableAllSlots(true);
            break;
        }
        case SCAN_MODE_MAIN_BUS_MUX_ONLY:
        case SCAN_MODE_MAIN_BUS:
        case SCAN_MODE_SCAN_FAST:
        case SCAN_MODE_SCAN_SLOW:
        {
            // Find the next address to scan - scan based on scan frequency tables
            uint32_t addr = 0;
            uint32_t slotNum = 0;
            bool addrIsValid = false;

            // Scan loop
            while (true)
            {
                // Get next address to scan
                addrIsValid = getAddrAndSlotToScanNext(addr, slotNum, sweepCompleted, 
#ifdef DEBUG_FORCE_SIMPLE_LINEAR_SCANNING
                true
#else
                false
#endif
);

                if (!addrIsValid)
                {
#ifdef DEBUG_NO_VALID_ADDRESS
                    LOG_I(MODULE_PREFIX, "taskService %s no valid address", getScanStateStr(_scanMode));
#endif
                    break;
                }

                // Only scan the main bus for addresses already known to be on the main bus - otherwise
                // they will incorrectly appear multiple times on slots
                if ((slotNum != 0) && _busStatusMgr.isAddrFoundOnMainBus(addr))
                    continue;

                // Avoid scanning a bus multiplexer address on the wrong slot
                if (_busMultiplexers.isBusMultiplexer(addr))
                {
                    if (!_busMultiplexers.isSlotCorrect(addr, slotNum))
                        continue;
                }

                // Scan the address
                bool failedToEnableSlot = false;
                auto rslt = scanOneAddress(addr, slotNum, failedToEnableSlot);
                if (!failedToEnableSlot)
                {
                    if (_busMultiplexers.elemStateChange(addr, slotNum, rslt == RaftI2CCentralIF::ACCESS_RESULT_OK))
                    {
                        // We get here if there is a change to a mux state (new mux, online/offline, etc)
                        // so we need to move back to scanning muliplexers
                        setScanMode(SCAN_MODE_MAIN_BUS_MUX_ONLY);
                    }
                    updateBusElemState(addr, slotNum, rslt);
                }
                else if (rslt == RaftI2CCentralIF::ACCESS_RESULT_BUS_STUCK)
                {
                    // Update state for all elements to offline and indicate that the bus is failing
                    _busStatusMgr.informBusStuck();

#ifdef DEBUG_CANT_ENABLE_SLOT
                    LOG_I(MODULE_PREFIX, "taskService %s bus stuck attempting to set slotNum %d", 
                                getScanStateStr(_scanMode), slotNum);
#endif
                    break;
                }

                // Disable all slots
                _busMultiplexers.disableAllSlots(false);

                // Check sweepComplete or timeout
                if (sweepCompleted || Raft::isTimeout(micros(), scanLoopStartTimeUs, _scanMode == SCAN_MODE_SCAN_FAST ? maxFastTimeInLoopUs : maxSlowTimeInLoopUs))
                    break;
            }
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
                        getScanStateStr(_scanMode), startingScanAddressList, sweepTimeMs, _scanAddressesCurrentList);
            _scanPriorityRecs[startingScanAddressList]._debugScanSweepStartMs = millis();
        }
#endif

        _scanStateRepeatCount++;
        if (_scanStateRepeatCount >= _scanStateRepeatMax)
        {
            switch (_scanMode)
            {
                case SCAN_MODE_MAIN_BUS_MUX_ONLY:
                    setScanMode(SCAN_MODE_MAIN_BUS);
                    break;
                case SCAN_MODE_MAIN_BUS: 
                    setScanMode(SCAN_MODE_SCAN_FAST);
                     break;
                case SCAN_MODE_SCAN_FAST: 
                    setScanMode(SCAN_MODE_SCAN_SLOW);
                    break;
                default: 
                    break;
            }
        }
    }
    return _scanMode != SCAN_MODE_SCAN_SLOW;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Check if a scan is pending
/// @return true if a scan is pending
bool BusScanner::isScanPending(uint32_t curTimeMs)
{
    switch(_scanMode)
    {
        case SCAN_MODE_IDLE:
        case SCAN_MODE_MAIN_BUS_MUX_ONLY:
        case SCAN_MODE_MAIN_BUS:
        case SCAN_MODE_SCAN_FAST:
            return true;
        case SCAN_MODE_SCAN_SLOW:
            return _slowScanEnabled && ((_slowScanPeriodMs == 0) || (Raft::isTimeout(curTimeMs, _scanLastMs, _slowScanPeriodMs)));
    }
    return false;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Set scan mode
/// @param scanMode Scan mode
/// @param repeatCount Repeat count
void BusScanner::setScanMode(BusScanMode scanMode, uint32_t repeatCount)
{
#ifdef DEBUG_SCAN_MODE
    LOG_I(MODULE_PREFIX, "%s setScanMode %s repeatCount %d", __func__,
                getScanStateStr(scanMode), repeatCount);
#endif
    _scanAddressesCurrentList = 0;
    _scanPriorityRecs[_scanAddressesCurrentList].scanListIndex = 0;
    _scanPriorityRecs[_scanAddressesCurrentList].scanSlotNum = 0;
    _scanStateRepeatCount = 0;
    _scanMode = scanMode;
    _scanStateRepeatMax = repeatCount;
    _scanLastMs = 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Set current address and get slot to scan next
/// @param addr (out) Address
/// @param slotNum (out) Slot number (1-based)
/// @param sweepCompleted (out) Sweep completed
/// @param ignorePriorities Ignore priorities - simply scan all addresses (and slots) equally
/// @return True if valid
bool BusScanner::getAddrAndSlotToScanNext(uint32_t& addr, uint32_t& slotNum, bool& sweepCompleted, bool ignorePriorities)
{
    // Flag for addresses on a slot done
    bool addressesOnSlotDone = false;

    // Check if we need to switch to simple scanning because of mode, lack of priority lists, etc
    if ((_scanMode == SCAN_MODE_MAIN_BUS_MUX_ONLY) || (_scanMode == SCAN_MODE_MAIN_BUS))
        ignorePriorities = true;
    if (!ignorePriorities)
    {
        if ((_scanAddressesCurrentList >= _scanPriorityLists.size()) ||
            (_scanPriorityLists[_scanAddressesCurrentList].size() == 0))
        {
            // Revert to simple scanning
            ignorePriorities = true;
        }
    }

    // Get the address to scan next
    addr = getAddrFromScanListIndex(_scanPriorityRecs[_scanAddressesCurrentList], ignorePriorities, addressesOnSlotDone);

    // Check priority scanning still ok
    if (!ignorePriorities)
    {
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
                LOG_I(MODULE_PREFIX, "getAddrAndSlotToScanNext %s %s incrementing list %d count %d maxCount %d",
                            _scanPriorityRecs[_scanAddressesCurrentList].count >= _scanPriorityRecs[_scanAddressesCurrentList].maxCount ? "breaking" : "looping",
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

    // Get the slot to scan
    slotNum = getSlotNumFromSlotIdx(_scanPriorityRecs[_scanAddressesCurrentList], sweepCompleted, addressesOnSlotDone);

#ifdef DEBUG_SCAN_SEQUENCE
    LOG_I(MODULE_PREFIX, "getAddrAndSlotToScanNext %s slots addr %02x slotNum %d sweepCompleted %s addressesOnSlotDone %s ignorePriorities %s", 
                getScanStateStr(_scanMode),
                addr, slotNum,
                sweepCompleted ? "Y" : "N",
                addressesOnSlotDone ? "Y" : "N",
                ignorePriorities ? "Y" : "N");
#endif

    return true;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get address from scan list index
/// @param scanRec Scan priority record
/// @param ignorePriorities Ignore priorities - simply scan all addresses (and slots) equally
/// @param indexWrap index has wrapped around
/// @return Address to scan
BusElemAddrType BusScanner::getAddrFromScanListIndex(ScanPriorityRec& scanRec, bool ignorePriorities, bool& indexWrap)
{
    indexWrap = false;
    uint32_t addr = I2C_BUS_ADDRESS_MIN;
    switch(_scanMode)
    {
        case SCAN_MODE_MAIN_BUS_MUX_ONLY:
        {
            uint32_t extMinAddr = _busMultiplexers.getMinAddr();
            uint32_t extMaxAddr = _busMultiplexers.getMaxAddr();
            if (scanRec.scanListIndex >= (extMaxAddr - extMinAddr + 1))
            {
                scanRec.scanListIndex = 0;
                indexWrap = true;
            }
            addr = extMinAddr + scanRec.scanListIndex;
            scanRec.scanListIndex++;
            break;
        }
        case SCAN_MODE_MAIN_BUS:
        case SCAN_MODE_SCAN_FAST:
        case SCAN_MODE_SCAN_SLOW:
        {
#ifdef DEBUG_LIMIT_SCAN_ADDRS_TO_LIST
            static constexpr uint32_t nonMuxAddrs[] = { DEBUG_LIMIT_SCAN_ADDRS_TO_LIST };
            static constexpr uint32_t nonMuxAddrsCount = sizeof(nonMuxAddrs)/sizeof(nonMuxAddrs[0]);
            static const uint32_t maxScanIndex = nonMuxAddrsCount;
            if (scanRec.scanListIndex >= maxScanIndex)
            {
                scanRec.scanListIndex = 0;
                indexWrap = true;
            }
            uint32_t addr = nonMuxAddrs[scanRec.scanListIndex];
            break;
#endif

            // Check for ignore priorities
            if (ignorePriorities)
            {
                if (scanRec.scanListIndex >= (I2C_BUS_ADDRESS_MAX - I2C_BUS_ADDRESS_MIN + 1))
                {
                    scanRec.scanListIndex = 0;
                    indexWrap = true;
                }
                addr = I2C_BUS_ADDRESS_MIN + scanRec.scanListIndex;
            }
            else
            {
                // Get the current list and priority record
                std::vector<BusElemAddrType>& scanList = _scanPriorityLists[_scanAddressesCurrentList];
                ScanPriorityRec& scanRec = _scanPriorityRecs[_scanAddressesCurrentList];

                // Check the index is valid
                if (scanRec.scanListIndex >= scanList.size())
                {
                    scanRec.scanListIndex = 0;
                    indexWrap = true;
                }

                // Check the list is valid
                if (scanList.size() == 0)
                    return I2C_BUS_ADDRESS_MIN;

                // Get address corresponding to index and increment index
                addr = scanList[scanRec.scanListIndex];
            }
            scanRec.scanListIndex++;
            break;
        }
        default:
            break;
    }
    return addr;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get slot from slot index
/// @param scanRec scan priority record
/// @param sweepCompleted (out) sweep completed
/// @param addressesOnSlotDone addresses on slot done
/// @return Slot to scan
uint32_t BusScanner::getSlotNumFromSlotIdx(ScanPriorityRec& scanRec, bool& sweepCompleted, bool addressesOnSlotDone)
{
    // Check if we are only scanning the main bus
    if ((_scanMode == SCAN_MODE_MAIN_BUS_MUX_ONLY) || (_scanMode == SCAN_MODE_MAIN_BUS))
    {
        scanRec.scanSlotNum = 0;
        if (addressesOnSlotDone)
        {
            sweepCompleted = true;
        }
        return 0;
    }

    // We want to ensure that the main bus is scanned more frequently than the slots - so use indices 0 and 1 for the main bus here
    // and then the slots are from 2 onwards
    // Check slot index is valid
    if (scanRec.scanSlotNum > _busMultiplexers.getSlotIndices().size() + 1)
        scanRec.scanSlotNum = 0;

    // Get the slotNum
    uint32_t slotNum = scanRec.scanSlotNum < 2 ? 0 : _busMultiplexers.getSlotIndices()[scanRec.scanSlotNum-2] + 1;

    // Check if we are done with addresses on a slot
    if (addressesOnSlotDone)
    {
        scanRec.scanSlotNum++;
        // Check for wrap around
        if (scanRec.scanSlotNum > _busMultiplexers.getSlotIndices().size() + 1)
        {
            scanRec.scanSlotNum = 0;
            sweepCompleted = true;
        }
    }
    return slotNum;
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
        setScanMode(SCAN_MODE_SCAN_FAST);
    }
    _slowScanEnabled = enableSlowScan;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Scan one address
/// @param addr Address
/// @param slotNum SlotNum (1-based)
/// @param failedToEnableSlot (out) Failed to enable slot
/// @return Access result code
RaftI2CCentralIF::AccessResultCode BusScanner::scanOneAddress(uint32_t addr, uint32_t slotNum, bool& failedToEnableSlot)
{
    // Enable the slot (if there is one)
    auto rslt = _busMultiplexers.enableOneSlot(slotNum);
    if (rslt != RaftI2CCentralIF::ACCESS_RESULT_OK)
    {
        failedToEnableSlot = true;
        return rslt;
    }

    // Handle the scan
    failedToEnableSlot = false;
    BusRequestInfo reqRec(BUS_REQ_TYPE_SLOW_SCAN,
                addr,
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
