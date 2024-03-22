/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// I2C Bus Status Manager
//
// Rob Dobson 2020-2024
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "BusStatusMgr.h"
#include "Logger.h"

// #define DEBUG_CONSECUTIVE_ERROR_HANDLING
// #define DEBUG_CONSECUTIVE_ERROR_HANDLING_ADDR 0x55
// #define WARN_ON_FAILED_TO_GET_SEMAPHORE
// #define DEBUG_LOCKUP_DETECTION
// #define DEBUG_NO_SCANNING
// #define DEBUG_SERVICE_BUS_ELEM_STATUS_CHANGE

static const char* MODULE_PREFIX = "BusStatusMgr";

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

    // Debug
    LOG_I(MODULE_PREFIX, "task lockupDetect addr %02x (valid %s)",
                _addrForLockupDetect, _addrForLockupDetectValid ? "Y" : "N");

}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Service
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void BusStatusMgr::service(bool hwIsOperatingOk)
{
    // Obtain semaphore controlling access to busElemChange list and flag
    // so we can update bus and element operation status - don't worry if we can't
    // access the list as there will be other service loops
    std::vector<BusElemAddrAndStatus> statusChanges;
    BusOperationStatus newBusOperationStatus = _busOperationStatus; 
    if (xSemaphoreTake(_busElemStatusMutex, 0) == pdTRUE)
    {    
        // Check for any changes
        if (_busElemStatusChangeDetected)
        {
            // Go through once and look for changes
            uint32_t numChanges = 0;
            for (uint16_t i = 0; i <= I2C_BUS_ADDRESS_MAX; i++)
            {
                if (_i2cAddrResponseStatus[i].isChange)
                    numChanges++;
            }

            // Create change vector if required
            if (numChanges > 0)
            {
                statusChanges.reserve(numChanges);
                for (uint16_t i2cAddr = 0; i2cAddr <= I2C_BUS_ADDRESS_MAX; i2cAddr++)
                {
                    if (_i2cAddrResponseStatus[i2cAddr].isChange)
                    {
                        // Handle element change
                        BusElemAddrAndStatus statusChange = 
                            {
                                i2cAddr, 
                                _i2cAddrResponseStatus[i2cAddr].isOnline
                            };
#ifdef DEBUG_SERVICE_BUS_ELEM_STATUS_CHANGE
                        LOG_I(MODULE_PREFIX, "service addr 0x%02x status change to %s", 
                                    i2cAddr, statusChange.isChangeToOnline ? "online" : "offline");
#endif
                        statusChanges.push_back(statusChange);
                        _i2cAddrResponseStatus[i2cAddr].isChange = false;

                        // Check if this is the addrForLockupDetect
                        if (_addrForLockupDetectValid && (i2cAddr == _addrForLockupDetect) && (_i2cAddrResponseStatus[i2cAddr].isValid))
                        {
                            newBusOperationStatus = _i2cAddrResponseStatus[i2cAddr].isOnline ?
                                        BUS_OPERATION_OK : BUS_OPERATION_FAILING;
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
            _busOperationStatus = newBusOperationStatus;
            _busBase.callBusOperationStatusCB(_busOperationStatus);
        }
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Bus element state changes
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void BusStatusMgr::handleBusElemStateChanges(uint32_t address, bool elemResponding)
{
    LOG_I(MODULE_PREFIX, "handleBusElemStateChanges addr %02x isResponding %d", address, elemResponding);

    // Check valid
    if (address > I2C_BUS_ADDRESS_MAX)
        return;

    // Obtain semaphore controlling access to busElemChange list and flag
    if (xSemaphoreTake(_busElemStatusMutex, pdMS_TO_TICKS(1)) == pdTRUE)
    {    
        // Init state change detection
        uint8_t count = _i2cAddrResponseStatus[address].count;
        bool isOnline = _i2cAddrResponseStatus[address].isOnline;
        bool isChange = _i2cAddrResponseStatus[address].isChange;
        bool isValid = _i2cAddrResponseStatus[address].isValid;

        // Check coming online - the count must reach ok level to indicate online
        // If a change is detected then we toggle the change indicator - if it was already
        // set then we must have a double-change which is equivalent to no change
        // We also set the _busElemStatusChangedDetected even if there might have been a
        // double-change as it is not safe to clear it because there may be other changes
        // - the service loop will sort out what has really happened
        if (elemResponding && !isOnline)
        {
            count = (count < I2C_ADDR_RESP_COUNT_OK_MAX) ? count+1 : count;
            if (count >= I2C_ADDR_RESP_COUNT_OK_MAX)
            {
                // Now online
                isChange = !isChange;
                count = 0;
                isOnline = true;
                isValid = true;
                _busElemStatusChangeDetected = true;
            }
        }
        else if (!elemResponding && (isOnline || !isValid))
        {
            // Bump the failure count indicator and check if we reached failure level
            count = (count < I2C_ADDR_RESP_COUNT_FAIL_MAX) ? count+1 : count;
            if (count >= I2C_ADDR_RESP_COUNT_FAIL_MAX)
            {
                // Now offline 
                isChange = !isChange;
                isOnline = false;
                isValid = true;
                _busElemStatusChangeDetected = true;
            }
        }

#ifdef DEBUG_CONSECUTIVE_ERROR_HANDLING
        uint32_t prevCount = count;
        bool prevOnline = isOnline;
        bool prevChange = isChange;
        bool prevValid = isValid;
#endif
        // Update status
        _i2cAddrResponseStatus[address].count = count;
        _i2cAddrResponseStatus[address].isOnline = isOnline;
        _i2cAddrResponseStatus[address].isChange = isChange;
        _i2cAddrResponseStatus[address].isValid = isValid;

        // Return semaphore
        xSemaphoreGive(_busElemStatusMutex);

#ifdef DEBUG_CONSECUTIVE_ERROR_HANDLING
#ifdef DEBUG_CONSECUTIVE_ERROR_HANDLING_ADDR
        if (address == DEBUG_CONSECUTIVE_ERROR_HANDLING_ADDR)
#endif
        // if (isChange)
        {
            LOG_I(MODULE_PREFIX, "handleBusElemStateChanges addr %02x failCount %d(was %d) isOnline %d(was %d) isChange %d(was %d) isValid %d(was %d) isResponding %d",
                        address, count, prevCount, isOnline, prevOnline, isChange, prevChange, isValid, prevValid, elemResponding);
        }
#endif
    }
    else
    {
#ifdef WARN_ON_FAILED_TO_GET_SEMAPHORE
        LOG_E(MODULE_PREFIX, "handleBusElemStateChanges addr %02x failed to obtain semaphore", address);
#endif
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Check bus element is responding
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool BusStatusMgr::isElemResponding(uint32_t address, bool* pIsValid)
{
#ifdef DEBUG_NO_SCANNING
    return true;
#endif
    if (pIsValid)
        *pIsValid = false;
    if (address > I2C_BUS_ADDRESS_MAX)
        return false;
        
    // Obtain semaphore controlling access to busElemChange list and flag
    bool isOnline = false;
    if (xSemaphoreTake(_busElemStatusMutex, pdMS_TO_TICKS(1)) == pdTRUE)
    {    
        isOnline = _i2cAddrResponseStatus[address].isOnline;
        if (pIsValid)
            *pIsValid = _i2cAddrResponseStatus[address].isValid;

        // Return semaphore
        xSemaphoreGive(_busElemStatusMutex);
    }
    return isOnline;
}
