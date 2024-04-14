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
#include "RaftUtils.h"
#include "DeviceStatus.h"
#include "BusI2CAddrStatus.h"
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

    // Set bus element device status (which includes device type and can be empty) for an address
    void setBusElemDeviceStatus(RaftI2CAddrAndSlot addrAndSlot, const DeviceStatus& deviceStatus);

    // Get pending ident poll
    bool getPendingIdentPoll(uint32_t timeNowMs, DevicePollingInfo& pollInfo);

    // Store poll results
    bool pollResultStore(uint32_t timeNowMs, const DevicePollingInfo& pollInfo, RaftI2CAddrAndSlot addrAndSlot, const std::vector<uint8_t>& pollResultData);

    /// @brief Get ident poll last update time ms
    /// @return ident poll last update time ms
    uint32_t getIdentPollLastUpdateMs() const;

    /// @brief Check if any ident poll responses are available and, if so, return addresses of devices that have responded
    /// @param addresses - vector to store the addresses of devices that have responded
    /// @return true if there are any ident poll responses available
    bool pollResponseAddresses(std::vector<uint32_t>& addresses);

    /// @brief Get ident poll responses
    /// @param address - address of device to get responses for
    /// @param devicePollResponseData - vector to store the device poll responses
    /// @param responseSize - (out) size of the response data
    /// @param deviceTypeIndex - (out) index of the device type
    /// @param maxResponsesToReturn - maximum number of responses to return (0 for no limit)
    /// @return number of poll responses returned
    uint32_t pollResponsesGet(uint32_t address, std::vector<uint8_t>& devicePollResponseData, 
                uint32_t& responseSize, uint16_t& deviceTypeIndex, uint32_t maxResponsesToReturn);

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
    std::vector<BusI2CAddrStatus> _i2cAddrStatus;
    static const uint32_t I2C_ADDR_STATUS_MAX = 50;

    // Ident poll last update time ms
    uint32_t _identPollLastUpdateTimeMs = 0;

    // Find address record
    // Assumes semaphore already taken
    BusI2CAddrStatus* findAddrStatusRecord(RaftI2CAddrAndSlot addrAndSlot)
    {
        for (BusI2CAddrStatus& addrStatus : _i2cAddrStatus)
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
