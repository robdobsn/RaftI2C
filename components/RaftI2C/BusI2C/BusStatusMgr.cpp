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
#ifdef I2C_USE_RAFT_I2C
    uint32_t address = strtoul(config.getString("lockupDetect", "0xffffffff").c_str(), NULL, 0);
    if (address != 0xffffffff)
    {
        _addrForLockupDetect = address;
        _addrForLockupDetectValid = true;
    }
#endif
    _busOperationStatus = BUS_OPERATION_UNKNOWN;
    _busElemStatusChangeDetected = false;
    _i2cAddrStatus.clear();

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
                            addrStatus.isOnline
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

bool BusStatusMgr::updateBusElemState(RaftI2CAddrAndSlot addrAndSlot, bool elemResponding, bool& isOnline)
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
        I2CAddrStatus prevStatus;
        I2CAddrStatus newStatus;
#endif

        // Find address record
        I2CAddrStatus* pAddrStatus = findAddrStatusRecord(addrAndSlot);

        // If not found and element is responding then add a new record
        if ((pAddrStatus == nullptr) && elemResponding && (_i2cAddrStatus.size() < I2C_ADDR_STATUS_MAX))
        {
            // Add new record
            I2CAddrStatus newAddrStatus;
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

#ifdef DEBUG_HANDLE_BUS_ELEM_STATE_CHANGES
            LOG_I(MODULE_PREFIX, "updateBusElemState addr@slot+1 %s isResponding %d isNewStatusChange %d", 
                        addrAndSlot.toString().c_str(), elemResponding,
                        isNewStatusChange);
#endif

#ifdef DEBUG_CONSECUTIVE_ERROR_HANDLING
            // Debug
            newStatus = *pAddrStatus;
#endif
        }

        // Update status change
        if (isNewStatusChange)
            _busElemStatusChangeDetected = true;

        // Check for spurious record detected
        if (flagSpuriousRecord)
        {
            // Remove the record
            _i2cAddrStatus.erase(std::remove_if(_i2cAddrStatus.begin(), _i2cAddrStatus.end(), 
                [addrAndSlot](I2CAddrStatus& addrStatus) { return addrStatus.addrAndSlot == addrAndSlot; }), 
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

BusOperationStatus BusStatusMgr::isElemOnline(RaftI2CAddrAndSlot addrAndSlot)
{
#ifdef DEBUG_NO_SCANNING
    return BUS_OPERATION_OK;
#endif
    if (addrAndSlot.addr > I2C_BUS_ADDRESS_MAX)
        return BUS_OPERATION_UNKNOWN;
        
    // Obtain semaphore controlling access to busElemChange list and flag
    BusOperationStatus onlineStatus = BUS_OPERATION_UNKNOWN;
    if (xSemaphoreTake(_busElemStatusMutex, pdMS_TO_TICKS(1)) == pdTRUE)
    {
        // Find address record
        I2CAddrStatus* pAddrStatus = findAddrStatusRecord(addrAndSlot);
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

bool BusStatusMgr::isAddrFoundOnAnyExtender(uint32_t addr)
{
    // Obtain semaphore
    if (xSemaphoreTake(_busElemStatusMutex, pdMS_TO_TICKS(1)) != pdTRUE)
        return false;

    // Check if address is found
    bool rslt = false;
    for (I2CAddrStatus& addrStatus : _i2cAddrStatus)
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

void BusStatusMgr::barElemAccessSet(uint32_t timeNowMs, RaftI2CAddrAndSlot addrAndSlot, uint32_t barAccessAfterSendMs)
{
    // Obtain semaphore
    if (xSemaphoreTake(_busElemStatusMutex, pdMS_TO_TICKS(1)) != pdTRUE)
        return;

    // Find address record
    I2CAddrStatus* pAddrStatus = findAddrStatusRecord(addrAndSlot);
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

bool BusStatusMgr::barElemAccessGet(uint32_t timeNowMs, RaftI2CAddrAndSlot addrAndSlot)
{
    // Obtain semaphore
    if (xSemaphoreTake(_busElemStatusMutex, pdMS_TO_TICKS(1)) != pdTRUE)
        return false;

    // Find address record
    bool accessBarred = false;
    I2CAddrStatus* pAddrStatus = findAddrStatusRecord(addrAndSlot);

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
// Set bus element device info (which can be null) for address
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void BusStatusMgr::setBusElemDevInfo(RaftI2CAddrAndSlot addrAndSlot, const DevInfoRec* pDevInfo)
{
    // Obtain sempahore
    if (xSemaphoreTake(_busElemStatusMutex, pdMS_TO_TICKS(1)) != pdTRUE)
        return;

    // Find address record
    I2CAddrStatus* pAddrStatus = findAddrStatusRecord(addrAndSlot);
    if (pAddrStatus)
    {
        // Set device ident
        pAddrStatus->deviceIdent = pDevInfo ? pDevInfo->getDeviceIdent() : DeviceIdent();

        // Check if polling is required
        if (pDevInfo)
        {
            // Get polling requests
            std::vector<BusI2CRequestRec> pollRequests;
            pDevInfo->getDevicePollReqs(addrAndSlot, pollRequests);

#ifdef DEBUG_HANDLE_BUS_DEVICE_INFO
            LOG_I(MODULE_PREFIX, "setBusElemDevInfo addr@slot+1 %s numPollReqs %d", 
                    addrAndSlot.toString().c_str(),
                    pollRequests.size());
#endif

            // Set polling info
            pAddrStatus->deviceIdentPolling.set(pDevInfo->pollIntervalMs, pollRequests);
        }
    }

    // Return semaphore
    xSemaphoreGive(_busElemStatusMutex);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Get pending bus request
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool BusStatusMgr::getPendingBusRequestsForOneDevice(uint32_t timeNowMs, std::vector<BusI2CRequestRec>& busReqRecs)
{
    // Obtain semaphore
    if (xSemaphoreTake(_busElemStatusMutex, pdMS_TO_TICKS(1)) != pdTRUE)
        return false;

    // Check for any pending requests
    busReqRecs.clear();
    for (I2CAddrStatus& addrStatus : _i2cAddrStatus)
    {
        // Check if a poll is due
        if (Raft::isTimeout(timeNowMs, addrStatus.deviceIdentPolling.lastPollTimeMs, addrStatus.deviceIdentPolling.pollIntervalMs))
        {
            // Iterate polling records
            for (BusI2CRequestRec& reqRec : addrStatus.deviceIdentPolling.pollReqs)
            {
                // Append to list
                busReqRecs.push_back(reqRec);
                addrStatus.deviceIdentPolling.lastPollTimeMs = timeNowMs;
            }
        }

        // Break if we have a request (ensure all requests are for the same device)
        if (busReqRecs.size() > 0)
            break;
    }

    // Return semaphore
    xSemaphoreGive(_busElemStatusMutex);
    return busReqRecs.size() > 0;
}
