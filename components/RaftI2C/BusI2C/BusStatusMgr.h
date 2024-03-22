/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// I2C Bus Status Manager
//
// Rob Dobson 2020-2024
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "BusBase.h"
#include "BusI2CConsts.h"

class BusStatusMgr {

public:
    BusStatusMgr(BusBase& busBase) :
        _busBase(busBase)
    {
        // Bus element status change detection
        _busElemStatusMutex = xSemaphoreCreateMutex();
    }

    ~BusStatusMgr()
    {
        if (_busElemStatusMutex)
            vSemaphoreDelete(_busElemStatusMutex);
    }

    void setup(const RaftJsonIF& config);

    void service(bool hwIsOperatingOk);

    BusOperationStatus isOperatingOk() const
    {
        return _busOperationStatus;
    }

    // Check if element is responding
    bool isElemResponding(uint32_t address, bool* pIsValid);

    // Handle bus element state changes
    void handleBusElemStateChanges(uint32_t address, bool elemResponding);

    // Max failures before declaring a bus element offline
    static const uint32_t I2C_ADDR_RESP_COUNT_FAIL_MAX = 3;

    // Max successes before declaring a bus element online
    static const uint32_t I2C_ADDR_RESP_COUNT_OK_MAX = 2;

private:
    // Bus element status change mutex
    SemaphoreHandle_t _busElemStatusMutex = nullptr;

    // I2C address response status
    class I2CAddrRespStatus
    {
    public:
        I2CAddrRespStatus()
        {
            clear();
        }
        void clear()
        {
            count = 0;
            isChange = false;
            isOnline = false;
            isValid = false;
        }
        uint8_t count : 5;
        bool isChange : 1;
        bool isOnline : 1;
        bool isValid : 1;
    };

    // I2C address response status
    I2CAddrRespStatus _i2cAddrResponseStatus[I2C_BUS_ADDRESS_MAX+1];

    // Address for lockup detect
    uint8_t _addrForLockupDetect = 0;
    bool _addrForLockupDetectValid = false;

    // Bus operation status
    BusOperationStatus _busOperationStatus = BUS_OPERATION_UNKNOWN;

    // Bus base
    BusBase& _busBase;

    // Bus element status change detection
    bool _busElemStatusChangeDetected = false;
};