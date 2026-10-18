/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// I2C Bus Scanner
//
// Rob Dobson 2020-2024
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "RaftUtils.h"
#include "BusScanner.h"
#include <algorithm>
#include "BusRequestInfo.h"
#include "DeviceTypeRecords.h"
#include "BusI2CAddrAndSlot.h"

// #define DEBUG_ELEM_STATUS_DEVICE_IDENT
// #define DEBUG_ELEM_STATUS_UPDATE_ALL
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
BusScanner::BusScanner(BusStatusMgr& busStatusMgr, 
        BusI2CElemTracker& busElemTracker, 
        BusMultiplexers& busMultiplexers,
        BusIOExpanders& busIOExpanders,
        DeviceIdentMgr& deviceIdentMgr, 
        BusReqSyncFn busI2CReqSyncFn) :
    _busStatusMgr(busStatusMgr),
    _busElemTracker(busElemTracker),
    _busMultiplexers(busMultiplexers),
    _busIOExpanders(busIOExpanders),
    _deviceIdentMgr(deviceIdentMgr),
    _busReqSyncFn(busI2CReqSyncFn)
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
    _backgroundSweepMs = config.getLong("backgroundSweepMs", BACKGROUND_SWEEP_DEFAULT_MS);
    _scanShareMinPct = config.getLong("scanShareMinPct", SCAN_SHARE_MIN_DEFAULT_PCT);
    _scanShareMaxPct = config.getLong("scanShareMaxPct", SCAN_SHARE_MAX_DEFAULT_PCT);
    if (_scanShareMaxPct < _scanShareMinPct)
        _scanShareMaxPct = _scanShareMinPct;
    _fastScanMaxMs = config.getLong("fastScanMaxMs", FAST_SCAN_MAX_DEFAULT_MS);
    _muxCheckMs = config.getLong("muxCheckMs", MUX_CHECK_DEFAULT_MS);
    _slotKnownWeight = config.getLong("slotKnownWeight", SLOT_KNOWN_WEIGHT_DEFAULT);
    _slotChangedWeight = config.getLong("slotChangedWeight", SLOT_CHANGED_WEIGHT_DEFAULT);
    _slotChangedMs = config.getLong("slotChangedMs", SLOT_CHANGED_DEFAULT_MS);

    // Debug
    LOG_I(MODULE_PREFIX, "setup busScanPeriodMs %d", _slowScanPeriodMs);

    // Scan boost addresses (added to the highest priority list)
    _scanBoostAddrs.clear();
    std::vector<String> scanBoostAddrStrs;
    config.getArrayElems("scanBoost", scanBoostAddrStrs);
    for (const String& addrStr : scanBoostAddrStrs)
        _scanBoostAddrs.push_back(strtoul(addrStr.c_str(), nullptr, 0));

    // Scan priority lists (built from the device type records, and rebuilt when records are added)
    loadScanLists();

    // Scanner reset
    setScanMode(SCAN_MODE_IDLE);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Service
