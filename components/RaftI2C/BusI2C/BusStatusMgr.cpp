/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Bus Status Manager
//
// Rob Dobson 2020-2024
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "BusStatusMgr.h"
#include "Logger.h"
#include "RaftUtils.h"
#include "DeviceIdentMgr.h"
#include <algorithm>

// #define DEBUG_HANDLE_BUS_ELEM_STATE_CHANGES
// #define DEBUG_CONSECUTIVE_ERROR_HANDLING
// #define DEBUG_CONSECUTIVE_ERROR_HANDLING_ADDR 0x55
// #define WARN_ON_FAILED_TO_GET_SEMAPHORE
// #define DEBUG_BUS_OPERATION_STATUS
// #define DEBUG_NO_SCANNING
// #define DEBUG_SERVICE_BUS_ELEM_STATUS_CHANGE
// #define DEBUG_ACCESS_BARRING_FOR_MS
// #define DEBUG_HANDLE_BUS_DEVICE_INFO
// #define DEBUG_HANDLE_POLL_RESULT
// #define DEBUG_GET_POLL_RESULT

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
    // so we can update bus and element operation status - don't worry if we can't
    // access the list as there will be other service loops
    std::vector<BusElemAddrAndStatus> statusChanges;
    uint32_t numChanges = 0;
    if (RaftMutex_lock(_busElemStatusMutex, RAFT_MUTEX_WAIT_FOREVER))
    {
        // Go through once and look for changes
        for (auto& addrStatus : _addrStatus)
        {
            if (addrStatus.isChange || addrStatus.isNewlyIdentified)
                numChanges++;
        }

        // Return semaphore
        RaftMutex_unlock(_busElemStatusMutex);
    }

    // Check for status changes
    if (numChanges > 0)
    {
        // Make space for changes
        statusChanges.reserve(numChanges+1);

        // Get semaphore again
        if (RaftMutex_lock(_busElemStatusMutex, RAFT_MUTEX_WAIT_FOREVER))
        {
            for (auto& addrStatus : _addrStatus)
            {
                if (addrStatus.isChange || addrStatus.isNewlyIdentified)
                {
                    // Handle element change
                    BusElemAddrAndStatus statusChange = 
                        {
                            addrStatus.address, 
                            (addrStatus.onlineState == DeviceOnlineState::ONLINE) && addrStatus.isChange,
                            (addrStatus.onlineState == DeviceOnlineState::OFFLINE) && addrStatus.isChange,
                            addrStatus.isNewlyIdentified,
                            addrStatus.deviceStatus.getDeviceTypeIndex()
                        };
                    statusChanges.push_back(statusChange);
                    addrStatus.isChange = false;
                    addrStatus.isNewlyIdentified = false;
                }

                // Check if this is the addrForLockupDetect
                if (_addrForLockupDetectValid && 
                            (addrStatus.address == _addrForLockupDetect) && 
                            (addrStatus.onlineState == DeviceOnlineState::OFFLINE))
                {
                    newBusOperationStatus = (addrStatus.onlineState == DeviceOnlineState::ONLINE) ? BUS_OPERATION_OK : BUS_OPERATION_FAILING;
                }
            }

           // Unlock
           RaftMutex_unlock(_busElemStatusMutex);
        }

        // No more changes
        _busElemStatusChangeDetected = false;
    }

    // Return semaphore

    // Perform elem state change callback if required
    if ((statusChanges.size() > 0))
        _raftBus.callBusElemStatusCB(statusChanges);

    // Debug
#ifdef DEBUG_SERVICE_BUS_ELEM_STATUS_CHANGE
    for (auto& statusChange : statusChanges)
    {
        LOG_I(MODULE_PREFIX, "loop address %04x status change to %s",
                    statusChange.address,
                    statusChange.isChangeToOnline ? "online" : "offline");
    }
#endif

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
#ifdef DEBUG_HANDLE_BUS_ELEM_STATE_CHANGES
    LOG_I(MODULE_PREFIX, "updateBusElemState address %04x isResponding %d", address, elemResponding);
