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

    // Check if element is online
    BusOperationStatus isElemOnline(RaftI2CAddrAndSlot addrAndSlot);

    // Handle bus element state changes
    void handleBusElemStateChanges(RaftI2CAddrAndSlot addrAndSlot, bool elemResponding);

    // Check if address is a bus extender
    static bool isBusExtender(uint8_t addr)
    {
        return (addr >= I2C_BUS_EXTENDER_BASE) && (addr < I2C_BUS_EXTENDER_BASE+I2C_BUS_EXTENDERS_MAX);
    }

    // Get count of address status records
    uint32_t getAddrStatusCount() const
    {
        return _i2cAddrStatus.size();
    }

    // Get count of bus extenders
    uint32_t getBusExtenderCount() const
    {
        return _busExtenders.size();
    }

    // Get list of bus extender addresses
    void getBusExtenderAddrList(std::vector<uint32_t>& busExtenderAddrList)
    {
        for (const BusExtender& busExtender : _busExtenders)
            busExtenderAddrList.push_back(busExtender.addr);
    }

    // Get address of bus extender requiring initilisation
    bool getBusExtenderAddrRequiringInit(uint32_t& addr)
    {
        for (const BusExtender& busExtender : _busExtenders)
        {
            if (!busExtender.isInitialised)
            {
                addr = busExtender.addr;
                return true;
            }
        }
        return false;
    }

    // Set bus extender as initialised
    void setBusExtenderAsInitialised(uint32_t addr)
    {
        for (BusExtender& busExtender : _busExtenders)
        {
            if (busExtender.addr == addr)
            {
                busExtender.isInitialised = true;
                return;
            }
        }
    }

    // Check if address is already detected on an extender
    bool isAddrFoundOnAnyExtender(uint32_t addr)
    {
        for (I2CAddrStatus& addrStatus : _i2cAddrStatus)
        {
            if ((addrStatus.addrAndSlot.addr == addr) && 
                    (addrStatus.addrAndSlot.slotPlus1 != 0))
                return true;
        }
        return false;
    }

    // Max failures before declaring a bus element offline
    static const uint32_t I2C_ADDR_RESP_COUNT_FAIL_MAX = 3;

    // Max successes before declaring a bus element online
    static const uint32_t I2C_ADDR_RESP_COUNT_OK_MAX = 2;

    // Bus extender slot count
    static const uint32_t I2C_BUS_EXTENDER_SLOT_COUNT = 8;

    // Bus extender channels
    static const uint32_t I2C_BUS_EXTENDER_ALL_CHANS_OFF = 0;
    static const uint32_t I2C_BUS_EXTENDER_ALL_CHANS_ON = 0xff;

private:
    // Bus element status change mutex
    SemaphoreHandle_t _busElemStatusMutex = nullptr;

    // I2C address status
    class I2CAddrStatus
    {
    public:
        RaftI2CAddrAndSlot addrAndSlot;
        int8_t count = 0;
        bool isChange : 1 = false;
        bool isOnline : 1 = false;
        bool wasOnline: 1 = false;
        bool slotResolved: 1 = false;
        uint32_t barStartMs = 0;
        uint16_t barDurationMs = 0;

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

    // Bus extenders
    class BusExtender
    {
    public:
        uint8_t addr = 0;
        bool isInitialised: 1 = false;
    };
    std::vector<BusExtender> _busExtenders;

    // Helper for finding bus extender
    BusExtender* findBusExtender(uint8_t addr)
    {
        for (BusExtender& busExtender : _busExtenders)
        {
            if (busExtender.addr == addr)
                return &busExtender;
        }
        return nullptr;
    }
};