void BusScanner::loop()
{
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Load the scan priority lists from the device type records (plus the scan boost addresses)
/// @note The last list holds every scanned address not in a higher one: those that nothing can claim (no record
///       identifies or claims them, the identification hook doesn't handle them, and they aren't a multiplexer's
///       or a scan boost address) are moved to the background sweep. Each list keeps its scan position where it is
///       unchanged in size
void BusScanner::loadScanLists()
{
    _scanListsGeneration = deviceTypeRecords.getGeneration();
    _scanClaimsEpoch = _deviceIdentMgr.getHookClaimsEpoch();
    std::vector<std::vector<BusElemAddrType>> lists;
    deviceTypeRecords.getScanPriorityLists(lists);
    if (lists.size() == 0)
        lists.resize(1);
    for (uint32_t addr : _scanBoostAddrs)
        lists[0].push_back(addr);

    std::vector<std::vector<uint8_t>> scheduleLists(lists.size());
    std::vector<uint8_t> background;
    for (uint32_t i = 0; i < lists.size(); i++)
    {
        bool isLast = i == lists.size() - 1;
        for (BusElemAddrType addr : lists[i])
        {
            if (isLast && (lists.size() > 1) && !isAddrClaimable(addr))
                background.push_back((uint8_t)addr);
            else
                scheduleLists[i].push_back((uint8_t)addr);
        }
    }
    _schedule.setLists(scheduleLists);
    _schedule.setBackground(background, _backgroundSweepMs);
    for (uint32_t i = 0; i < 3; i++)
        _numListAddrs[i] = i < scheduleLists.size() ? scheduleLists[i].size() : 0;
    _numBackgroundAddrs = background.size();
    LOG_I(MODULE_PREFIX, "loadScanLists lists %d/%d/%d background %d", (int)_numListAddrs[0], (int)_numListAddrs[1],
                (int)_numListAddrs[2], (int)_numBackgroundAddrs);

    // Linear scanning (startup phases) uses one record
    if (_scanPriorityRecs.empty())
        _scanPriorityRecs.resize(1);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Check whether something could claim a device at an address (so it is scanned often)
bool BusScanner::isAddrClaimable(uint32_t addr) const
{
    if (deviceTypeRecords.isAddrReserved(addr) || !deviceTypeRecords.getIdentCandidates(addr).empty())
        return true;
    if (_deviceIdentMgr.isAddrClaimedByHook(addr))
        return true;
    if ((addr >= _busMultiplexers.getMinAddr()) && (addr <= _busMultiplexers.getMaxAddr()))
        return true;
    return std::find(_scanBoostAddrs.begin(), _scanBoostAddrs.end(), addr) != _scanBoostAddrs.end();
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Slot number for a schedule slot position (0 the main bus, 1 + n the nth slot)
uint32_t BusScanner::slotNumFromPos(uint32_t pos) const
{
    const auto& slotIndices = _busMultiplexers.getSlotIndices();
    if ((pos == 0) || (pos - 1 >= slotIndices.size()))
        return 0;
    return slotIndices[pos - 1] + 1;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Weight each slot by its activity: a slot with an online device is scanned slotKnownWeight times as often
///        as an empty one, and one where something changed in the last slotChangedMs slotChangedWeight times
void BusScanner::updateSlotWeights()
{
    const auto& slotIndices = _busMultiplexers.getSlotIndices();
    uint32_t maxSlotNum = 0;
    for (uint8_t slotIdx : slotIndices)
        maxSlotNum = std::max(maxSlotNum, (uint32_t)slotIdx + 1);
    std::vector<uint32_t> lastChangeMs;
    std::vector<uint8_t> numOnline;
    _busStatusMgr.getSlotActivity(maxSlotNum + 1, lastChangeMs, numOnline);
    std::vector<uint8_t> weights(slotIndices.size(), 1);
    uint32_t timeNowMs = millis();
    _numSlotsKnown = 0;
    _numSlotsChanged = 0;
    for (uint32_t i = 0; i < slotIndices.size(); i++)
    {
        uint32_t slotNum = slotIndices[i] + 1;
        if (numOnline[slotNum] > 0)
        {
            weights[i] = std::max(weights[i], (uint8_t)_slotKnownWeight);
            _numSlotsKnown++;
        }
        if ((lastChangeMs[slotNum] != 0) && !Raft::isTimeout(timeNowMs, lastChangeMs[slotNum], _slotChangedMs))
        {
            weights[i] = std::max(weights[i], (uint8_t)_slotChangedWeight);
            _numSlotsChanged++;
        }
    }
    _schedule.setSlotWeights(weights);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Share of bus time for scanning in the slow mode, from the poll load
uint32_t BusScanner::getScanSharePct() const
{
    uint32_t load = _pollLoadPct;
    uint32_t share = load >= 100 ? 0 : _scanShareMaxPct * (100 - load) / 100;
    return share < _scanShareMinPct ? _scanShareMinPct : share;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Earn scanning time since the last update
void BusScanner::updateScanCredit(uint64_t timeNowUs)
{
    if (_scanCreditLastUs == 0)
        _scanCreditLastUs = timeNowUs;
    uint64_t elapsedUs = timeNowUs - _scanCreditLastUs;
    _scanCreditLastUs = timeNowUs;
    _scanCreditUs += (int64_t)(elapsedUs * getScanSharePct() / 100);
    if (_scanCreditUs > SCAN_CREDIT_MAX_US)
        _scanCreditUs = SCAN_CREDIT_MAX_US;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Scanning status as JSON
String BusScanner::getScanStatsJson() const
{
    char buf[320];
    snprintf(buf, sizeof(buf), R"({"mode":"%s","lists":[%d,%d,%d],"background":%d,"sharePct":%d,"bgProbes":%d,"confirmProbes":%d,"mainBusChecks":%d,"slots":%d,"slotsKnown":%d,"slotsChanged":%d,"slotCycle":%d,"muxRescans":%d,"muxChecks":%d})",
                getScanStateStr(_scanMode), (int)_numListAddrs[0], (int)_numListAddrs[1], (int)_numListAddrs[2],
                (int)_numBackgroundAddrs, (int)getScanSharePct(), (int)_backgroundProbeCount, (int)_confirmProbeCount,
                (int)_mainBusCheckCount, (int)_busMultiplexers.getSlotIndices().size(), (int)_numSlotsKnown,
                (int)_numSlotsChanged, (int)_schedule.numSlotPositions(), (int)_muxRescanCount, (int)_muxCheckProbeCount);
    return buf;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Service called from I2C task
/// @param curTimeUs Current time in microseconds
/// @param maxFastTimeInLoopUs Maximum time allowed in this loop when fast scanning
/// @param maxSlowTimeInLoopUs Maximum time allowed in this loop when slow scanning
/// @return true if fast scanning in progress
bool BusScanner::taskService(uint64_t curTimeUs, uint64_t maxFastTimeInLoopUs, uint64_t maxSlowTimeInLoopUs)
{
    // Handle any scan request made from another task
    handleScanRequest();

    // Pick up the scan lists again if device type records, or what the identification hook handles, have changed
    if ((deviceTypeRecords.getGeneration() != _scanListsGeneration) || (_deviceIdentMgr.getHookClaimsEpoch() != _scanClaimsEpoch))
        loadScanLists();
    _schedule.setNumSlots(_busMultiplexers.getSlotIndices().size());
    if (Raft::isTimeout(millis(), _slotWeightsLastMs, SLOT_WEIGHTS_UPDATE_MS) || (_slotWeightsLastMs == 0))
    {
        _slotWeightsLastMs = millis();
        updateSlotWeights();
    }

    // Time of last scan
    uint32_t curTimeMs = curTimeUs / 1000;
    _scanLastMs = curTimeMs;
    uint64_t scanLoopStartTimeUs = micros();
    bool sweepCompleted = false;

    // The fast mode is limited in time
    if ((_scanMode == SCAN_MODE_SCAN_FAST) && Raft::isTimeout(curTimeMs, _fastScanStartMs, _fastScanMaxMs))
        setScanMode(SCAN_MODE_SCAN_SLOW);

    // The slow mode spends the scanning time earned (its share of the bus)
    if (_scanMode == SCAN_MODE_SCAN_SLOW)
    {
        updateScanCredit(scanLoopStartTimeUs);
        if (_scanCreditUs <= 0)
            return false;
        if ((uint64_t)_scanCreditUs < maxSlowTimeInLoopUs)
            maxSlowTimeInLoopUs = _scanCreditUs;
    }
    // Confirmation probes for elements that answered on an earlier pass (not this one, so the two probes are apart)
    uint32_t confirmsThisPass = _confirmQueue.size();

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
                // Get next address to scan: an element that has answered once (to bring it online), a background
                // address that is due, or the next from the priority lists
                if ((_scanMode != SCAN_MODE_MAIN_BUS_MUX_ONLY) && (confirmsThisPass > 0) && !_confirmQueue.empty())
                {
                    confirmsThisPass--;
                    addr = _confirmQueue.front().first;
                    slotNum = _confirmQueue.front().second;
                    _confirmQueue.erase(_confirmQueue.begin());
                    _confirmProbeCount++;
                    addrIsValid = true;
                }
                else if ((_scanMode != SCAN_MODE_MAIN_BUS_MUX_ONLY) && (_muxCheckMs > 0) &&
                            (_busMultiplexers.getMaxAddr() >= _busMultiplexers.getMinAddr()) &&
                            Raft::isTimeout(millis(), _muxCheckLastMs, _muxCheckMs))
                {
                    // Multiplexer supervision: one multiplexer address in turn, on the slot it is reached through,
                    // so a multiplexer that goes or comes (a hub unplugged) is noticed quickly
                    _muxCheckLastMs = millis();
                    uint32_t numMuxAddrs = _busMultiplexers.getMaxAddr() - _busMultiplexers.getMinAddr() + 1;
                    _muxCheckIdx = (_muxCheckIdx + 1) % numMuxAddrs;
                    addr = _busMultiplexers.getMinAddr() + _muxCheckIdx;
                    slotNum = _busMultiplexers.getMuxConnSlotNum(_muxCheckIdx);
                    _muxCheckProbeCount++;
                    addrIsValid = true;
                }
                else
                {
                    uint32_t bgPos = 0;
                    if ((_scanMode == SCAN_MODE_SCAN_SLOW) && _schedule.nextBackground(millis(), addr, bgPos))
                    {
                        slotNum = slotNumFromPos(bgPos);
                        _backgroundProbeCount++;
                        addrIsValid = true;
                    }
                    else
                    {
#ifdef DEBUG_FORCE_SIMPLE_LINEAR_SCANNING
                        addrIsValid = getAddrAndSlotToScanNext(addr, slotNum, sweepCompleted, true);
#else
                        addrIsValid = getAddrAndSlotToScanNext(addr, slotNum, sweepCompleted, false);
#endif
                    }
                }

                if (!addrIsValid)
                {
#ifdef DEBUG_NO_VALID_ADDRESS
                    LOG_I(MODULE_PREFIX, "taskService %s no valid address", getScanStateStr(_scanMode));
#endif
                    break;
                }

                // Only scan the main bus for addresses already known to be on the main bus - otherwise
                // they will incorrectly appear multiple times on slots because main bus devices
                // will respond on all slots
                if ((slotNum != 0) && _busElemTracker.isAddrFoundOnMainBus(addr))
                    continue;

                // Avoid scanning a bus multiplexer address on the wrong slot
                if (_busMultiplexers.isBusMultiplexer(addr))
                {
                    if (!_busMultiplexers.isSlotCorrect(addr, slotNum))
                        continue;
                }

                // Avoid scanning defined BusIOExpander addresses
                if (slotNum == 0)
                {
                    if (_busIOExpanders.isIOExpander(addr))
                        continue;
                }
                else
                {
                    uint32_t muxIdx = 0;
                    uint32_t slotIdx = 0;
                    if (!_busMultiplexers.getMuxAndSlotIdx(slotNum, muxIdx, slotIdx))
                        continue;
                    uint32_t muxAddr = _busMultiplexers.getAddrFromMuxIdx(muxIdx);
                    if (_busIOExpanders.isIOExpander(addr, muxAddr, slotIdx))
                        continue;
                }

                // An online device polled often enough isn't scanned: its polls show whether it is there. One polled
                // less often (e.g. a temperature sensor every 5 s) is scanned too, so its removal is seen at the scan
                // rate rather than after several failed polls. An offline device is scanned to see it come back
                if (_busStatusMgr.isAddrBeingPolled(BusI2CAddrAndSlot(addr, slotNum), PRESENCE_FROM_POLLS_MAX_INTERVAL_US))
                {
                    if (_busStatusMgr.isElemOnline(BusI2CAddrAndSlot(addr, slotNum)) == BUS_OPERATION_OK)
                        continue;
                }

                // Scan the address
                bool failedToEnableSlot = false;
                auto rslt = scanOneAddress(addr, slotNum, failedToEnableSlot);
                if (!failedToEnableSlot)
                {
                    if (_busMultiplexers.elemStateChange(addr, slotNum, rslt == RAFT_OK))
                    {
                        // A multiplexer has come or gone. During startup the multiplexers are scanned again from
                        // the start; after that only this multiplexer's slots are favoured (as recently changed) in a
                        // fast scan, rather than rescanning the whole bus
                        if ((_scanMode == SCAN_MODE_MAIN_BUS_MUX_ONLY) || (_scanMode == SCAN_MODE_MAIN_BUS))
                        {
                            setScanMode(SCAN_MODE_MAIN_BUS_MUX_ONLY);
                        }
                        else
                        {
                            // A multiplexer that has gone takes the devices on its slots with it (a device that isn't polled
                            // would otherwise stay online, as its slot is no longer scanned)
                            uint32_t muxIdx = addr - _busMultiplexers.getMinAddr();
                            const auto& slotIndices = _busMultiplexers.getSlotIndices();
                            for (uint32_t slotIdx = 0; slotIdx < BusMultiplexers::getSlotsPerMux(); slotIdx++)
                            {
                                uint32_t muxSlotIdx = muxIdx * BusMultiplexers::getSlotsPerMux() + slotIdx;
                                if (std::find(slotIndices.begin(), slotIndices.end(), muxSlotIdx) == slotIndices.end())
                                    _busStatusMgr.goingOfflineOnSlot(muxSlotIdx + 1);
                                _busStatusMgr.noteSlotChange(muxSlotIdx + 1);
                            }
                            _muxRescanCount++;
                            _slotWeightsLastMs = 0;
                            setScanMode(SCAN_MODE_SCAN_FAST);
                            LOG_I(MODULE_PREFIX, "taskService multiplexer 0x%02x changed - fast scan favouring its slots", addr);
                        }
                    }
                    // Only update bus element state for non-multiplexer devices
                    // Multiplexers are handled separately via elemStateChange()
                    if (!_busMultiplexers.isBusMultiplexer(addr))
                    {
                        updateBusElemState(addr, slotNum, rslt);
                    }
                }
                else if (rslt == RAFT_BUS_STUCK)
                {
                    // Update state for all elements to offline and indicate that the bus is failing
                    _busStatusMgr.informBusStuck();

#ifdef DEBUG_CANT_ENABLE_SLOT
                    LOG_I(MODULE_PREFIX, "taskService %s bus stuck attempting to set slotNum %d", 
                                getScanStateStr(_scanMode), slotNum);
#endif
                    break;
                }

                // Check sweepComplete or timeout (the slot stays selected for the next probe if it is on the same
                // slot - slots are deselected when the next probe is on another, and at the end of the pass)
                if (sweepCompleted || Raft::isTimeout(micros(), scanLoopStartTimeUs, _scanMode == SCAN_MODE_SCAN_FAST ? maxFastTimeInLoopUs : maxSlowTimeInLoopUs))
                    break;
            }
            break;
        }
    }

    // Deselect the slot left selected
    if (_scanSlotSelected != 0)
    {
        _busMultiplexers.disableAllSlots(false);
        _scanSlotSelected = 0;
    }

    // Spend the scanning time used
    if (_scanMode == SCAN_MODE_SCAN_SLOW)
        _scanCreditUs -= (int64_t)(micros() - scanLoopStartTimeUs);

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
    // Handle any scan request made from another task
    handleScanRequest();

    switch(_scanMode)
    {
        case SCAN_MODE_IDLE:
        case SCAN_MODE_MAIN_BUS_MUX_ONLY:
        case SCAN_MODE_MAIN_BUS:
        case SCAN_MODE_SCAN_FAST:
            return true;
        case SCAN_MODE_SCAN_SLOW:
            if (!_slowScanEnabled || !((_slowScanPeriodMs == 0) || (Raft::isTimeout(curTimeMs, _scanLastMs, _slowScanPeriodMs))))
                return false;
            updateScanCredit(micros());
            return _scanCreditUs > 0;
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
    if (scanMode == SCAN_MODE_SCAN_FAST)
    {
        _fastScanStartMs = millis();
        _schedule.restart();
    }
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
    // The startup phases scan every address linearly; the fast and slow modes follow the schedule
    if ((_scanMode == SCAN_MODE_MAIN_BUS_MUX_ONLY) || (_scanMode == SCAN_MODE_MAIN_BUS))
        ignorePriorities = true;
    if (!ignorePriorities)
    {
        uint32_t pos = 0;
        if (_schedule.next(addr, pos, sweepCompleted))
        {
            slotNum = slotNumFromPos(pos);
#ifdef DEBUG_SCAN_SEQUENCE
            LOG_I(MODULE_PREFIX, "getAddrAndSlotToScanNext %s addr %02x slotNum %d sweepCompleted %s",
                        getScanStateStr(_scanMode), addr, slotNum, sweepCompleted ? "Y" : "N");
#endif
            return true;
        }
    }

    // Linear scanning
    bool addressesOnSlotDone = false;
    addr = getAddrFromScanListIndex(_scanPriorityRecs[0], true, addressesOnSlotDone);
    slotNum = getSlotNumFromSlotIdx(_scanPriorityRecs[0], sweepCompleted, addressesOnSlotDone);
#ifdef DEBUG_SCAN_SEQUENCE
    LOG_I(MODULE_PREFIX, "getAddrAndSlotToScanNext %s linear addr %02x slotNum %d sweepCompleted %s",
                getScanStateStr(_scanMode), addr, slotNum, sweepCompleted ? "Y" : "N");
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

            // Every address in turn (priority order is BusScanSchedule's)
            (void)ignorePriorities;
            if (scanRec.scanListIndex >= (I2C_BUS_ADDRESS_MAX - I2C_BUS_ADDRESS_MIN + 1))
            {
                scanRec.scanListIndex = 0;
                indexWrap = true;
            }
            addr = I2C_BUS_ADDRESS_MIN + scanRec.scanListIndex;
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
/// @note This may be called from any task - the scanner state is owned by the I2C task so the request is
///       recorded in atomic flags which are consumed by the I2C task (see handleScanRequest)
void BusScanner::requestScan(bool enableSlowScan, bool requestFastScan)
{
    // Slow scan enable
    _slowScanEnabled = enableSlowScan;

    // Perform fast scans to detect online/offline status
    if (requestFastScan)
        _fastScanRequested = true;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Handle a scan request (called on the I2C task only)
void BusScanner::handleScanRequest()
{
    // Consume the request flag
    if (_fastScanRequested.exchange(false))
    {
        setScanMode(SCAN_MODE_SCAN_FAST);
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Scan one address
/// @param addr Address
/// @param slotNum SlotNum (1-based)
/// @param failedToEnableSlot (out) Failed to enable slot
/// @return Access result code
RaftRetCode BusScanner::scanOneAddress(uint32_t addr, uint32_t slotNum, bool& failedToEnableSlot)
{
    // Select the slot (only one slot is ever selected: enabling a slot only writes its own multiplexer, so the
    // slots of the one selected before are deselected first). The main bus is selected by deselecting every slot
    if ((slotNum != _scanSlotSelected) && (_scanSlotSelected != 0))
        _busMultiplexers.disableAllSlots(false);
    _scanSlotSelected = 0;
    auto rslt = _busMultiplexers.enableOneSlot(slotNum);
    if (rslt != RAFT_OK)
    {
        _busMultiplexers.disableAllSlots(false);
        failedToEnableSlot = true;
        return rslt;
    }
    _scanSlotSelected = slotNum;

    // Handle the scan
    failedToEnableSlot = false;
    _scanProbeCount++;
    BusRequestInfo reqRec(BUS_REQ_TYPE_SLOW_SCAN,
                addr,
                0, 0, 
                nullptr,
                0, 
                0,
                nullptr, 
                this);
    return _busReqSyncFn(&reqRec, nullptr);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Update bus element state
/// @param i2cAddr Address on the I2C bus (not including slot)
/// @param slot Slot
/// @param accessResult Access result code
void BusScanner::updateBusElemState(uint32_t i2cAddr, uint32_t slot, RaftRetCode accessResult)
{
    // An address barred for a time (e.g. a device pausing after a command) wasn't accessed, so this says nothing
    // about whether it is there
    if (accessResult == RAFT_BUS_BARRED)
        return;

    // A device that answers on a slot but isn't known there may be a main-bus device (one on the main bus answers
    // whichever slot is selected) that has come back while the slots were being scanned: check the main bus, with
    // the slots disabled, before recording it against the slot
    BusElemAddrType address(BusI2CAddrAndSlot(i2cAddr, slot));
    if ((slot != 0) && (accessResult == RAFT_OK) && (_busStatusMgr.isElemOnline(address) != BUS_OPERATION_OK))
    {
        bool failedToEnableSlot = false;
        _mainBusCheckCount++;
        if (scanOneAddress(i2cAddr, 0, failedToEnableSlot) == RAFT_OK)
        {
            updateBusElemState(i2cAddr, 0, RAFT_OK);
            return;
        }
    }

    // Update bus element state
    bool isOnline = false;
    bool isChange = _busStatusMgr.updateBusElemState(address, accessResult == RAFT_OK, isOnline);

    // An element that has answered once is probed again on the next pass, so it comes online without waiting for
    // the scan to come round again
    if ((accessResult == RAFT_OK) && !isOnline && (_confirmQueue.size() < CONFIRM_QUEUE_MAX) &&
                _busStatusMgr.isElemPendingOnline(address))
    {
        auto entry = std::make_pair((uint8_t)i2cAddr, (uint8_t)slot);
        if (std::find(_confirmQueue.begin(), _confirmQueue.end(), entry) == _confirmQueue.end())
            _confirmQueue.push_back(entry);
    }

    // Identification is done by the identification scheduler (DeviceIdentMgr::taskService), on each element's own
    // schedule - a newly online element at once

    //If this is a change to online then update the bus element tracker
    if (isOnline && isChange)
    {
        _busElemTracker.setElemFound(i2cAddr, slot);

#ifdef DEBUG_ELEM_STATUS_DEVICE_IDENT
        LOG_I(MODULE_PREFIX, "updateBusElemState addr&slot %04x (addr %02x slot %d) accessResult %d isOnline %d isChange %d", 
                address, i2cAddr, slot, accessResult, isOnline, isChange);
#endif
    }
}