#endif

    // Check for new status change
    isOnline = false;
    bool isNewStatusChange = false;
    bool flagSpuriousRecord = false;

#ifdef DEBUG_CONSECUTIVE_ERROR_HANDLING
    // Debug
    BusAddrStatus prevStatus;
    BusAddrStatus newStatus;
#endif

    // Obtain semaphore controlling access to busElemChange list and flag
    if (RaftMutex_lock(_busElemStatusMutex, RAFT_MUTEX_WAIT_FOREVER))
    {

        // Find address record
        BusAddrStatus* pAddrStatus = findAddrStatusRecordEditable(address);

        // If not found and element is responding then add a new record
        if ((pAddrStatus == nullptr) && elemResponding && (_addrStatus.size() < ADDR_STATUS_MAX))
        {
            // Add new record
            BusAddrStatus newAddrStatus;
            newAddrStatus.address = address;
            _addrStatus.push_back(newAddrStatus);
            pAddrStatus = &_addrStatus.back();
        }

        // Check if we found a record
        if (pAddrStatus)
        {
#ifdef DEBUG_CONSECUTIVE_ERROR_HANDLING
            // Debug
            prevStatus = *pAddrStatus;
#endif

            // Handle element response
            isNewStatusChange = pAddrStatus->handleResponding(elemResponding, flagSpuriousRecord);
            isOnline = pAddrStatus->onlineState == DeviceOnlineState::ONLINE;

#ifdef DEBUG_CONSECUTIVE_ERROR_HANDLING
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
        }

        // Check for spurious record detected
        if (flagSpuriousRecord)
        {
            // Remove the record
            _addrStatus.erase(std::remove_if(_addrStatus.begin(), _addrStatus.end(), 
                [address](BusAddrStatus& addrStatus) { return addrStatus.address == address; }), 
                _addrStatus.end());
        }

        // Return semaphore
        RaftMutex_unlock(_busElemStatusMutex);

#ifdef DEBUG_CONSECUTIVE_ERROR_HANDLING
#ifdef DEBUG_CONSECUTIVE_ERROR_HANDLING_ADDR
        if (address == DEBUG_CONSECUTIVE_ERROR_HANDLING_ADDR)
#endif
        if (isNewStatusChange)
        {
            LOG_I(MODULE_PREFIX, "updateBusElemState address %04x count %d(prev %d) state %s(prev %s) isChange %d(was %d) isResponding %d",
                        newStatus.address,
                        newStatus.count, prevStatus.count, 
                        newStatus.onlineState == DeviceOnlineState::ONLINE ? "ONLINE" : 
                            (newStatus.onlineState == DeviceOnlineState::OFFLINE ? "OFFLINE" : "INITIAL"),
                        prevStatus.onlineState == DeviceOnlineState::ONLINE ? "ONLINE" : 
                            (prevStatus.onlineState == DeviceOnlineState::OFFLINE ? "OFFLINE" :
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
        const BusAddrStatus* pAddrStatus = findAddrStatusRecord(address);
        if (!pAddrStatus || (pAddrStatus->onlineState != DeviceOnlineState::OFFLINE))
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
    BusAddrStatus* pAddrStatus = findAddrStatusRecordEditable(address);
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
    BusAddrStatus* pAddrStatus = findAddrStatusRecordEditable(address);

    // Check if access is barred
    if (pAddrStatus && pAddrStatus->barDurationMs != 0)
    {
        // Check if time has elapsed and ignore request if not
        if (Raft::isTimeout(timeNowMs, pAddrStatus->barStartMs, pAddrStatus->barDurationMs))
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
    BusAddrStatus* pAddrStatus = findAddrStatusRecordEditable(address);
    if (pAddrStatus)
    {
        // Set device status (includes device type index)
        pAddrStatus->deviceStatus = deviceStatus;

        // Check if device type index is valid - if so this is a new identification
        if (deviceStatus.getDeviceTypeIndex() != DeviceStatus::DEVICE_TYPE_INDEX_INVALID)
        {
            pAddrStatus->isNewlyIdentified = true;
        }
    }

    // Return semaphore
    RaftMutex_unlock(_busElemStatusMutex);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get device type index by address
/// @param address address
/// @return device type index
uint16_t BusStatusMgr::getDeviceTypeIndexByAddr(BusElemAddrType address) const
{
    // Obtain semaphore
    if (!RaftMutex_lock(_busElemStatusMutex, RAFT_MUTEX_WAIT_FOREVER))
        return DeviceStatus::DEVICE_TYPE_INDEX_INVALID;

    // Find address record
    uint16_t deviceTypeIndex = DeviceStatus::DEVICE_TYPE_INDEX_INVALID;
    const BusAddrStatus* pAddrStatus = findAddrStatusRecord(address);
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
    const BusAddrStatus* pAddrStatus = findAddrStatusRecord(address);
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
/// @brief Inform that addresses are going offline
/// @param addrList list of addresses
void BusStatusMgr::goingOffline(std::vector<BusElemAddrType>& addrList)
{
    // Obtain semaphore
    if (!RaftMutex_lock(_busElemStatusMutex, RAFT_MUTEX_WAIT_FOREVER))
        return;

    // Go through all devices and set status
    for (BusAddrStatus& addrStatus : _addrStatus)
    {
        if (std::find(addrList.begin(), addrList.end(), addrStatus.address) != addrList.end())
        {
            addrStatus.isChange = addrStatus.onlineState == DeviceOnlineState::ONLINE;
            addrStatus.onlineState = DeviceOnlineState::OFFLINE;
            _busElemStatusChangeDetected = true;
            _lastBusElemOnlineStatusUpdateTimeMs = millis();
            _lastPollOrStatusUpdateTimeMs = millis();
        }
    }

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
    bool anySet = false;
    for (BusAddrStatus& addrStatus : _addrStatus)
    {
        addrStatus.isChange = addrStatus.onlineState == DeviceOnlineState::ONLINE;
        addrStatus.onlineState = DeviceOnlineState::OFFLINE;
        anySet = true;
    }
    _busElemStatusChangeDetected = anySet;
    if (anySet)
    {
        _lastBusElemOnlineStatusUpdateTimeMs = millis();
        _lastPollOrStatusUpdateTimeMs = millis();
    }

    // Return semaphore
    RaftMutex_unlock(_busElemStatusMutex);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Get pending ident poll requests for a single device
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool BusStatusMgr::getPendingIdentPoll(uint64_t timeNowUs, DevicePollingInfo& pollInfo)
{
    // Obtain semaphore
    if (!RaftMutex_lock(_busElemStatusMutex, RAFT_MUTEX_WAIT_FOREVER))
        return false;

    // Check for any pending requests
    for (BusAddrStatus& addrStatus : _addrStatus)
    {
        // Check if a poll is due
        if (addrStatus.deviceStatus.getPendingIdentPollInfo(timeNowUs, pollInfo))
        {
            // Return semaphore
            RaftMutex_unlock(_busElemStatusMutex);
            return true;
        }
    }

    // Return semaphore
    RaftMutex_unlock(_busElemStatusMutex);
    return false;
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
    uint16_t deviceTypeIdx = 0;
    uint32_t timeNowMs = timeNowUs / 1000;

    // Obtain semaphore
    if (!RaftMutex_lock(_busElemStatusMutex, RAFT_MUTEX_WAIT_FOREVER))
        return false;

    // Find address record
    BusAddrStatus* pAddrStatus = findAddrStatusRecordEditable(address);
    bool putResult = false;
    if (pAddrStatus)
    {
        // Add result to aggregator
        putResult = pAddrStatus->deviceStatus.storePollResults(nextReqIdx, timeNowUs, pollResultData, pPollInfo, pauseAfterSendMs);

        // Check if this is a completed poll
        if (nextReqIdx == 0)
        {            
            // Check for callback
            pCallback = pAddrStatus->getDataChangeCB();
            if (pCallback)
            {
                // Check min time between reports
                if (Raft::isTimeout(timeNowMs, pAddrStatus->lastDataChangeReportTimeMs, pAddrStatus->minTimeBetweenReportsMs))
                {
                    // Get device type index and callback info
                    deviceTypeIdx = pAddrStatus->deviceStatus.getDeviceTypeIndex();
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

    // Return semaphore
    RaftMutex_unlock(_busElemStatusMutex);

    // Check if a callback is required
    if (pCallback)
    {
        // Call the callback
        pCallback(deviceTypeIdx, pollResultData, pCallbackInfo);
    }

#ifdef DEBUG_HANDLE_POLL_RESULT
    LOG_I(MODULE_PREFIX, "handlePollResult address %04x pAddrStatus %p deviceTypeIdx %d callbackValid %s", 
                address, pAddrStatus, deviceTypeIdx,
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
    for (const BusAddrStatus& addrStatus : _addrStatus)
    {
        bool includeAddr = !onlyAddressesWithIdentPollResponses || addrStatus.deviceStatus.dataAggregator.count() > 0;
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
/// @param isOnline - (out) true if device is online
/// @param deviceTypeIndex - (out) device type index
/// @param devicePollResponseData - (out) vector to store the device poll response data
/// @param responseSize - (out) size of the response data
/// @param maxResponsesToReturn - maximum number of responses to return (0 for no limit)
/// @return number of responses returned
uint32_t BusStatusMgr::getBusElemPollResponses(BusElemAddrType address, bool& isOnline, uint16_t& deviceTypeIndex, 
            std::vector<uint8_t>& devicePollResponseData, 
            uint32_t& responseSize, uint32_t maxResponsesToReturn)
{
    // Obtain semaphore
    if (!RaftMutex_lock(_busElemStatusMutex, RAFT_MUTEX_WAIT_FOREVER))
        return 0;

    // Find address record
    uint32_t numResponses = 0;
    BusAddrStatus* pAddrStatus = findAddrStatusRecordEditable(address);
    if (pAddrStatus)
    {
        // Elem online
        isOnline = pAddrStatus->onlineState == DeviceOnlineState::ONLINE;
        
        // Device type index
        deviceTypeIndex = pAddrStatus->deviceStatus.getDeviceTypeIndex();

        // Get results from aggregator
        numResponses = pAddrStatus->deviceStatus.dataAggregator.get(devicePollResponseData, responseSize, maxResponsesToReturn);
    }

    // Return semaphore
    RaftMutex_unlock(_busElemStatusMutex);

#ifdef DEBUG_GET_POLL_RESULT
    LOG_I(MODULE_PREFIX, "getBusElemPollResponses address %04x isOnline %d deviceTypeIndex %d numResponses %d responseSize %d",
                address, isOnline, deviceTypeIndex, numResponses, responseSize);
#endif

    return numResponses;
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
    for (const BusAddrStatus& addrStatus : _addrStatus)
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
/// @brief Set device polling interval (us) for a specific address
/// @param address address
/// @param pollIntervalUs poll interval in microseconds
/// @return true if address found and updated
bool BusStatusMgr::setDevicePollInterval(BusElemAddrType address, uint32_t pollIntervalUs)
{
    // Obtain semaphore
    if (!RaftMutex_lock(_busElemStatusMutex, RAFT_MUTEX_WAIT_FOREVER))
        return false;

    bool updated = false;
    BusAddrStatus* pAddrStatus = findAddrStatusRecordEditable(address);
    if (pAddrStatus)
    {
        pAddrStatus->deviceStatus.deviceIdentPolling.pollIntervalUs = pollIntervalUs;
        updated = true;
    }

    RaftMutex_unlock(_busElemStatusMutex);
    return updated;
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
    BusAddrStatus* pAddrStatus = findAddrStatusRecordEditable(address);
    if (pAddrStatus)
    {
        // Register for data change
        pAddrStatus->registerForDataChange(dataChangeCB, minTimeBetweenReportsMs, pCallbackInfo);
    }

    // Return semaphore
    RaftMutex_unlock(_busElemStatusMutex);
}