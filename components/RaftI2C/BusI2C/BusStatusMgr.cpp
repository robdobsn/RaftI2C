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
                    LOG_I(MODULE_PREFIX, "service addr 0x%02x slot+1 %d status change to %s", 
                                addrStatus.addrAndSlot.addr,
                                addrStatus.addrAndSlot.slotPlus1,
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
// Bus element state changes
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void BusStatusMgr::handleBusElemStateChanges(RaftI2CAddrAndSlot addrAndSlot, bool elemResponding)
{
#ifdef DEBUG_HANDLE_BUS_ELEM_STATE_CHANGES
    LOG_I(MODULE_PREFIX, "handleBusElemStateChanges addr 0x%02x slot+1 %d isResponding %d", 
                        addrAndSlot.addr, addrAndSlot.slotPlus1, elemResponding);
#endif

    // Obtain semaphore controlling access to busElemChange list and flag
    if (xSemaphoreTake(_busElemStatusMutex, pdMS_TO_TICKS(1)) == pdTRUE)
    {
#ifdef DEBUG_CONSECUTIVE_ERROR_HANDLING
        // Debug
        I2CAddrStatus prevStatus;
        I2CAddrStatus newStatus;
#endif

        // Check for new status change
        bool isNewStatusChange = false;
        bool flagSpuriousRecord = false;

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
            // TODO remove
            LOG_I(MODULE_PREFIX, "handleBusElemStateChanges new addr 0x%02x slot+1 %d", 
                        addrAndSlot.addr, addrAndSlot.slotPlus1)
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

#ifdef DEBUG_HANDLE_BUS_ELEM_STATE_CHANGES
            LOG_I(MODULE_PREFIX, "handleBusElemStateChanges addr 0x%02x slot+1 %d isResponding %d isNewStatusChange %d", 
                        addrAndSlot.addr, addrAndSlot.slotPlus1, elemResponding,
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
        // if (isNewStatusChange)
        {
            LOG_I(MODULE_PREFIX, "handleBusElemStateChanges addr 0x%02x slot+1 %d count %d(was %d) isOnline %d(was %d) isNewStatusChange %d(was %d) wasOnline %d(was %d) isResponding %d",
                        newStatus.addrAndSlot.addr, newStatus.addrAndSlot.slotPlus1,
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
        LOG_E(MODULE_PREFIX, "handleBusElemStateChanges addr 0x%02x slot+1 %d failed to obtain semaphore", 
                            addrAndSlot.addr, addrAndSlot.slotPlus1);
#endif
    }
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
// Bus element access barring
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void BusStatusMgr::barElemAccessSet(RaftI2CAddrAndSlot addrAndSlot, uint32_t barAccessAfterSendMs)
{
    // Find address record
    I2CAddrStatus* pAddrStatus = findAddrStatusRecord(addrAndSlot);
    if (!pAddrStatus)
        return;

    // Set access barring
    pAddrStatus->barStartMs = millis();
    pAddrStatus->barDurationMs = barAccessAfterSendMs;
#ifdef DEBUG_ACCESS_BARRING_FOR_MS
    LOG_W(MODULE_PREFIX, "i2cSendHelper barring bus access for address 0x%02x slot %d for %dms", 
                    addrAndSlot.addr, addrAndSlot.slotPlus1, barAccessAfterSendMs);
#endif

}

bool BusStatusMgr::barElemAccessGet(RaftI2CAddrAndSlot addrAndSlot)
{
    // Find address record
    I2CAddrStatus* pAddrStatus = findAddrStatusRecord(addrAndSlot);
    if (!pAddrStatus)
        return false;

    // Check if access is barred
    if (pAddrStatus->barDurationMs != 0)
    {
        // Check if time has elapsed and ignore request if not
        if (Raft::isTimeout(millis(), pAddrStatus->barStartMs, pAddrStatus->barDurationMs))
        {
#ifdef DEBUG_ACCESS_BARRING_FOR_MS
            LOG_W(MODULE_PREFIX, "i2cSendHelper access barred for address 0x%02x slot+1 %d for %ldms", 
                            addrAndSlot.addr, addrAndSlot.slotPlus1, pAddrStatus->barDurationMs);
#endif
            return true;
        }
#ifdef DEBUG_ACCESS_BARRING_FOR_MS
        LOG_W(MODULE_PREFIX, "i2cSendHelper access bar released for address 0x%02x slot %d for %ldms", 
                            addrAndSlot.addr, addrAndSlot.slotPlus1, pAddrStatus->barDurationMs);
#endif
        pAddrStatus->barDurationMs = 0;
    }
    return false;
}
