/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// I2C Bus Status Manager
//
// Rob Dobson 2020-2024
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "BusStatusMgr.h"
#include "Logger.h"
#include "RaftUtils.h"
#include "DeviceIdentMgr.h"

// #define DEBUG_HANDLE_BUS_ELEM_STATE_CHANGES
// #define DEBUG_CONSECUTIVE_ERROR_HANDLING
// #define DEBUG_CONSECUTIVE_ERROR_HANDLING_ADDR 0x55
// #define WARN_ON_FAILED_TO_GET_SEMAPHORE
// #define DEBUG_BUS_OPERATION_STATUS
// #define DEBUG_NO_SCANNING
// #define DEBUG_SERVICE_BUS_ELEM_STATUS_CHANGE
// #define DEBUG_ACCESS_BARRING_FOR_MS
// #define DEBUG_HANDLE_BUS_DEVICE_INFO

static const char* MODULE_PREFIX = "BusStatusMgr";

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Constructor and destructor
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

BusStatusMgr::BusStatusMgr(BusBase& busBase) :
    _busBase(busBase)
{
    // Bus element status change detection
    _busElemStatusMutex = xSemaphoreCreateMutex();
}

BusStatusMgr::~BusStatusMgr()
{
    if (_busElemStatusMutex)
        vSemaphoreDelete(_busElemStatusMutex);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Setup
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void BusStatusMgr::setup(const RaftJsonIF& config)
{
    // Get address to use for lockup detection
    _addrForLockupDetect = 0;
    _addrForLockupDetectValid = false;
    uint32_t address = strtoul(config.getString("lockupDetect", "0xffffffff").c_str(), NULL, 0);
    if (address != 0xffffffff)
    {
        _addrForLockupDetect = address;
        _addrForLockupDetectValid = true;
    }
    _busOperationStatus = BUS_OPERATION_UNKNOWN;
    _busElemStatusChangeDetected = false;
    _i2cAddrStatus.clear();

    // Clear found on main bus bits
    for (int i = 0; i < SIZE_OF_MAIN_BUS_ADDR_BITS_ARRAY; i++)
        _mainBusAddrBits[i] = 0;

    // Debug
    LOG_I(MODULE_PREFIX, "task lockupDetect addr %02x (valid %s)",
                _addrForLockupDetect, _addrForLockupDetectValid ? "Y" : "N");

}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Service
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void BusStatusMgr::service(bool hwIsOperatingOk)
{
    // Check for any changes detected
    if (!_busElemStatusChangeDetected)
        return;

    // Obtain semaphore controlling access to busElemChange list and flag
    // so we can update bus and element operation status - don't worry if we can't
    // access the list as there will be other service loops
    std::vector<BusElemAddrAndStatus> statusChanges;
    BusOperationStatus newBusOperationStatus = _busOperationStatus; 
    if (xSemaphoreTake(_busElemStatusMutex, 0) == pdTRUE)
    {
        // Go through once and look for changes
        uint32_t numChanges = 0;
        for (auto& addrStatus : _i2cAddrStatus)
        {
            if (addrStatus.isChange)
                numChanges++;
        }

        // Create change vector if required
        if (numChanges > 0)
        {
            statusChanges.reserve(numChanges);
            for (auto& addrStatus : _i2cAddrStatus)
            {
                if (addrStatus.isChange)
                {
                    // Handle element change
                    BusElemAddrAndStatus statusChange = 
                        {
                            addrStatus.addrAndSlot.toCompositeAddrAndSlot(), 
                            addrStatus.isOnline,
                            addrStatus.wasOnline && !addrStatus.isOnline,
                        };
#ifdef DEBUG_SERVICE_BUS_ELEM_STATUS_CHANGE
                    LOG_I(MODULE_PREFIX, "service addr@slot+1 %s status change to %s", 
                                addrStatus.addrAndSlot.toString().c_str(),
                                statusChange.isChangeToOnline ? "online" : "offline");
#endif
                    statusChanges.push_back(statusChange);
                    addrStatus.isChange = false;

                    // Check if this is the addrForLockupDetect
                    if (_addrForLockupDetectValid && 
                                (addrStatus.addrAndSlot.addr == _addrForLockupDetect) && 
                                (addrStatus.wasOnline))
                    {
                        newBusOperationStatus = addrStatus.isOnline ? BUS_OPERATION_OK : BUS_OPERATION_FAILING;
                    }
                }
            }
        }

        // No more changes
        _busElemStatusChangeDetected = false;
    }

    // If no lockup detection addresses are used then rely on the bus's isOperatingOk() result
    if (!_addrForLockupDetectValid)
    {
        newBusOperationStatus = hwIsOperatingOk ? BUS_OPERATION_OK : BUS_OPERATION_FAILING;
    }

    // Return semaphore
    xSemaphoreGive(_busElemStatusMutex);

    // Elem change callback if required
    if ((statusChanges.size() > 0))
        _busBase.callBusElemStatusCB(statusChanges);

    // Bus operation change callback if required
    if (_busOperationStatus != newBusOperationStatus)
    {
#ifdef DEBUG_BUS_OPERATION_STATUS
        LOG_I(MODULE_PREFIX, "service newOpStatus %s (was %s)", 
                    newBusOperationStatus ? "online" : "offline",
                    _busOperationStatus ? "online" : "offline");
#endif
        _busOperationStatus = newBusOperationStatus;
        _busBase.callBusOperationStatusCB(_busOperationStatus);
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Update bus element state
// Returns true if the element state has changed
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool BusStatusMgr::updateBusElemState(BusI2CAddrAndSlot addrAndSlot, bool elemResponding, bool& isOnline)
{
#ifdef DEBUG_HANDLE_BUS_ELEM_STATE_CHANGES
    LOG_I(MODULE_PREFIX, "updateBusElemState addr@slot+1 %s isResponding %d", 
                        addrAndSlot.toString().c_str(), elemResponding);
#endif

    // Check for new status change
    isOnline = false;
    bool isNewStatusChange = false;
    bool flagSpuriousRecord = false;

    // Obtain semaphore controlling access to busElemChange list and flag
    if (xSemaphoreTake(_busElemStatusMutex, pdMS_TO_TICKS(1)) == pdTRUE)
    {
#ifdef DEBUG_CONSECUTIVE_ERROR_HANDLING
        // Debug
        BusI2CAddrStatus prevStatus;
        BusI2CAddrStatus newStatus;
#endif

        // Find address record
        BusI2CAddrStatus* pAddrStatus = findAddrStatusRecordEditable(addrAndSlot);

        // If not found and element is responding then add a new record
        if ((pAddrStatus == nullptr) && elemResponding && (_i2cAddrStatus.size() < I2C_ADDR_STATUS_MAX))
        {
            // Add new record
            BusI2CAddrStatus newAddrStatus;
            newAddrStatus.addrAndSlot = addrAndSlot;
            _i2cAddrStatus.push_back(newAddrStatus);
            pAddrStatus = &_i2cAddrStatus.back();
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
            isOnline = pAddrStatus->isOnline;

            // Check if this is a main-bus address (not on an extender) and keep track of all main-bus addresses if so
            if (isNewStatusChange && isOnline && (addrAndSlot.slotPlus1 == 0))
            {
                // Set address found on main bus
                setAddrFoundOnMainBus(addrAndSlot.addr);
            }

#ifdef DEBUG_HANDLE_BUS_ELEM_STATE_CHANGES
            LOG_I(MODULE_PREFIX, "updateBusElemState addr@slot+1 %s isResponding %d isNewStatusChange %d isOnMainBus %s", 
                        addrAndSlot.toString().c_str(), elemResponding,
                        isNewStatusChange, 
                        (isNewStatusChange && isOnline && (addrAndSlot.slotPlus1 == 0)) ? "Y" : "N");
#endif

#ifdef DEBUG_CONSECUTIVE_ERROR_HANDLING
            // Debug
            newStatus = *pAddrStatus;
#endif
        }

        // Update status change
        if (isNewStatusChange)
        {
            _busElemStatusChangeDetected = true;
            _lastBusElemOnlineStatusUpdateTimeUs = micros();
        }

        // Check for spurious record detected
        if (flagSpuriousRecord)
        {
            // Remove the record
            _i2cAddrStatus.erase(std::remove_if(_i2cAddrStatus.begin(), _i2cAddrStatus.end(), 
                [addrAndSlot](BusI2CAddrStatus& addrStatus) { return addrStatus.addrAndSlot == addrAndSlot; }), 
                _i2cAddrStatus.end());
        }

        // Return semaphore
        xSemaphoreGive(_busElemStatusMutex);

#ifdef DEBUG_CONSECUTIVE_ERROR_HANDLING
#ifdef DEBUG_CONSECUTIVE_ERROR_HANDLING_ADDR
        if (addrAndSlot.addr == DEBUG_CONSECUTIVE_ERROR_HANDLING_ADDR)
#endif
        if (isNewStatusChange)
        {
            LOG_I(MODULE_PREFIX, "updateBusElemState addr@slot+1 %s count %d(was %d) isOnline %d(was %d) isNewStatusChange %d(was %d) wasOnline %d(was %d) isResponding %d",
                        newStatus.addrAndSlot.toString().c_str(),
                        newStatus.count, prevStatus.count, 
                        newStatus.isOnline, prevStatus.isOnline, 
                        newStatus.isChange, prevStatus.isChange, 
                        newStatus.wasOnline, prevStatus.wasOnline, 
                        elemResponding);
        }
#endif
    }
    else
    {
#ifdef WARN_ON_FAILED_TO_GET_SEMAPHORE
        LOG_E(MODULE_PREFIX, "updateBusElemState addr@slot+1 %s failed to obtain semaphore", 
                            addrAndSlot.toString().c_str());
#endif
    }

    return isNewStatusChange;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Check bus element is online
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

BusOperationStatus BusStatusMgr::isElemOnline(BusI2CAddrAndSlot addrAndSlot) const
{
#ifdef DEBUG_NO_SCANNING
    return BUS_OPERATION_OK;
#endif
    if ((addrAndSlot.addr < I2C_BUS_ADDRESS_MIN) || (addrAndSlot.addr > I2C_BUS_ADDRESS_MAX))
        return BUS_OPERATION_UNKNOWN;
        
    // Obtain semaphore controlling access to busElemChange list and flag
    BusOperationStatus onlineStatus = BUS_OPERATION_UNKNOWN;
    if (xSemaphoreTake(_busElemStatusMutex, pdMS_TO_TICKS(1)) == pdTRUE)
    {
        // Find address record
        const BusI2CAddrStatus* pAddrStatus = findAddrStatusRecord(addrAndSlot);
        if (!pAddrStatus || !pAddrStatus->wasOnline)
            onlineStatus = BUS_OPERATION_UNKNOWN;
        else
            onlineStatus = pAddrStatus->isOnline ? BUS_OPERATION_OK : BUS_OPERATION_FAILING;
        // Return semaphore
        xSemaphoreGive(_busElemStatusMutex);
    }
    return onlineStatus;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Get count of address status records
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

uint32_t BusStatusMgr::getAddrStatusCount() const
{
    // Obtain semaphore
    if (xSemaphoreTake(_busElemStatusMutex, pdMS_TO_TICKS(1)) != pdTRUE)
        return 0;

    // Get count
    uint32_t count = _i2cAddrStatus.size();

    // Return semaphore
    xSemaphoreGive(_busElemStatusMutex);
    return count;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Check if address is already detected on an extender
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool BusStatusMgr::isAddrFoundOnAnyExtender(uint32_t addr) const
{
    // Obtain semaphore
    if (xSemaphoreTake(_busElemStatusMutex, pdMS_TO_TICKS(1)) != pdTRUE)
        return false;

    // Check if address is found
    bool rslt = false;
    for (const BusI2CAddrStatus& addrStatus : _i2cAddrStatus)
    {
        if ((addrStatus.addrAndSlot.addr == addr) && 
                (addrStatus.addrAndSlot.slotPlus1 != 0))
        {
            rslt = true;
            break;
        }
    }

    // Return semaphore
    xSemaphoreGive(_busElemStatusMutex);
    return rslt;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Bus element access barring
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void BusStatusMgr::barElemAccessSet(uint32_t timeNowMs, BusI2CAddrAndSlot addrAndSlot, uint32_t barAccessAfterSendMs)
{
    // Obtain semaphore
    if (xSemaphoreTake(_busElemStatusMutex, pdMS_TO_TICKS(1)) != pdTRUE)
        return;

    // Find address record
    BusI2CAddrStatus* pAddrStatus = findAddrStatusRecordEditable(addrAndSlot);
    if (pAddrStatus)
    {
        // Set access barring
        pAddrStatus->barStartMs = timeNowMs;
        pAddrStatus->barDurationMs = barAccessAfterSendMs;
    }

    // Return semaphore
    xSemaphoreGive(_busElemStatusMutex);

#ifdef DEBUG_ACCESS_BARRING_FOR_MS
    LOG_W(MODULE_PREFIX, "i2cSendHelper %s barring bus access for addr@slot+1 %s for %dms",
                    pAddrStatus ? "OK" : "FAIL", 
                    addrAndSlot.toString().c_str(), barAccessAfterSendMs);
#endif
}

bool BusStatusMgr::barElemAccessGet(uint32_t timeNowMs, BusI2CAddrAndSlot addrAndSlot)
{
    // Obtain semaphore
    if (xSemaphoreTake(_busElemStatusMutex, pdMS_TO_TICKS(1)) != pdTRUE)
        return false;

    // Find address record
    bool accessBarred = false;
    BusI2CAddrStatus* pAddrStatus = findAddrStatusRecordEditable(addrAndSlot);

    // Check if access is barred
    if (pAddrStatus && pAddrStatus->barDurationMs != 0)
    {
        // Check if time has elapsed and ignore request if not
        if (Raft::isTimeout(timeNowMs, pAddrStatus->barStartMs, pAddrStatus->barDurationMs))
        {
#ifdef DEBUG_ACCESS_BARRING_FOR_MS
            LOG_W(MODULE_PREFIX, "i2cSendHelper access barred for addr@slot+1 %s for %ldms", 
                            addrAndSlot.toString().c_str(), pAddrStatus->barDurationMs);
#endif
            accessBarred = true;
        }
#ifdef DEBUG_ACCESS_BARRING_FOR_MS
        LOG_W(MODULE_PREFIX, "i2cSendHelper access bar released for addr@slot+1 %s for %ldms", 
                            addrAndSlot.toString().c_str(), pAddrStatus->barDurationMs);
#endif
        pAddrStatus->barDurationMs = 0;
    }

    // Return semaphore
    xSemaphoreGive(_busElemStatusMutex);
    return accessBarred;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Set bus element device status (which includes device type and can be empty) for an address
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void BusStatusMgr::setBusElemDeviceStatus(BusI2CAddrAndSlot addrAndSlot, const DeviceStatus& deviceStatus)
{
    // Obtain sempahore
    if (xSemaphoreTake(_busElemStatusMutex, pdMS_TO_TICKS(1)) != pdTRUE)
        return;

    // Find address record
    BusI2CAddrStatus* pAddrStatus = findAddrStatusRecordEditable(addrAndSlot);
    if (pAddrStatus)
    {
        // Set device type
        pAddrStatus->deviceStatus = deviceStatus;
    }

    // Return semaphore
    xSemaphoreGive(_busElemStatusMutex);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get device type index by address
/// @param addrAndSlot address and slot of device
/// @return device type index
uint16_t BusStatusMgr::getDeviceTypeIndexByAddr(BusI2CAddrAndSlot addrAndSlot) const
{
    // Obtain semaphore
    if (xSemaphoreTake(_busElemStatusMutex, pdMS_TO_TICKS(1)) != pdTRUE)
        return DeviceStatus::DEVICE_TYPE_INDEX_INVALID;

    // Find address record
    uint16_t deviceTypeIndex = DeviceStatus::DEVICE_TYPE_INDEX_INVALID;
    const BusI2CAddrStatus* pAddrStatus = findAddrStatusRecord(addrAndSlot);
    if (pAddrStatus)
    {
        // Get device type index
        deviceTypeIndex = pAddrStatus->deviceStatus.getDeviceTypeIndex();
    }

    // Return semaphore
    xSemaphoreGive(_busElemStatusMutex);
    return deviceTypeIndex;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Get pending ident poll requests for a single device
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool BusStatusMgr::getPendingIdentPoll(uint64_t timeNowUs, DevicePollingInfo& pollInfo)
{
    // Obtain semaphore
    if (xSemaphoreTake(_busElemStatusMutex, pdMS_TO_TICKS(1)) != pdTRUE)
        return false;

    // Check for any pending requests
    for (BusI2CAddrStatus& addrStatus : _i2cAddrStatus)
    {
        // Check if a poll is due
        if (addrStatus.deviceStatus.getPendingIdentPollInfo(timeNowUs, pollInfo))
        {
            // Return semaphore
            xSemaphoreGive(_busElemStatusMutex);
            return true;
        }
    }

    // Return semaphore
    xSemaphoreGive(_busElemStatusMutex);
    return false;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Store poll results
/// @param timeNowUs time in us (passed in to aid testing)
/// @param pollInfo device polling info 
/// @param addrAndSlot address and slot of device
/// @param pollResultData poll result data
/// @return true if result stored
bool BusStatusMgr::pollResultStore(uint64_t timeNowUs, const DevicePollingInfo& pollInfo, 
                        BusI2CAddrAndSlot addrAndSlot, const std::vector<uint8_t>& pollResultData)
{
    // Obtain semaphore
    if (xSemaphoreTake(_busElemStatusMutex, pdMS_TO_TICKS(1)) != pdTRUE)
        return false;

    // Find address record
    BusI2CAddrStatus* pAddrStatus = findAddrStatusRecordEditable(addrAndSlot);
    bool putResult = false;
    if (pAddrStatus)
    {
        // Add result to aggregator
        putResult = pAddrStatus->deviceStatus.pollResultStore(timeNowUs, pollInfo, pollResultData);
    }

    // Store time of last status update
    _lastIdentPollUpdateTimeUs = timeNowUs;

    // Return semaphore
    xSemaphoreGive(_busElemStatusMutex);
    return putResult;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get last status update time
/// @param includeElemOnlineStatusChanges include changes in online status of elements
/// @param includePollDataUpdates include updates from polling data
/// @return last update time
uint64_t BusStatusMgr::getLastStatusUpdateMs(bool includeElemOnlineStatusChanges, bool includePollDataUpdates) const
{
    // Obtain semaphore
    if (xSemaphoreTake(_busElemStatusMutex, pdMS_TO_TICKS(1)) != pdTRUE)
        return 0;

    // Get last update time
    uint64_t lastUpdateTimeUs = 0;
    if (includeElemOnlineStatusChanges)
        lastUpdateTimeUs = lastUpdateTimeUs > _lastBusElemOnlineStatusUpdateTimeUs ? lastUpdateTimeUs : _lastBusElemOnlineStatusUpdateTimeUs;
    if (includePollDataUpdates)
        lastUpdateTimeUs = lastUpdateTimeUs > _lastIdentPollUpdateTimeUs ? lastUpdateTimeUs : _lastIdentPollUpdateTimeUs;
    
    // Return semaphore
    xSemaphoreGive(_busElemStatusMutex);
    return lastUpdateTimeUs/1000;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Return addresses of devices attached to the bus
/// @param addresses - vector to store the addresses of devices
/// @param onlyAddressesWithIdentPollResponses - true to only return addresses with ident poll responses
/// @return true if there are any ident poll responses available
bool BusStatusMgr::getBusElemAddresses(std::vector<uint32_t>& addresses, bool onlyAddressesWithIdentPollResponses)
{
    // Obtain semaphore
    if (xSemaphoreTake(_busElemStatusMutex, pdMS_TO_TICKS(1)) != pdTRUE)
        return false;

    // Iterate address status records
    for (BusI2CAddrStatus& addrStatus : _i2cAddrStatus)
    {
        bool includeAddr = !onlyAddressesWithIdentPollResponses || addrStatus.deviceStatus.dataAggregator.count() > 0;
        if (includeAddr)
        {
            // Add address to list
            addresses.push_back(addrStatus.addrAndSlot.toCompositeAddrAndSlot());
        }
    }

    // Return semaphore
    xSemaphoreGive(_busElemStatusMutex);
    return addresses.size() > 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////    
/// @brief Get bus element status for a specific address
/// @param address - address of device to get responses for
/// @param isOnline - (out) true if device is online
/// @param deviceTypeIndex - (out) device type index
/// @param devicePollResponseData - (out) vector to store the device poll response data
/// @param responseSize - (out) size of the response data
/// @param maxResponsesToReturn - maximum number of responses to return (0 for no limit)
/// @return number of responses returned
uint32_t BusStatusMgr::getBusElemStatus(uint32_t address, bool& isOnline, uint16_t& deviceTypeIndex, 
            std::vector<uint8_t>& devicePollResponseData, 
            uint32_t& responseSize, uint32_t maxResponsesToReturn)
{
    // Obtain semaphore
    if (xSemaphoreTake(_busElemStatusMutex, pdMS_TO_TICKS(1)) != pdTRUE)
        return 0;

    // Find address record
    uint32_t numResponses = 0;
    BusI2CAddrStatus* pAddrStatus = findAddrStatusRecordEditable(BusI2CAddrAndSlot::fromCompositeAddrAndSlot(address));
    if (pAddrStatus)
    {
        // Elem online
        isOnline = pAddrStatus->isOnline;
        
        // Device type index
        deviceTypeIndex = pAddrStatus->deviceStatus.getDeviceTypeIndex();

        // Get results from aggregator
        numResponses = pAddrStatus->deviceStatus.dataAggregator.get(devicePollResponseData, responseSize, maxResponsesToReturn);
    }

    // Return semaphore
    xSemaphoreGive(_busElemStatusMutex);
    return numResponses;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get bus status json
/// @param deviceIdentMgr device identity manager
/// @return bus status json
String BusStatusMgr::getBusStatusJson(DeviceIdentMgr& deviceIdentMgr)
{
    // Return string
    String jsonStr;

    // Get list of all bus element addresses
    std::vector<uint32_t> addresses;
    getBusElemAddresses(addresses, false);
    for (uint32_t address : addresses)
    {
        // Get bus status for each address
        bool isOnline = false;
        uint16_t deviceTypeIndex = 0;
        std::vector<uint8_t> devicePollResponseData;
        uint32_t responseSize = 0;
        getBusElemStatus(address, isOnline, deviceTypeIndex, devicePollResponseData, responseSize, 0);

        // Use device identity manager to convert to JSON
        String jsonData = deviceIdentMgr.deviceStatusToJson(BusI2CAddrAndSlot::fromCompositeAddrAndSlot(address), 
                        isOnline, deviceTypeIndex, devicePollResponseData, responseSize);
        if (jsonData.length() > 0)
        {
            jsonStr += (jsonStr.length() == 0 ? "{" : ",") + jsonData;
        }
    }
    return jsonStr.length() == 0 ? "{}" : jsonStr + "}";
}
