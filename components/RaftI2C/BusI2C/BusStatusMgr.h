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
#include "BusExtenderMgr.h"
#include "DeviceIdent.h"
#include "DevicePollingInfo.h"
#include "DevInfoRec.h"
#include <list>

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
    void barElemAccessSet(uint32_t timeNowMs, RaftI2CAddrAndSlot addrAndSlot, uint32_t barAccessAfterSendMs);
    bool barElemAccessGet(uint32_t timeNowMs, RaftI2CAddrAndSlot addrAndSlot);

    // Check if element is online
    BusOperationStatus isElemOnline(RaftI2CAddrAndSlot addrAndSlot);

    // Update bus element state
    // Returns true if state has changed
    bool updateBusElemState(RaftI2CAddrAndSlot addrAndSlot, bool elemResponding, bool& isOnline);

    // Get count of address status records
    uint32_t getAddrStatusCount() const;

    // Check if address is already detected on an extender
    bool isAddrFoundOnAnyExtender(uint32_t addr);

    // Set bus element device info (which can be null) for address
    void setBusElemDevInfo(RaftI2CAddrAndSlot addrAndSlot, const DevInfoRec* pDevInfoRec);

    // Get pending bus request
    bool getPendingBusRequestsForOneDevice(uint32_t timeNowMs, std::vector<BusI2CRequestRec>& busReqRecs);

    // Max failures before declaring a bus element offline
    static const uint32_t I2C_ADDR_RESP_COUNT_FAIL_MAX = 3;

    // Max successes before declaring a bus element online
    static const uint32_t I2C_ADDR_RESP_COUNT_OK_MAX = 2;

private:
    // Bus element status change mutex
    SemaphoreHandle_t _busElemStatusMutex = nullptr;

    // Bus base
    BusBase& _busBase;

    // I2C address status
    class I2CAddrStatus
    {
    public:
        // Address and slot
        RaftI2CAddrAndSlot addrAndSlot;

        // Online/offline count
        int8_t count = 0;

        // State
        bool isChange : 1 = false;
        bool isOnline : 1 = false;
        bool wasOnline: 1 = false;
        bool slotResolved: 1 = false;

        // Access barring
        uint32_t barStartMs = 0;
        uint16_t barDurationMs = 0;

        // Device ident
        DeviceIdent deviceIdent;

        // Device polling
        DevicePollingInfo devicePollingInfo;

        // Handle responding
        bool handleResponding(bool isResponding, bool& flagSpuriousRecord)
        {
            // Handle is responding or not
            if (isResponding)
            {
                // If not already online then count upwards
                if (!isOnline)
                {
                    // Check if we've reached the threshold for online
                    count = (count < I2C_ADDR_RESP_COUNT_OK_MAX) ? count+1 : count;
                    if (count >= I2C_ADDR_RESP_COUNT_OK_MAX)
                    {
                        // Now online
                        isChange = !isChange;
                        count = 0;
                        isOnline = true;
                        wasOnline = true;
                        return true;
                    }
                }
            }
            else
            {
                // Not responding - check for change to offline
                if (isOnline || !wasOnline)
                {
                    // Count down to offline/spurious threshold
                    count = (count < -I2C_ADDR_RESP_COUNT_FAIL_MAX) ? count : count-1;
                    if (count <= -I2C_ADDR_RESP_COUNT_FAIL_MAX)
                    {
                        // Now offline/spurious
                        count = 0;
                        if (!wasOnline)
                            flagSpuriousRecord = true;
                        else
                            isChange = !isChange;
                        isOnline = false;
                        return true;
                    }
                }
            }
            return false;
        }
    };

    // I2C address status
    std::vector<I2CAddrStatus> _i2cAddrStatus;
    static const uint32_t I2C_ADDR_STATUS_MAX = 50;

    // Find address record
    // Assumes semaphore already taken
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

    // Bus element status change detection
    bool _busElemStatusChangeDetected = false;
};
