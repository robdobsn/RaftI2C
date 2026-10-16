/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Bus Status Manager
//
// Rob Dobson 2020-2024
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "BusStatusMgr.h"
#include "BusI2CAddrAndSlot.h"
#include "Logger.h"
#include "RaftUtils.h"
#include "DeviceIdentMgr.h"
#include <algorithm>
#include "DeviceTypeRecords.h"

#define WARN_ON_FAILED_TO_GET_SEMAPHORE

// #define DEBUG_BUS_ELEM_STATE_CHANGE
// #define DEBUG_BUS_ELEM_STATE_CHANGE_ADDR 0x76a
// #define DEBUG_UPDATE_BUS_ELEM_STATE_VERBOSE
// #define DEBUG_UPDATE_BUS_ELEM_STATE_ON_ADDRESS 0x76a
// #define DEBUG_LOOP_PROCESS_BUS_ELEM_STATUS_CHANGES
// #define DEBUG_BUS_OPERATION_STATUS
// #define DEBUG_NO_SCANNING
// #define DEBUG_ACCESS_BARRING_FOR_MS
// #define DEBUG_HANDLE_POLL_RESULT
// #define DEBUG_GET_POLL_RESULT
// #define DEBUG_POLL_RESULT_DETAIL
// #define DEBUG_OVERFLOW_MONITOR

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Constructor
/// @param raftBus raft bus
BusStatusMgr::BusStatusMgr(RaftBus& raftBus) :
    _raftBus(raftBus)
{
    // Bus element status change detection
    RaftMutex_init(_busElemStatusMutex);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Destructor
BusStatusMgr::~BusStatusMgr()
{
    RaftMutex_destroy(_busElemStatusMutex);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Setup
/// @param config configuration
void BusStatusMgr::setup(const RaftJsonIF& config)
{
    // Time base for identification timing
    _setupTimeMs = millis();

    // Get address to use for lockup detection
    _addrForLockupDetect = 0;
    _addrForLockupDetectValid = false;
    BusElemAddrType address = strtoul(config.getString("lockupDetect", "0xffffffff").c_str(), NULL, 0);
    if (address != 0xffffffff)
    {
        _addrForLockupDetect = address;
        _addrForLockupDetectValid = true;
    }
    _busOperationStatus = BUS_OPERATION_UNKNOWN;
    _busElemStatusChangeDetected = false;
    _addrStatus.clear();

    // Debug
    LOG_I(MODULE_PREFIX, "task lockupDetect addr %02x (valid %s)",
                _addrForLockupDetect, _addrForLockupDetectValid ? "Y" : "N");

}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Loop
/// @param hwIsOperatingOk hardware is operating ok
void BusStatusMgr::loop(bool hwIsOperatingOk)
{


#ifdef DEBUG_OVERFLOW_MONITOR
    // Periodic overflow logging (runs regardless of status changes)
    if (Raft::isTimeout(millis(), _debugOverflowLastLogMs, DEBUG_OVERFLOW_LOG_INTERVAL_MS))
    {
        _debugOverflowLastLogMs = millis();
        if (RaftMutex_lock(_busElemStatusMutex, 0))
        {
            for (auto& addrStatus : _addrStatus)
            {
                if (addrStatus.deviceStatus.pDataAggregator)
                {
                    uint32_t ringOvr = addrStatus.deviceStatus.pDataAggregator->debugGetAndClearOverflowCount();
                    LOG_I(MODULE_PREFIX, "overflowMon addr=0x%03x ringOvr=%d", addrStatus.address, ringOvr);
                }
            }
            RaftMutex_unlock(_busElemStatusMutex);
        }
    }
#endif

    // Check for any changes detected
    if (!_busElemStatusChangeDetected)
        return;

    // If no lockup detection addresses are used then rely on the bus's isOperatingOk() result
    BusOperationStatus prevBusOperationStatus = _busOperationStatus;
    BusOperationStatus newBusOperationStatus = prevBusOperationStatus;
    if (!_addrForLockupDetectValid)
    {
        newBusOperationStatus = hwIsOperatingOk ? BUS_OPERATION_OK : BUS_OPERATION_FAILING;
    }

    // Obtain semaphore controlling access to busElemChange list and flag
    // so we can update bus and element operation status
    // The harvest of changes is done in a single locked section and the change-detected flag is
    // cleared INSIDE that section (before any callbacks are made). Any change recorded by the bus
    // task after the harvest sets the flag again (under the same mutex) and is therefore picked up
    // on a subsequent loop rather than being lost
    std::vector<BusAddrStatus> statusChanges;
    if (RaftMutex_lock(_busElemStatusMutex, RAFT_MUTEX_WAIT_FOREVER))
    {
        // Clear the flag - also handles the case where the flag was set but no changes are found
        _busElemStatusChangeDetected = false;

        // Go through once and look for changes
        uint32_t numChanges = 0;
        for (auto& addrStatus : _addrStatus)
        {
            if (addrStatus.hasStatusChange())
                numChanges++;
        }

        // Make space for changes
        statusChanges.reserve(numChanges);

        // Harvest changes
        for (auto& addrStatus : _addrStatus)
        {
            if (addrStatus.hasStatusChange())
            {
                // Handle element change - create lightweight status for callback (with any identity change since
                // the last report). An identification attempt that changed nothing (e.g. a retry that still found
                // nothing) isn't reported
                BusAddrStatus statusChange = addrStatus.takeStatusChange();
                if (statusChange.isChange || statusChange.isNewlyIdentified || (statusChange.identEvent != BusIdentEvent::None))
                    statusChanges.push_back(statusChange);
            }

            // Check if this is the addrForLockupDetect
            if (_addrForLockupDetectValid &&
                        (addrStatus.address == _addrForLockupDetect))
            {
                newBusOperationStatus = (addrStatus.onlineState == DeviceOnlineState::ONLINE) ? BUS_OPERATION_OK : BUS_OPERATION_FAILING;
            }
        }

        // Unlock
        RaftMutex_unlock(_busElemStatusMutex);
    }

    // Check for status changes
    if (statusChanges.size() > 0)
    {
        // Perform elem state change callback (outside the lock)
        {
#ifdef DEBUG_LOOP_PROCESS_BUS_ELEM_STATUS_CHANGES
            for (auto& statusChange : statusChanges)
            {
                LOG_I(MODULE_PREFIX, "service busElemStatusChange addr %04x isChange %s status %s devTypeIdx %d",
                            statusChange.address,
                            statusChange.isChange ? "Y" : "N",
                            BusAddrStatus::getOnlineStateStr(statusChange.onlineState),
                            statusChange.deviceTypeIndex);
            }
#endif            
            _raftBus.callBusElemStatusCB(statusChanges);

            // Delete records that were pending deletion (OFFLINE devices) now that callback has been made
            // Only records whose pending deletion was reported in the callback above are deleted - a record
            // which the bus task marked as pending deletion while the callback was in progress has not yet
            // been reported and must be retained until a later pass reports it
            std::vector<BusElemAddrType> reportedDeletions;
            for (const auto& statusChange : statusChanges)
            {
                if (statusChange.onlineState == DeviceOnlineState::PENDING_DELETION)
                    reportedDeletions.push_back(statusChange.address);
            }
            if ((reportedDeletions.size() > 0) && RaftMutex_lock(_busElemStatusMutex, RAFT_MUTEX_WAIT_FOREVER))
            {
                // First add them to the pending deletion queue for publishing
                for (const auto& s : _addrStatus)
                {
                    if ((s.onlineState == DeviceOnlineState::PENDING_DELETION) &&
                        (std::find(reportedDeletions.begin(), reportedDeletions.end(), s.address) != reportedDeletions.end()))
                    {
                        // Add to pending deletion queue if not full
                        if (_pendingDeletionQueue.size() < PENDING_DELETION_QUEUE_MAX)
                        {
                            _pendingDeletionQueue.push_back({s.address, s.deviceStatus.deviceTypeIndex});
                        }
                        addForgottenAddr(s.address);
                    }
                }
                _addrStatus.erase(
                    std::remove_if(_addrStatus.begin(), _addrStatus.end(),
                        [&reportedDeletions](const BusAddrRecord& s) {
                            return (s.onlineState == DeviceOnlineState::PENDING_DELETION) &&
                                (std::find(reportedDeletions.begin(), reportedDeletions.end(), s.address) != reportedDeletions.end());
                        }),
                    _addrStatus.end());
                RaftMutex_unlock(_busElemStatusMutex);
            }
        }
    }

    // Bus operation change callback if required
    if (prevBusOperationStatus != newBusOperationStatus)
    {
#ifdef DEBUG_BUS_OPERATION_STATUS
        LOG_I(MODULE_PREFIX, "loop newOpStatus %s (was %s)", 
                    newBusOperationStatus ? "online" : "offline",
                    prevBusOperationStatus ? "online" : "offline");
#endif
        _busOperationStatus = newBusOperationStatus;
        _raftBus.callBusOperationStatusCB(newBusOperationStatus);
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Update bus element state
/// @param address address
/// @param elemResponding element is responding
/// @param isOnline (out) element is online
/// @return true if state has changed
bool BusStatusMgr::updateBusElemState(BusElemAddrType address, bool elemResponding, bool& isOnline)
{
#if defined(DEBUG_UPDATE_BUS_ELEM_STATE_VERBOSE) || defined(DEBUG_UPDATE_BUS_ELEM_STATE_ON_ADDRESS)
#if defined(DEBUG_UPDATE_BUS_ELEM_STATE_ON_ADDRESS)
    if (address == DEBUG_UPDATE_BUS_ELEM_STATE_ON_ADDRESS)
#endif
    {
        LOG_I(MODULE_PREFIX, "updateBusElemState address %04x isResponding %d", address, elemResponding);
    }
#endif

    // Check for new status change
    isOnline = false;
    bool isNewStatusChange = false;
    bool flagSpuriousRecord = false;

#ifdef DEBUG_BUS_ELEM_STATE_CHANGE
    // Debug
    BusAddrRecord prevStatus;
    BusAddrRecord newStatus;
#endif

    // Obtain semaphore controlling access to busElemChange list and flag
    if (RaftMutex_lock(_busElemStatusMutex, RAFT_MUTEX_WAIT_FOREVER))
    {

        // Find address record
        BusAddrRecord* pAddrStatus = findAddrStatusRecordEditable(address);

        // If not found and element is responding then add a new record
        if ((pAddrStatus == nullptr) && elemResponding && (_addrStatus.size() >= ADDR_STATUS_MAX) &&
                    !_addrStatusFullWarned)
        {
            _addrStatusFullWarned = true;
            LOG_W(MODULE_PREFIX, "updateBusElemState address table full (%d) - address %04x and any others found "
                        "will not be tracked", (int)ADDR_STATUS_MAX, address);
        }
        if ((pAddrStatus == nullptr) && elemResponding && (_addrStatus.size() < ADDR_STATUS_MAX))
        {
            // Add new record (isNewlyIdentified is false - set later by setBusElemDeviceStatus when identified)
            BusAddrRecord newAddrStatus(address, DeviceOnlineState::INITIAL, true, false);
            _addrStatus.push_back(newAddrStatus);
            pAddrStatus = &_addrStatus.back();

            // Identification timing - first seen
            IdentTiming* pIdentTiming = getIdentTiming(address, true);
            if (pIdentTiming && (pIdentTiming->seenMs == IdentTiming::NOT_REACHED))
                pIdentTiming->seenMs = millis() - _setupTimeMs;
        }

        // Check if we found a record
        if (pAddrStatus)
        {
#ifdef DEBUG_BUS_ELEM_STATE_CHANGE
            // Debug
            prevStatus = *pAddrStatus;
#endif

            // Handle element response
            isNewStatusChange = pAddrStatus->handleResponding(elemResponding, flagSpuriousRecord);
            isOnline = pAddrStatus->onlineState == DeviceOnlineState::ONLINE;

            // A newly online element is identified at once
            if (isNewStatusChange && isOnline)
            {
                pAddrStatus->identState = BusIdentState::Pending;
                pAddrStatus->identAttempts = 0;
                pAddrStatus->nextIdentMs = millis();
                pAddrStatus->reverifyRequested = false;
            }

            // Identification timing - first time online
            if (isNewStatusChange && isOnline)
            {
                IdentTiming* pIdentTiming = getIdentTiming(address, false);
                if (pIdentTiming && (pIdentTiming->onlineMs == IdentTiming::NOT_REACHED))
                    pIdentTiming->onlineMs = millis() - _setupTimeMs;
            }

#ifdef DEBUG_BUS_ELEM_STATE_CHANGE
            // Debug
            newStatus = *pAddrStatus;
#endif
        }

        // Update status change
        if (isNewStatusChange)
        {
            _busElemStatusChangeDetected = true;
            _lastBusElemOnlineStatusUpdateTimeMs = millis();
            _lastPollOrStatusUpdateTimeMs = millis();

            // Mark for deletion if device has gone offline (will be deleted after callback)
            if (pAddrStatus && pAddrStatus->onlineState == DeviceOnlineState::OFFLINE)
            {
                pAddrStatus->onlineState = DeviceOnlineState::PENDING_DELETION;
            }
        }

        // Check for spurious record detected
        if (flagSpuriousRecord)
        {
            // Remove the record
            addForgottenAddr(address);
            _addrStatus.erase(std::remove_if(_addrStatus.begin(), _addrStatus.end(), 
                [address](BusAddrRecord& addrStatus) { return addrStatus.address == address; }), 
                _addrStatus.end());
        }

        // Return semaphore
        RaftMutex_unlock(_busElemStatusMutex);

#if defined(DEBUG_BUS_ELEM_STATE_CHANGE)
#ifdef DEBUG_BUS_ELEM_STATE_CHANGE_ADDR
        if (address == DEBUG_BUS_ELEM_STATE_CHANGE_ADDR)
#endif
        if (isNewStatusChange)
        {
            LOG_I(MODULE_PREFIX, "updateBusElemState address %04x count %d(prev %d) state %s(prev %s) isChange %d(was %d) isResponding %d",
                        newStatus.address,
                        newStatus.count, prevStatus.count, 
                        BusAddrStatus::getOnlineStateStr(newStatus.onlineState),
                        BusAddrStatus::getOnlineStateStr(prevStatus.onlineState),
                        newStatus.isChange, prevStatus.isChange, 
                        elemResponding);
        }
#endif
    }
    else
    {
#ifdef WARN_ON_FAILED_TO_GET_SEMAPHORE
        LOG_E(MODULE_PREFIX, "updateBusElemState address %04x failed to obtain semaphore", 
                            address);
#endif
    }

    return isNewStatusChange;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Check if an element is online
/// @param address address
/// @return UNKNOWN if not known, OK if online, FAILING if offline
BusOperationStatus BusStatusMgr::isElemOnline(BusElemAddrType address) const
{
#ifdef DEBUG_NO_SCANNING
    return BUS_OPERATION_OK;
#endif

    // Obtain semaphore controlling access to busElemChange list and flag
    BusOperationStatus onlineStatus = BUS_OPERATION_UNKNOWN;
    if (RaftMutex_lock(_busElemStatusMutex, RAFT_MUTEX_WAIT_FOREVER))
    {
        // Find address record
        const BusAddrRecord* pAddrStatus = findAddrStatusRecord(address);
        if (!pAddrStatus)
            onlineStatus = BUS_OPERATION_UNKNOWN;
        else
            onlineStatus = (pAddrStatus->onlineState == DeviceOnlineState::ONLINE) ? BUS_OPERATION_OK : BUS_OPERATION_FAILING;
        // Return semaphore
        RaftMutex_unlock(_busElemStatusMutex);
    }
    return onlineStatus;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get count of address status records
/// @return count
uint32_t BusStatusMgr::getAddrStatusCount() const
{
    // Obtain semaphore
    if (!RaftMutex_lock(_busElemStatusMutex, RAFT_MUTEX_WAIT_FOREVER))
        return 0;

    // Get count
    uint32_t count = _addrStatus.size();

    // Return semaphore
    RaftMutex_unlock(_busElemStatusMutex);
    return count;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Set bus element access barring
/// @param timeNowMs current time in ms
/// @param address address
/// @param barAccessAfterSendMs time to bar access after send in ms
void BusStatusMgr::barElemAccessSet(uint32_t timeNowMs, BusElemAddrType address, uint32_t barAccessAfterSendMs)
{
    // Obtain semaphore
    if (!RaftMutex_lock(_busElemStatusMutex, RAFT_MUTEX_WAIT_FOREVER))
        return;

    // Find address record
    BusAddrRecord* pAddrStatus = findAddrStatusRecordEditable(address);
    if (pAddrStatus)
    {
        // Set access barring
        pAddrStatus->barStartMs = timeNowMs;
        pAddrStatus->barDurationMs = barAccessAfterSendMs;
    }

    // Return semaphore
    RaftMutex_unlock(_busElemStatusMutex);

#ifdef DEBUG_ACCESS_BARRING_FOR_MS
    LOG_W(MODULE_PREFIX, "barElemAccessSet %s barring bus access for address %04x for %dms",
                    pAddrStatus ? "OK" : "FAIL", 
                    address, barAccessAfterSendMs);
#endif
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get bus element access barring
/// @brief timeNowMs current time in ms
/// @param address address
/// @return true if access is barred
bool BusStatusMgr::barElemAccessGet(uint32_t timeNowMs, BusElemAddrType address)
{
    // Obtain semaphore
    if (!RaftMutex_lock(_busElemStatusMutex, RAFT_MUTEX_WAIT_FOREVER))
        return false;

    // Find address record
    bool accessBarred = false;
#ifdef DEBUG_ACCESS_BARRING_FOR_MS
    bool barReleased = false;
#endif
    BusAddrRecord* pAddrStatus = findAddrStatusRecordEditable(address);

    // Check if access is barred
    if (pAddrStatus && pAddrStatus->barDurationMs != 0)
    {
        // Barred until the time has elapsed (this was inverted: an access during the bar released it, and the first
        // access after it was barred until another bar was set)
        if ((uint32_t)(timeNowMs - pAddrStatus->barStartMs) < pAddrStatus->barDurationMs)
        {
            accessBarred = true;
        }
        else
        {
            pAddrStatus->barDurationMs = 0;
#ifdef DEBUG_ACCESS_BARRING_FOR_MS
            barReleased = true;
#endif
        }
    }

    // Return semaphore
    RaftMutex_unlock(_busElemStatusMutex);

#ifdef DEBUG_ACCESS_BARRING_FOR_MS
    if (accessBarred)
    {
        LOG_W(MODULE_PREFIX, "barElemAccessGet access barred for address %04x for %ldms", 
                        address, pAddrStatus->barDurationMs);
    }
    if (barReleased)
    {
        LOG_W(MODULE_PREFIX, "barElemAccessGet access bar released for address %04x", address);
    }
    
#endif

    return accessBarred;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Set bus element device status (which includes device type and can be empty) for an address
/// @param address address
/// @param deviceStatus device status
void BusStatusMgr::setBusElemDeviceStatus(BusElemAddrType address, const DeviceStatus& deviceStatus)
{
    // Obtain semaphore
    if (!RaftMutex_lock(_busElemStatusMutex, RAFT_MUTEX_WAIT_FOREVER))
        return;

    // Find address record
    BusAddrRecord* pAddrStatus = findAddrStatusRecordEditable(address);
    if (pAddrStatus)
    {
        // Set device status (includes device type index)
        pAddrStatus->deviceStatus = deviceStatus;

        // Check if device type index is valid - if so this is a new identification
        pAddrStatus->identChangePending = true;
        _busElemStatusChangeDetected = true;
        if (deviceStatus.getDeviceTypeIndex() != DEVICE_TYPE_INDEX_INVALID)
        {
            pAddrStatus->isNewlyIdentified = true;
            pAddrStatus->identState = BusIdentState::Identified;
            pAddrStatus->identAttempts = 0;

            // Identification timing - first identification
            IdentTiming* pIdentTiming = getIdentTiming(address, false);
            if (pIdentTiming && (pIdentTiming->identMs == IdentTiming::NOT_REACHED))
            {
                pIdentTiming->identMs = millis() - _setupTimeMs;
                pIdentTiming->deviceTypeIndex = deviceStatus.getDeviceTypeIndex();
            }
        }
    }

    // Return semaphore
    RaftMutex_unlock(_busElemStatusMutex);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get device type index by address
/// @param address address
/// @return device type index
DeviceTypeIndexType BusStatusMgr::getDeviceTypeIndexByAddr(BusElemAddrType address) const
{
    // Obtain semaphore
    if (!RaftMutex_lock(_busElemStatusMutex, RAFT_MUTEX_WAIT_FOREVER))
        return DEVICE_TYPE_INDEX_INVALID;
    // Find address record
    DeviceTypeIndexType deviceTypeIndex = DEVICE_TYPE_INDEX_INVALID;
    const BusAddrRecord* pAddrStatus = findAddrStatusRecord(address);
    if (pAddrStatus)
    {
        // Get device type index
        deviceTypeIndex = pAddrStatus->deviceStatus.getDeviceTypeIndex();
    }

    // Return semaphore
    RaftMutex_unlock(_busElemStatusMutex);
    return deviceTypeIndex;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Check if an address is being polled
/// @param address address
/// @return true if the address is being polled
bool BusStatusMgr::isAddrBeingPolled(BusElemAddrType address) const
{
    // Obtain semaphore
    if (!RaftMutex_lock(_busElemStatusMutex, RAFT_MUTEX_WAIT_FOREVER))
        return false;

    // Check if the address is being polled
    bool isBeingPolled = false;
    const BusAddrRecord* pAddrStatus = findAddrStatusRecord(address);
    if (pAddrStatus)
    {
        // Check if the address is being polled
        isBeingPolled = pAddrStatus->deviceStatus.getNumPollRequests() > 0;
    }

    // Return semaphore
    RaftMutex_unlock(_busElemStatusMutex);
    return isBeingPolled;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Inform that the devices on a slot are going offline (e.g. the slot is being power cycled)
/// @param slotNum slot number (0 for the main bus)
void BusStatusMgr::goingOfflineOnSlot(uint32_t slotNum)
{
    // Obtain semaphore
    if (!RaftMutex_lock(_busElemStatusMutex, RAFT_MUTEX_WAIT_FOREVER))
        return;

    // Records hold slot-encoded addresses, so match on the slot
    for (BusAddrRecord& addrStatus : _addrStatus)
    {
        if (BusI2CAddrAndSlot(addrStatus.address).getSlotNum() == slotNum)
            markGoneOffline(addrStatus);
    }
    eraseSpuriousRecords();

    // Return semaphore
    RaftMutex_unlock(_busElemStatusMutex);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Inform that the bus is stuck
void BusStatusMgr::informBusStuck()
{
    // Get semaphore
    if (!RaftMutex_lock(_busElemStatusMutex, RAFT_MUTEX_WAIT_FOREVER))
        return;

    // Go through all devices and set status to offline
    for (BusAddrRecord& addrStatus : _addrStatus)
        markGoneOffline(addrStatus);
    eraseSpuriousRecords();

    // Return semaphore
    RaftMutex_unlock(_busElemStatusMutex);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief A device has gone (e.g. its slot lost power). An online device is reported as going and then
///        deleted (as when it stops responding); a device not yet online is marked for eraseSpuriousRecords()
/// @param rec record
/// @note assumes the lock is held
void BusStatusMgr::markGoneOffline(BusAddrRecord& rec)
{
    switch (rec.onlineState)
    {
        case DeviceOnlineState::ONLINE:
        case DeviceOnlineState::OFFLINE:
            rec.onlineState = DeviceOnlineState::PENDING_DELETION;
            rec.isChange = true;
            rec.count = 0;
            _busElemStatusChangeDetected = true;
            _lastBusElemOnlineStatusUpdateTimeMs = millis();
            _lastPollOrStatusUpdateTimeMs = millis();
            break;
        case DeviceOnlineState::INITIAL:
            // Never reported, so nothing to tell anyone - erased by eraseSpuriousRecords()
            rec.onlineState = DeviceOnlineState::OFFLINE;
            rec.isChange = false;
            break;
        case DeviceOnlineState::PENDING_DELETION:
            break;
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Erase records that went offline without ever being reported online (see markGoneOffline())
/// @note assumes the lock is held
void BusStatusMgr::eraseSpuriousRecords()
{
    for (const BusAddrRecord& rec : _addrStatus)
    {
        if ((rec.onlineState == DeviceOnlineState::OFFLINE) && !rec.isChange)
            addForgottenAddr(rec.address);
    }
    _addrStatus.erase(std::remove_if(_addrStatus.begin(), _addrStatus.end(),
            [](const BusAddrRecord& rec) { return (rec.onlineState == DeviceOnlineState::OFFLINE) && !rec.isChange; }),
        _addrStatus.end());
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Add an address to the addresses whose bus task state should be forgotten
/// @note assumes the lock is held
void BusStatusMgr::addForgottenAddr(BusElemAddrType address)
{
    _forgottenAddrsPending = true;
    if (std::find(_forgottenAddrs.begin(), _forgottenAddrs.end(), address) != _forgottenAddrs.end())
        return;
    if (_forgottenAddrs.size() >= FORGOTTEN_ADDRS_MAX)
    {
        _forgottenAddrsOverflow = true;
        return;
    }
    _forgottenAddrs.push_back(address);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get addresses whose per-address state on the bus task should be forgotten since the last call
/// @param addrs (out) addresses
/// @return false if some were missed (more than the list holds) - then forget the state of every address
bool BusStatusMgr::getForgottenAddresses(std::vector<BusElemAddrType>& addrs)
{
    addrs.clear();
    if (!_forgottenAddrsPending)
        return true;
    if (!RaftMutex_lock(_busElemStatusMutex, RAFT_MUTEX_WAIT_FOREVER))
        return true;
    addrs.swap(_forgottenAddrs);
    bool allReported = !_forgottenAddrsOverflow;
    _forgottenAddrsOverflow = false;
    _forgottenAddrsPending = false;
    RaftMutex_unlock(_busElemStatusMutex);
    return allReported;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Get pending ident poll requests for a single device
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool BusStatusMgr::getPendingIdentPoll(uint64_t timeNowUs, DevicePollingInfo& pollInfo)
{
    // Obtain semaphore
    if (!RaftMutex_lock(_busElemStatusMutex, RAFT_MUTEX_WAIT_FOREVER))
        return false;

    // Find the due device with the earliest due time
    bool pollPending = false;
    int32_t recIdx = _pollScheduler.findEarliestDue(_addrStatus, getRecordDeviceStatusIfOnline, timeNowUs);
    if (recIdx != DevicePollScheduler::NONE_DUE)
    {
        pollPending = _addrStatus[recIdx].deviceStatus.getPendingIdentPollInfo(timeNowUs, pollInfo);
        _pollScheduler.markPolled(recIdx);
    }

    // Return semaphore
    RaftMutex_unlock(_busElemStatusMutex);
    return pollPending;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get the earliest time any device poll (or poll step) is due
/// @param timeNowUs current time in us
/// @param dueUs (out) earliest due time (may be in the past)
/// @return false if no device is polled
bool BusStatusMgr::getNextPollDueUs(uint64_t timeNowUs, uint64_t& dueUs)
{
    // Obtain semaphore
    if (!RaftMutex_lock(_busElemStatusMutex, RAFT_MUTEX_WAIT_FOREVER))
        return false;
    bool anyPolled = DevicePollScheduler::getEarliestDueUs(_addrStatus, getRecordDeviceStatusIfOnline, timeNowUs, dueUs);
    RaftMutex_unlock(_busElemStatusMutex);
    return anyPolled;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Store poll results
/// @param nextReqIdx index of next request to store (0 = completed poll, 1+ = partial poll results)
/// @param timeNowUs time in us (passed in to aid testing)
/// @param address address
/// @param pollResultData poll result data
/// @param pPollInfo pointer to device polling info (maybe nullptr)
/// @param pauseAfterSendMs pause after send in ms
/// @return true if result stored
bool BusStatusMgr::handlePollResult(uint32_t nextReqIdx, uint64_t timeNowUs, BusElemAddrType address, 
                        const std::vector<uint8_t>& pollResultData, const DevicePollingInfo* pPollInfo,
                        uint32_t pauseAfterSendMs)
{
    // Callback
    RaftDeviceDataChangeCB pCallback = nullptr;
    const void* pCallbackInfo = nullptr;
    uint16_t deviceTypeIndex = 0;
    uint32_t timeNowMs = timeNowUs / 1000;

    // Obtain semaphore
    if (!RaftMutex_lock(_busElemStatusMutex, RAFT_MUTEX_WAIT_FOREVER))
        return false;

    // Find address record
    BusAddrRecord* pAddrStatus = findAddrStatusRecordEditable(address);
    bool putResult = false;
    if (pAddrStatus)
    {
        // Add result to aggregator
        putResult = pAddrStatus->deviceStatus.storePollResults(nextReqIdx, timeNowUs, pollResultData, pPollInfo, pauseAfterSendMs);

        // Check if this is a completed poll
        if (nextReqIdx == 0)
        {
            // A poll that succeeds after one that failed: the failure count is reset (the device goes offline after
            // consecutive failures), and the device is re-verified - it may have reset, or been replaced
            if (pAddrStatus->count < 0)
            {
                pAddrStatus->count = 0;
                if (pAddrStatus->identState == BusIdentState::Identified)
                {
                    pAddrStatus->reverifyRequested = true;
                    pAddrStatus->nextIdentMs = timeNowMs;
                }
            }

            // Check for callback
            pCallback = pAddrStatus->getDataChangeCB();
            if (pCallback)
            {
                // Check min time between reports
                if (Raft::isTimeout(timeNowMs, pAddrStatus->lastDataChangeReportTimeMs, pAddrStatus->minTimeBetweenReportsMs))
                {
                    // Get device type index and callback info
                    deviceTypeIndex = pAddrStatus->deviceStatus.getDeviceTypeIndex();
                    pCallbackInfo = pAddrStatus->getCallbackInfo();
                    pAddrStatus->lastDataChangeReportTimeMs = timeNowMs;
                }
                else
                {
                    pCallback = nullptr;
                }
            }

            // Store time of last status update
            _lastIdentPollUpdateTimeMs = timeNowMs;
            _lastPollOrStatusUpdateTimeMs = timeNowMs;
        }
    }

    // Flag (while still holding the mutex) that a data change callback is in progress so that
    // unregisterForDeviceData can wait for it to complete before the subscriber is destroyed
    if (pCallback)
    {
        _dataChangeCBInProgressInfo = pCallbackInfo;
        _dataChangeCBInProgressTask = (void*)xTaskGetCurrentTaskHandle();
        _dataChangeCBCallCount++;
        _dataChangeCBInProgress = true;
    }

    // Return semaphore
    RaftMutex_unlock(_busElemStatusMutex);

    // Check if a callback is required
    if (pCallback)
    {
        // Call the callback
        pCallback(deviceTypeIndex, pollResultData, pCallbackInfo);

        // Callback no longer in progress
        _dataChangeCBInProgress = false;
    }

#ifdef DEBUG_HANDLE_POLL_RESULT
    LOG_I(MODULE_PREFIX, "handlePollResult address %04x pAddrStatus %p deviceTypeIdx %d callbackValid %s", 
                address, pAddrStatus, deviceTypeIndex,
                pCallback ? "Y" : "N");
#endif

    return putResult;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get latest timestamp of change to device info (online/offline, new data, etc)
/// @param includeElemOnlineStatusChanges include changes in online status of elements
/// @param includeDeviceDataUpdates include new data updates
/// @return latest update time in ms
uint64_t BusStatusMgr::getDeviceInfoTimestampMs(bool includeElemOnlineStatusChanges, bool includeDeviceDataUpdates) const
{
    // Check which time to return
    if (!includeElemOnlineStatusChanges)
        return _lastIdentPollUpdateTimeMs;
    if (!includeDeviceDataUpdates)
        return _lastBusElemOnlineStatusUpdateTimeMs;
    return _lastPollOrStatusUpdateTimeMs;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Return addresses of devices attached to the bus
/// @param addresses - vector to store the addresses of devices
/// @param onlyAddressesWithIdentPollResponses - true to only return addresses with ident poll responses
/// @return true if there are any ident poll responses available
bool BusStatusMgr::getBusElemAddresses(std::vector<BusElemAddrType>& addresses, bool onlyAddressesWithIdentPollResponses) const
{
    // Obtain semaphore
    if (!RaftMutex_lock(_busElemStatusMutex, RAFT_MUTEX_WAIT_FOREVER))
        return false;

    // Iterate address status records
    for (const BusAddrRecord& addrStatus : _addrStatus)
    {
        bool includeAddr = !onlyAddressesWithIdentPollResponses || addrStatus.deviceStatus.getPollRespCount() > 0;
        if (includeAddr)
        {
            // Add address to list
            addresses.push_back(addrStatus.address);
        }
    }

    // Return semaphore
    RaftMutex_unlock(_busElemStatusMutex);
    return addresses.size() > 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////    
/// @brief Get bus element poll responses for a specific address
/// @param address - address of device to get responses for
/// @param onlineState - (out) device online state
/// @param deviceTypeIndex - (out) device type index
/// @param devicePollResponseData - (out) vector to store the device poll response data
/// @param responseSize - (out) size of the response data
/// @param maxResponsesToReturn - maximum number of responses to return (0 for no limit)
/// @return number of responses returned
uint32_t BusStatusMgr::getBusElemPollResponses(BusElemAddrType address, DeviceOnlineState& onlineState, uint16_t& deviceTypeIndex, 
            std::vector<uint8_t>& devicePollResponseData, 
            uint32_t& responseSize, uint32_t maxResponsesToReturn)
{
    // Obtain semaphore
    if (!RaftMutex_lock(_busElemStatusMutex, RAFT_MUTEX_WAIT_FOREVER))
        return 0;

    // Find address record
    uint32_t numResponses = 0;
    BusAddrRecord* pAddrStatus = findAddrStatusRecordEditable(address);
    if (pAddrStatus)
    {
        // Device online state
        onlineState = pAddrStatus->onlineState;
        
        // Device type index
        deviceTypeIndex = pAddrStatus->deviceStatus.getDeviceTypeIndex();

        // Get results from aggregator
        numResponses = pAddrStatus->deviceStatus.getPollResponses(devicePollResponseData, responseSize, maxResponsesToReturn);
        
#ifdef DEBUG_POLL_RESULT_DETAIL
        // Log retrieved poll data for debugging (start of the data, whatever the device)
        if (numResponses > 0)
        {
            char hexBuf[64] = {};
            size_t hexLen = std::min(devicePollResponseData.size(), (size_t)20);
            for (size_t i = 0; i < hexLen; i++)
                snprintf(hexBuf + i*3, sizeof(hexBuf) - i*3, "%02x ", devicePollResponseData[i]);
            LOG_I(MODULE_PREFIX, "getPoll addr=0x%03x n=%d respSize=%d hex=[%s]",
                  address, numResponses, responseSize, hexBuf);
        }
#endif
    }

    // Return semaphore
    RaftMutex_unlock(_busElemStatusMutex);

#ifdef DEBUG_GET_POLL_RESULT
    LOG_I(MODULE_PREFIX, "getBusElemPollResponses address %04x onlineState %s deviceTypeIndex %d numResponses %d responseSize %d",
                address, BusAddrStatus::getOnlineStateStr(onlineState), deviceTypeIndex, numResponses, responseSize);
#endif

    return numResponses;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get bus element poll responses with per-sample actual lengths
uint32_t BusStatusMgr::getBusElemPollResponsesWithLengths(BusElemAddrType address,
            DeviceOnlineState& onlineState, uint16_t& deviceTypeIndex,
            std::vector<uint8_t>& devicePollResponseData,
            std::vector<uint16_t>& sampleLengths,
            uint32_t maxResponsesToReturn)
{
    if (!RaftMutex_lock(_busElemStatusMutex, RAFT_MUTEX_WAIT_FOREVER))
        return 0;

    uint32_t numResponses = 0;
    BusAddrRecord* pAddrStatus = findAddrStatusRecordEditable(address);
    if (pAddrStatus)
    {
        onlineState = pAddrStatus->onlineState;
        deviceTypeIndex = pAddrStatus->deviceStatus.getDeviceTypeIndex();
        numResponses = pAddrStatus->deviceStatus.getPollResponsesWithLengths(
            devicePollResponseData, sampleLengths, maxResponsesToReturn);
    }

    RaftMutex_unlock(_busElemStatusMutex);
    return numResponses;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get latest poll response for a bus element (non-destructive peek)
bool BusStatusMgr::getBusElemLatestPollResponse(BusElemAddrType address,
            DeviceOnlineState& onlineState, uint16_t& deviceTypeIndex,
            uint64_t& dataTimeUs, std::vector<uint8_t>& data, bool peek)
{
    if (!RaftMutex_lock(_busElemStatusMutex, RAFT_MUTEX_WAIT_FOREVER))
        return false;

    bool result = false;
    BusAddrRecord* pAddrStatus = findAddrStatusRecordEditable(address);
    if (pAddrStatus)
    {
        onlineState = pAddrStatus->onlineState;
        deviceTypeIndex = pAddrStatus->deviceStatus.getDeviceTypeIndex();
        result = peek ? pAddrStatus->deviceStatus.peekLatestPollResponse(dataTimeUs, data) :
                        pAddrStatus->deviceStatus.getLatestPollResponse(dataTimeUs, data);
    }

    RaftMutex_unlock(_busElemStatusMutex);
    return result;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get and increment per-device publish sequence counter
uint8_t BusStatusMgr::getAndIncrementDeviceSeqCounter(BusElemAddrType address)
{
    if (!RaftMutex_lock(_busElemStatusMutex, RAFT_MUTEX_WAIT_FOREVER))
        return 0;

    uint8_t seqNum = 0;
    BusAddrRecord* pAddrStatus = findAddrStatusRecordEditable(address);
    if (pAddrStatus)
        seqNum = pAddrStatus->deviceStatus.getAndIncrementSeqCounter();

    RaftMutex_unlock(_busElemStatusMutex);
    return seqNum;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get pending deletions and clear the queue
/// @param deletions (out) vector to receive pending deletions
void BusStatusMgr::getPendingDeletions(std::vector<DeletionNotice>& deletions)
{
    deletions.clear();
    if (RaftMutex_lock(_busElemStatusMutex, RAFT_MUTEX_WAIT_FOREVER))
    {
        deletions = std::move(_pendingDeletionQueue);
        _pendingDeletionQueue.clear();
        RaftMutex_unlock(_busElemStatusMutex);
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get debug JSON
/// @return JSON string
String BusStatusMgr::getDebugJSON(bool includeBraces) const
{
    // Obtain semaphore
    if (!RaftMutex_lock(_busElemStatusMutex, RAFT_MUTEX_WAIT_FOREVER))
        return "{}";

    // Create JSON
    String jsonStr;
    for (const BusAddrRecord& addrStatus : _addrStatus)
    {
        if (jsonStr.length() > 0)
            jsonStr += ",";
        jsonStr += addrStatus.getJson();
    }

    // Return semaphore
    RaftMutex_unlock(_busElemStatusMutex);
    jsonStr = "\"o\":" + String(_busOperationStatus ? 1 : 0) + ",\"d\":[" + jsonStr + "]";
    if (includeBraces)
        jsonStr = "{" + jsonStr + "}";
    return jsonStr;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Set device polling interval for a specific address
/// @param address address
/// @param pollIntervalUs poll interval in microseconds
/// @return true if address found and updated
bool BusStatusMgr::setDevicePollIntervalUs(BusElemAddrType address, uint32_t pollIntervalUs)
{
    // Obtain semaphore
    if (!RaftMutex_lock(_busElemStatusMutex, RAFT_MUTEX_WAIT_FOREVER))
        return false;

    bool updated = false;
    BusAddrRecord* pAddrStatus = findAddrStatusRecordEditable(address);
    if (pAddrStatus)
    {
        pAddrStatus->deviceStatus.setPollIntervalUs(pollIntervalUs, micros());
        updated = true;
    }

    RaftMutex_unlock(_busElemStatusMutex);
    return updated;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get device polling interval for a specific address
/// @param address address
/// @return poll interval in microseconds (0 if address not found or not being polled)
uint64_t BusStatusMgr::getDevicePollIntervalUs(BusElemAddrType address) const
{
    // Obtain semaphore
    if (!RaftMutex_lock(_busElemStatusMutex, RAFT_MUTEX_WAIT_FOREVER))
        return 0;

    uint64_t pollIntervalUs = 0;
    const BusAddrRecord* pAddrStatus = findAddrStatusRecord(address);
    if (pAddrStatus)
    {
        pollIntervalUs = pAddrStatus->deviceStatus.deviceIdentPolling.pollIntervalUs;
    }

    RaftMutex_unlock(_busElemStatusMutex);
    return pollIntervalUs;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Set number of poll result samples to store for a specific address
/// @param address address
/// @param numSamples number of samples to store
/// @return true if address found and updated
bool BusStatusMgr::setDeviceNumSamples(BusElemAddrType address, uint32_t numSamples)
{
    // Obtain semaphore
    if (!RaftMutex_lock(_busElemStatusMutex, RAFT_MUTEX_WAIT_FOREVER))
        return false;

    bool updated = false;
    BusAddrRecord* pAddrRecord = findAddrStatusRecordEditable(address);
    if (pAddrRecord)
    {
        // Update the stored config value
        pAddrRecord->deviceStatus.deviceIdentPolling.numPollResultsToStore = numSamples;
        // Resize the live aggregator
        if (pAddrRecord->deviceStatus.pDataAggregator)
            updated = pAddrRecord->deviceStatus.pDataAggregator->resize(numSamples);
    }

    RaftMutex_unlock(_busElemStatusMutex);
    return updated;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get number of poll result samples stored for a specific address
/// @param address address
/// @return number of samples (0 if address not found)
uint32_t BusStatusMgr::getDeviceNumSamples(BusElemAddrType address) const
{
    // Obtain semaphore
    if (!RaftMutex_lock(_busElemStatusMutex, RAFT_MUTEX_WAIT_FOREVER))
        return 0;

    uint32_t numSamples = 0;
    const BusAddrRecord* pAddrRecord = findAddrStatusRecord(address);
    if (pAddrRecord)
    {
        numSamples = pAddrRecord->deviceStatus.deviceIdentPolling.numPollResultsToStore;
    }

    RaftMutex_unlock(_busElemStatusMutex);
    return numSamples;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Set bus frequency override for polling a specific address
/// @param address address
/// @param busHz bus frequency in Hz (0 = use bus default)
/// @return true if address found and updated
bool BusStatusMgr::setDevicePollBusHz(BusElemAddrType address, uint32_t busHz)
{
    // Obtain semaphore
    if (!RaftMutex_lock(_busElemStatusMutex, RAFT_MUTEX_WAIT_FOREVER))
        return false;

    bool updated = false;
    BusAddrRecord* pAddrStatus = findAddrStatusRecordEditable(address);
    if (pAddrStatus)
    {
        pAddrStatus->deviceStatus.deviceIdentPolling.pollBusHz = busHz;
        updated = true;
    }

    RaftMutex_unlock(_busElemStatusMutex);
    return updated;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get bus frequency override for polling a specific address
/// @param address address
/// @return bus frequency in Hz (0 = use bus default)
uint32_t BusStatusMgr::getDevicePollBusHz(BusElemAddrType address) const
{
    // Obtain semaphore
    if (!RaftMutex_lock(_busElemStatusMutex, RAFT_MUTEX_WAIT_FOREVER))
        return 0;

    uint32_t busHz = 0;
    const BusAddrRecord* pAddrRecord = findAddrStatusRecord(address);
    if (pAddrRecord)
    {
        busHz = pAddrRecord->deviceStatus.deviceIdentPolling.pollBusHz;
    }

    RaftMutex_unlock(_busElemStatusMutex);
    return busHz;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Set bus frequency slot mask for polling a specific address
/// @param address address
/// @param slotMask bitmask of slots where busHz applies (0 = all slots)
/// @return true if address found and updated
bool BusStatusMgr::setDevicePollBusHzSlotMask(BusElemAddrType address, uint64_t slotMask)
{
    // Obtain semaphore
    if (!RaftMutex_lock(_busElemStatusMutex, RAFT_MUTEX_WAIT_FOREVER))
        return false;

    bool updated = false;
    BusAddrRecord* pAddrStatus = findAddrStatusRecordEditable(address);
    if (pAddrStatus)
    {
        pAddrStatus->deviceStatus.deviceIdentPolling.pollBusHzSlotMask = slotMask;
        updated = true;
    }

    RaftMutex_unlock(_busElemStatusMutex);
    return updated;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get bus frequency slot mask for polling a specific address
/// @param address address
/// @return bitmask of slots where busHz applies (0 = all slots)
uint64_t BusStatusMgr::getDevicePollBusHzSlotMask(BusElemAddrType address) const
{
    // Obtain semaphore
    if (!RaftMutex_lock(_busElemStatusMutex, RAFT_MUTEX_WAIT_FOREVER))
        return 0;

    uint64_t slotMask = 0;
    const BusAddrRecord* pAddrRecord = findAddrStatusRecord(address);
    if (pAddrRecord)
    {
        slotMask = pAddrRecord->deviceStatus.deviceIdentPolling.pollBusHzSlotMask;
    }

    RaftMutex_unlock(_busElemStatusMutex);
    return slotMask;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Register for device data notifications
/// @param addrAndSlot address
/// @param dataChangeCB Callback for data change
/// @param minTimeBetweenReportsMs Minimum time between reports (ms)
/// @param pCallbackInfo Callback info (passed to the callback)
void BusStatusMgr::registerForDeviceData(BusElemAddrType address, RaftDeviceDataChangeCB dataChangeCB, 
            uint32_t minTimeBetweenReportsMs, const void* pCallbackInfo)
{
    // Obtain semaphore
    if (!RaftMutex_lock(_busElemStatusMutex, RAFT_MUTEX_WAIT_FOREVER))
        return;

    // Find address record
    BusAddrRecord* pAddrStatus = findAddrStatusRecordEditable(address);
    if (pAddrStatus)
    {
        // Register for data change
        pAddrStatus->registerForDataChange(dataChangeCB, minTimeBetweenReportsMs, pCallbackInfo);
    }

    // Return semaphore
    RaftMutex_unlock(_busElemStatusMutex);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Unregister for device data notifications for a specific address
/// @param address address (including slot)
/// @param pCallbackInfo Callback info that was passed when registering (identifies the subscriber)
/// @return true if a matching registration was found (and disarmed)
bool BusStatusMgr::unregisterForDeviceData(BusElemAddrType address, const void* pCallbackInfo)
{
    return unregisterForDeviceDataHelper(false, address, pCallbackInfo) > 0;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Unregister for device data notifications on all addresses
/// @param pCallbackInfo Callback info that was passed when registering (identifies the subscriber)
/// @return number of registrations disarmed
uint32_t BusStatusMgr::unregisterForDeviceDataAll(const void* pCallbackInfo)
{
    return unregisterForDeviceDataHelper(true, 0, pCallbackInfo);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Unregister helper
/// @param matchAllAddresses true to match all addresses
/// @param address address to match (if not matchAllAddresses)
/// @param pCallbackInfo Callback info to match
/// @return number of registrations disarmed
uint32_t BusStatusMgr::unregisterForDeviceDataHelper(bool matchAllAddresses, BusElemAddrType address, const void* pCallbackInfo)
{
    // Obtain semaphore
    if (!RaftMutex_lock(_busElemStatusMutex, RAFT_MUTEX_WAIT_FOREVER))
        return 0;

    // Disarm the callback and callback info in all matching records (under the mutex so the bus task
    // can never pick up a callback with mismatched info)
    uint32_t numDisarmed = 0;
    for (BusAddrRecord& addrStatus : _addrStatus)
    {
        if (!matchAllAddresses && (addrStatus.address != address))
            continue;
        if (addrStatus.getDataChangeCB() && (addrStatus.getCallbackInfo() == pCallbackInfo))
        {
            addrStatus.registerForDataChange(nullptr, 0, nullptr);
            numDisarmed++;
        }
    }

    // Check (under the mutex) if a callback for this subscriber is in progress on another task
    bool waitForCB = _dataChangeCBInProgress && (_dataChangeCBInProgressInfo == pCallbackInfo) &&
                (_dataChangeCBInProgressTask != (void*)xTaskGetCurrentTaskHandle());
    uint32_t callCountAtUnreg = _dataChangeCBCallCount;

    // Return semaphore
    RaftMutex_unlock(_busElemStatusMutex);

    // Wait for the in-progress callback to complete (no new callback for this subscriber can start
    // on an address disarmed above so only the call which was in progress at that point is of interest)
    if (waitForCB)
    {
        uint32_t waitStartMs = millis();
        while (_dataChangeCBInProgress && (_dataChangeCBCallCount == callCountAtUnreg))
        {
            if (Raft::isTimeout(millis(), waitStartMs, DATA_CHANGE_CB_QUIESCE_MAX_MS))
            {
                LOG_W(MODULE_PREFIX, "unregisterForDeviceData callback still in progress after %dms",
                            (int)DATA_CHANGE_CB_QUIESCE_MAX_MS);
                break;
            }
            vTaskDelay(1);
        }
    }
    return numDisarmed;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Clear an element's identification so the scanner identifies it again
/// @param address address (including slot)
void BusStatusMgr::clearDeviceIdentification(BusElemAddrType address)
{
    if (!RaftMutex_lock(_busElemStatusMutex, RAFT_MUTEX_WAIT_FOREVER))
        return;
    BusAddrRecord* pAddrStatus = findAddrStatusRecordEditable(address);
    if (pAddrStatus)
    {
        // Drop the device status (type index, polling info, data aggregator). The online
        // state is deliberately untouched: the device is still there, we just no longer
        // trust what we decided it was.
        pAddrStatus->deviceStatus.clear();
        pAddrStatus->isNewlyIdentified = false;
        addForgottenAddr(address);

        // Identified again at once - the identity change (if any) is reported when that is done
        pAddrStatus->identState = BusIdentState::Pending;
        pAddrStatus->identAttempts = 0;
        pAddrStatus->nextIdentMs = millis();
        pAddrStatus->reverifyRequested = false;
    }
    RaftMutex_unlock(_busElemStatusMutex);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get poll timing statistics for each polled device as JSON
/// @return JSON object keyed by element address (hex)
String BusStatusMgr::getPollTimingJson() const
{
    String json = "{";
    if (RaftMutex_lock(_busElemStatusMutex, RAFT_MUTEX_WAIT_FOREVER))
    {
        bool first = true;
        for (const BusAddrRecord& addrStatus : _addrStatus)
        {
            const DeviceStatus& deviceStatus = addrStatus.deviceStatus;
            if (!deviceStatus.isValid() || (deviceStatus.getNumPollRequests() == 0))
                continue;
            if (!first)
                json += ",";
            first = false;
            json += "\"" + BusI2CAddrAndSlot::toString(addrStatus.address) + "\":{\"dtIdx\":" +
                    String(deviceStatus.getDeviceTypeIndex()) + "," + deviceStatus.pollTimingStats.getJson().substring(1);
        }
        RaftMutex_unlock(_busElemStatusMutex);
    }
    json += "}";
    return json;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Record the bus time a device's poll (or poll step) took
/// @param address address
/// @param busTimeUs bus time in us
void BusStatusMgr::recordPollBusTimeUs(BusElemAddrType address, uint64_t busTimeUs)
{
    if (!RaftMutex_lock(_busElemStatusMutex, RAFT_MUTEX_WAIT_FOREVER))
        return;
    BusAddrRecord* pAddrStatus = findAddrStatusRecordEditable(address);
    if (pAddrStatus)
        pAddrStatus->deviceStatus.pollTimingStats.recordBusTimeUs(busTimeUs > UINT32_MAX ? UINT32_MAX : (uint32_t)busTimeUs);
    RaftMutex_unlock(_busElemStatusMutex);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Estimate the bus time one poll of a device takes
/// @param pollInfo device polling info
/// @param busHz bus frequency
/// @return estimated time in us
/// @note each byte is 9 bit times (8 data + ack), each transaction has an address byte for its write and another
///       for its read, plus start, restart and stop and a fixed software overhead
uint32_t BusStatusMgr::estimatePollTimeUs(const DevicePollingInfo& pollInfo, uint32_t busHz)
{
    if (busHz == 0)
        return 0;
    static const uint32_t BITS_PER_TRANSACTION_OVERHEAD = 3;
    static const uint32_t SOFTWARE_OVERHEAD_US_PER_TRANSACTION = 100;
    uint64_t totalUs = 0;
    for (const BusRequestInfo& req : pollInfo.pollReqs)
    {
        uint32_t bytes = 1 + req.getWriteDataLen();
        if (req.getReadReqLen() > 0)
            bytes += 1 + req.getReadReqLen();
        uint32_t bits = bytes * 9 + BITS_PER_TRANSACTION_OVERHEAD;
        totalUs += (uint64_t)bits * 1000000 / busHz + SOFTWARE_OVERHEAD_US_PER_TRANSACTION;
    }
    return totalUs > UINT32_MAX ? UINT32_MAX : (uint32_t)totalUs;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Estimate the share of the bus time that polling online devices at their intervals needs
/// @param defaultBusHz bus frequency for devices that don't set their own
/// @return estimated load in percent (can exceed 100 when more polling is configured than the bus can do)
uint32_t BusStatusMgr::getPollLoadPct(uint32_t defaultBusHz) const
{
    if (!RaftMutex_lock(_busElemStatusMutex, RAFT_MUTEX_WAIT_FOREVER))
        return 0;
    double load = 0;
    for (const BusAddrRecord& addrStatus : _addrStatus)
    {
        const DevicePollingInfo& pollInfo = addrStatus.deviceStatus.deviceIdentPolling;
        if ((addrStatus.onlineState != DeviceOnlineState::ONLINE) || (pollInfo.pollReqs.size() == 0))
            continue;
        uint32_t busHz = pollInfo.pollBusHz != 0 ? pollInfo.pollBusHz : defaultBusHz;
        uint32_t intervalUs = DevicePollingInfo::clampPollIntervalUs(pollInfo.pollIntervalUs);
        uint32_t pollTimeUs = addrStatus.deviceStatus.pollTimingStats.getMeanBusTimePerPollUs();
        if (pollTimeUs == 0)
            pollTimeUs = estimatePollTimeUs(pollInfo, busHz);
        load += (double)pollTimeUs / intervalUs;
    }
    RaftMutex_unlock(_busElemStatusMutex);
    return (uint32_t)(load * 100 + 0.5);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Reset the poll timing statistics of all devices
void BusStatusMgr::resetPollTiming()
{
    if (!RaftMutex_lock(_busElemStatusMutex, RAFT_MUTEX_WAIT_FOREVER))
        return;
    for (BusAddrRecord& addrStatus : _addrStatus)
        addrStatus.deviceStatus.pollTimingStats.reset();
    RaftMutex_unlock(_busElemStatusMutex);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get identification timing for each element seen since the bus started, as JSON
/// @return JSON array
String BusStatusMgr::getIdentTimingJson() const
{
    String json = "[";
    if (RaftMutex_lock(_busElemStatusMutex, RAFT_MUTEX_WAIT_FOREVER))
    {
        for (uint32_t i = 0; i < _identTimingCount; i++)
        {
            const IdentTiming& t = _identTiming[i];
            char buf[200];
            auto msOrNeg = [](uint32_t ms) { return (ms == IdentTiming::NOT_REACHED) ? -1L : (long)ms; };
            const BusAddrRecord* pRec = findAddrStatusRecord(t.address);
            snprintf(buf, sizeof(buf), R"(%s{"addr":"%s","seenMs":%ld,"onlineMs":%ld,"identMs":%ld,"dtIdx":%d,"attempts":%d,"state":"%s"})",
                     i > 0 ? "," : "", BusI2CAddrAndSlot::toString(t.address).c_str(), msOrNeg(t.seenMs),
                     msOrNeg(t.onlineMs), msOrNeg(t.identMs),
                     t.deviceTypeIndex == DEVICE_TYPE_INDEX_INVALID ? -1 : (int)t.deviceTypeIndex,
                     (int)t.identAttempts, pRec ? BusIdentSchedule::getStateStr(pRec->identState) : "gone");
            json += buf;
        }
        RaftMutex_unlock(_busElemStatusMutex);
    }
    json += "]";
    return json;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Find (or add) the identification timing entry for an element - assumes semaphore taken
/// @param address element address
/// @param addIfMissing true to add an entry if there is none (and there is room)
/// @return entry or nullptr
BusStatusMgr::IdentTiming* BusStatusMgr::getIdentTiming(BusElemAddrType address, bool addIfMissing)
{
    for (uint32_t i = 0; i < _identTimingCount; i++)
    {
        if (_identTiming[i].address == address)
            return &_identTiming[i];
    }
    if (!addIfMissing || (_identTimingCount >= MAX_IDENT_TIMING))
        return nullptr;
    IdentTiming& entry = _identTiming[_identTimingCount++];
    entry = IdentTiming();
    entry.address = address;
    return &entry;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get the next element due for identification (round robin)
/// @param timeNowMs time now
/// @param generation device type records generation
/// @param hookEpoch identification hook epoch
/// @param work (out) the element
/// @return true if an element is due
bool BusStatusMgr::getNextIdentWork(uint32_t timeNowMs, uint32_t generation, uint32_t hookEpoch, IdentWork& work)
{
    if (!RaftMutex_lock(_busElemStatusMutex, RAFT_MUTEX_WAIT_FOREVER))
        return false;
    bool found = false;
    uint32_t numRecs = _addrStatus.size();
    for (uint32_t i = 0; i < numRecs; i++)
    {
        uint32_t pos = (_identCursor + i) % numRecs;
        const BusAddrRecord& rec = _addrStatus[pos];

        // Online, and responded to its last access (an element that has just stopped responding - e.g. moved to
        // another address - isn't identified at its old address)
        if ((rec.onlineState != DeviceOnlineState::ONLINE) || (rec.count < 0))
            continue;
        bool identifiedDue = (rec.identState == BusIdentState::Identified) && BusIdentSchedule::timeReached(timeNowMs, rec.nextIdentMs);
        bool reident = rec.reidentRequested && identifiedDue;
        bool reverify = !reident && rec.reverifyRequested && identifiedDue;
        bool catalogueChanged = (rec.identGeneration != generation) || (rec.identHookEpoch != hookEpoch);
        if (!reident && !reverify && !BusIdentSchedule::isDue(rec.identState, rec.nextIdentMs, timeNowMs, catalogueChanged))
            continue;
        work.address = rec.address;
        work.reverify = reverify;
        work.reident = reident;
        work.deviceTypeIndex = rec.deviceStatus.getDeviceTypeIndex();
        _identCursor = pos + 1;
        found = true;
        break;
    }
    RaftMutex_unlock(_busElemStatusMutex);
    return found;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Set the outcome of identifying an element
void BusStatusMgr::setIdentOutcome(BusElemAddrType address, BusIdentState state, const DeviceStatus& deviceStatus,
            uint32_t generation, uint32_t hookEpoch, uint32_t resumeAtMs)
{
    if (!RaftMutex_lock(_busElemStatusMutex, RAFT_MUTEX_WAIT_FOREVER))
        return;
    BusAddrRecord* pRec = findAddrStatusRecordEditable(address);
    if (pRec)
    {
        uint32_t timeNowMs = millis();
        if (state == BusIdentState::Pending)
        {
            // In progress - resumed after a pause (the state is that of the last completed attempt, which the
            // retry backoff counts from)
            pRec->nextIdentMs = resumeAtMs;
        }
        else
        {
            // An attempt completed
            IdentTiming* pIdentTiming = getIdentTiming(address, false);
            if (pIdentTiming)
                pIdentTiming->identAttempts++;
            pRec->identAttempts = (pRec->identState == state) && (pRec->identAttempts < UINT8_MAX) ? pRec->identAttempts + 1 : 1;
            pRec->identState = state;
            pRec->identGeneration = generation;
            pRec->identHookEpoch = hookEpoch;
            pRec->nextIdentMs = timeNowMs + BusIdentSchedule::retryDelayMs(state, pRec->identAttempts);
            pRec->identChangePending = true;
            _busElemStatusChangeDetected = true;
            if (state == BusIdentState::Identified)
            {
                pRec->deviceStatus = deviceStatus;
                pRec->isNewlyIdentified = true;
                pRec->identAttempts = 0;
                if (pIdentTiming && (pIdentTiming->identMs == IdentTiming::NOT_REACHED))
                {
                    pIdentTiming->identMs = timeNowMs - _setupTimeMs;
                    pIdentTiming->deviceTypeIndex = deviceStatus.getDeviceTypeIndex();
                }
            }
            else
            {
                pRec->deviceStatus.clear();
            }
        }
    }
    RaftMutex_unlock(_busElemStatusMutex);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Set the outcome of re-verifying an element
void BusStatusMgr::setReverifyOutcome(BusElemAddrType address, ReverifyResult result)
{
    if (!RaftMutex_lock(_busElemStatusMutex, RAFT_MUTEX_WAIT_FOREVER))
        return;
    BusAddrRecord* pRec = findAddrStatusRecordEditable(address);
    if (pRec)
    {
        uint32_t timeNowMs = millis();
        switch (result)
        {
            case ReverifyResult::StillMatches:
                // Re-initialised: reported as Identified (with the same type) so that settings made since it was
                // first initialised are applied again
                pRec->reverifyRequested = false;
                pRec->identChangePending = true;
                _busElemStatusChangeDetected = true;
                break;
            case ReverifyResult::Mismatch:
                // Not that type now: identified again at once (and the identity change reported then)
                pRec->deviceStatus.clear();
                pRec->reverifyRequested = false;
                pRec->identState = BusIdentState::Pending;
                pRec->identAttempts = 0;
                pRec->nextIdentMs = timeNowMs;
                addForgottenAddr(address);
                break;
            case ReverifyResult::BusError:
                pRec->nextIdentMs = timeNowMs + BusIdentSchedule::RETRY_BUS_ERROR_MIN_MS;
                break;
        }
    }
    RaftMutex_unlock(_busElemStatusMutex);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Mark the devices in a re-identification request
/// @param request which devices
/// @return number of devices marked
uint32_t BusStatusMgr::applyReIdentRequest(const BusReIdentRequest& request)
{
    if (!RaftMutex_lock(_busElemStatusMutex, RAFT_MUTEX_WAIT_FOREVER))
        return 0;
    uint32_t timeNowMs = millis();
    uint32_t numMarked = 0;
    for (BusAddrRecord& rec : _addrStatus)
    {
        if (rec.onlineState != DeviceOnlineState::ONLINE)
            continue;
        DeviceTypeIndexType currentTypeIndex = (rec.identState == BusIdentState::Identified) ?
                    rec.deviceStatus.getDeviceTypeIndex() : DEVICE_TYPE_INDEX_INVALID;
        std::vector<DeviceTypeIndexType> candidates;
        if (request.scope == BusReIdentScope::Outranked)
            candidates = deviceTypeRecords.getIdentCandidates(BusI2CAddrAndSlot::getI2CAddr(rec.address));
        if (!BusIdentSchedule::reIdentScopeIncludes(request, rec.address, currentTypeIndex, candidates))
            continue;
        if (currentTypeIndex != DEVICE_TYPE_INDEX_INVALID)
        {
            // A device identified by a hook (probe-only type) is only re-identified when asked for by address
            const DeviceTypeRecord* pRec = deviceTypeRecords.getRecord(currentTypeIndex);
            if (pRec && pRec->isProbeOnly() && (request.scope != BusReIdentScope::Address))
                continue;
            rec.reidentRequested = true;
            rec.reidentAttempts = 0;
        }
        else
        {
            rec.identAttempts = 0;
        }
        rec.nextIdentMs = timeNowMs;
        numMarked++;
    }
    RaftMutex_unlock(_busElemStatusMutex);
    return numMarked;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Set the outcome of re-identifying an identified device
void BusStatusMgr::setReIdentOutcome(BusElemAddrType address, ReIdentResult result, const DeviceStatus& deviceStatus,
            uint32_t generation, uint32_t hookEpoch)
{
    if (!RaftMutex_lock(_busElemStatusMutex, RAFT_MUTEX_WAIT_FOREVER))
        return;
    BusAddrRecord* pRec = findAddrStatusRecordEditable(address);
    if (pRec)
    {
        uint32_t timeNowMs = millis();
        IdentTiming* pIdentTiming = getIdentTiming(address, false);
        if (pIdentTiming)
            pIdentTiming->identAttempts++;
        switch (result)
        {
            case ReIdentResult::SameType:
                pRec->reidentRequested = false;
                pRec->identGeneration = generation;
                pRec->identHookEpoch = hookEpoch;
                break;
            case ReIdentResult::NewType:
                pRec->reidentRequested = false;
                pRec->reverifyRequested = false;
                pRec->deviceStatus = deviceStatus;
                pRec->isNewlyIdentified = true;
                pRec->identChangePending = true;
                pRec->identGeneration = generation;
                pRec->identHookEpoch = hookEpoch;
                _busElemStatusChangeDetected = true;
                addForgottenAddr(address);
                break;
            case ReIdentResult::NoMatch:
                pRec->reidentRequested = false;
                pRec->reverifyRequested = false;
                pRec->deviceStatus.clear();
                pRec->identState = BusIdentState::NoMatch;
                pRec->identAttempts = 1;
                pRec->identGeneration = generation;
                pRec->identHookEpoch = hookEpoch;
                pRec->nextIdentMs = timeNowMs + BusIdentSchedule::retryDelayMs(BusIdentState::NoMatch, 1);
                pRec->identChangePending = true;
                _busElemStatusChangeDetected = true;
                addForgottenAddr(address);
                break;
            case ReIdentResult::TryAgain:
                // Keeps its type; tried a few more times, then left as it is
                pRec->reidentAttempts++;
                if (pRec->reidentAttempts >= REIDENT_MAX_ATTEMPTS)
                    pRec->reidentRequested = false;
                else
                    pRec->nextIdentMs = timeNowMs + BusIdentSchedule::retryDelayMs(BusIdentState::BusError, pRec->reidentAttempts);
                break;
        }
    }
    RaftMutex_unlock(_busElemStatusMutex);
}

