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
    // Constructor and destructor
    BusStatusMgr(BusBase& busBase);
    ~BusStatusMgr();

    // Setup & service
    void setup(const RaftJsonIF& config);
    void service(bool hwIsOperatingOk);

    // Get bus operation status
    BusOperationStatus isOperatingOk() const
    {
        return _busOperationStatus;
    }

    // Bus element access barring
    void barElemAccessSet(RaftI2CAddrAndSlot addrAndSlot, uint32_t barAccessAfterSendMs);
    bool barElemAccessGet(RaftI2CAddrAndSlot addrAndSlot);

    // Scan rate setting


    // Check if element is online
    BusOperationStatus isElemOnline(RaftI2CAddrAndSlot addrAndSlot);

    // Handle bus element state changes
    void handleBusElemStateChanges(RaftI2CAddrAndSlot addrAndSlot, bool elemResponding);

    // Max failures before declaring a bus element offline
    static const uint32_t I2C_ADDR_RESP_COUNT_FAIL_MAX = 3;

    // Max successes before declaring a bus element online
    static const uint32_t I2C_ADDR_RESP_COUNT_OK_MAX = 2;

private:
    // Bus element status change mutex
    SemaphoreHandle_t _busElemStatusMutex = nullptr;

    // I2C address status
    class I2CAddrStatus
    {
    public:
        RaftI2CAddrAndSlot addrAndSlot;
        uint8_t count : 5 = 0;
        bool isChange : 1 = false;
        bool isOnline : 1 = false;
        bool isValid : 1 = false;
        uint32_t barStartMs = 0;
        uint16_t barDurationMs = 0;

        bool handleIsResponding()
        {
            printf("---- handleIsResponding addr %02x slot %d count %d\n", addrAndSlot.addr, addrAndSlot.slotPlus1, count);
            count = (count < I2C_ADDR_RESP_COUNT_OK_MAX) ? count+1 : count;
            if (count >= I2C_ADDR_RESP_COUNT_OK_MAX)
            {
                // Now online
                isChange = !isChange;
                count = 0;
                isOnline = true;
                isValid = true;
                return true;
            }
            return false;
        }
        bool handleNotResponding()
        {
            // Bump the failure count indicator and check if we reached failure level
            count = (count < I2C_ADDR_RESP_COUNT_FAIL_MAX) ? count+1 : count;
            if (count >= I2C_ADDR_RESP_COUNT_FAIL_MAX)
            {
                // Now offline
                isChange = !isChange;
                isOnline = false;
                isValid = true;
                return true;
            }
            return false;
        }
    };

    // I2C address status
    std::vector<I2CAddrStatus> _i2cAddrStatus;
    static const uint32_t I2C_ADDR_STATUS_MAX = 50;

    // Find address record
    I2CAddrStatus* findAddrStatusRecord(RaftI2CAddrAndSlot addrAndSlot)
    {
        for (I2CAddrStatus& addrStatus : _i2cAddrStatus)
        {
            if ((addrStatus.addrAndSlot.addr == addrAndSlot.addr) && 
                    (addrStatus.addrAndSlot.slotPlus1 == addrAndSlot.slotPlus1))
                return &addrStatus;
        }
        return nullptr;
    }

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