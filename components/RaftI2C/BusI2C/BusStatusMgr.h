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
#include "RaftUtils.h"
#include "DeviceStatus.h"
#include "BusI2CAddrStatus.h"
#include <list>

class DeviceIdentMgr;

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
    void barElemAccessSet(uint32_t timeNowMs, BusI2CAddrAndSlot addrAndSlot, uint32_t barAccessAfterSendMs);
    bool barElemAccessGet(uint32_t timeNowMs, BusI2CAddrAndSlot addrAndSlot);

    // Check if element is online
    BusOperationStatus isElemOnline(BusI2CAddrAndSlot addrAndSlot) const;

    // Update bus element state
    // Returns true if state has changed
    bool updateBusElemState(BusI2CAddrAndSlot addrAndSlot, bool elemResponding, bool& isOnline);

    // Get count of address status records
    uint32_t getAddrStatusCount() const;

    // Check if address is already detected on an extender
    bool isAddrFoundOnAnyExtender(uint32_t addr) const;

    // Set bus element device status (which includes device type and can be empty) for an address
    void setBusElemDeviceStatus(BusI2CAddrAndSlot addrAndSlot, const DeviceStatus& deviceStatus);

    // Get device type index by address
    uint16_t getDeviceTypeIndexByAddr(BusI2CAddrAndSlot addrAndSlot) const;

    // Get pending ident poll
    bool getPendingIdentPoll(uint64_t timeNowUs, DevicePollingInfo& pollInfo);

    // Store poll results
    bool pollResultStore(uint64_t timeNowUs, const DevicePollingInfo& pollInfo, BusI2CAddrAndSlot addrAndSlot, const std::vector<uint8_t>& pollResultData);

    /// @brief Get last status update time ms
    /// @param includeElemOnlineStatusChanges include changes in online status of elements
    /// @param includePollDataUpdates include updates from polling data
    /// @return last status update time ms
    uint64_t getLastStatusUpdateMs(bool includeElemOnlineStatusChanges, bool includePollDataUpdates) const;

    ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Return addresses of devices attached to the bus
    /// @param addresses - vector to store the addresses of devices
    /// @param onlyAddressesWithIdentPollResponses - true to only return addresses with ident poll responses
    /// @return true if there are any ident poll responses available
    bool getBusElemAddresses(std::vector<uint32_t>& addresses, bool onlyAddressesWithIdentPollResponses) const;

    ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////    
    /// @brief Get bus element poll responses for a specific address
    /// @param address - address of device to get responses for
    /// @param isOnline - (out) true if device is online
    /// @param deviceTypeIndex - (out) device type index
    /// @param devicePollResponseData - (out) vector to store the device poll response data
    /// @param responseSize - (out) size of the response data
    /// @param maxResponsesToReturn - maximum number of responses to return (0 for no limit)
    /// @return number of responses returned
    uint32_t getBusElemPollResponses(uint32_t address, bool& isOnline, uint16_t& deviceTypeIndex, 
                std::vector<uint8_t>& devicePollResponseData, 
                uint32_t& responseSize, uint32_t maxResponsesToReturn);

    ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get bus poll responses json
    /// @param deviceIdentMgr device identity manager
    /// @return JSON string
    String getBusPollResponsesJson(const DeviceIdentMgr& deviceIdentMgr);

    ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Is address found on main bus
    /// @param addr address
    /// @return true if address found on main bus
    bool isAddrFoundOnMainBus(uint32_t addr) const
    {
        if (addr > I2C_BUS_ADDRESS_MAX)
            return false;
        return (_mainBusAddrBits[addr/32] & (1 << (addr % 32))) != 0;
    }

    ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Set address found on main bus
    /// @param addr address
    void setAddrFoundOnMainBus(uint32_t addr)
    {
        if (addr > I2C_BUS_ADDRESS_MAX)
            return;
        if (!isAddrFoundOnMainBus(addr))
            _mainBusAddrBits[addr/32] |= (1 << (addr % 32));
    }

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Inform that slot is powering down
    /// @param slotPlus1 slotPlus1
    void slotPoweringDown(uint32_t slotPlus1);

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Inform that the bus is stuck
    void informBusStuck();

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

    // Find address record
    // Assumes semaphore already taken
    const BusI2CAddrStatus* findAddrStatusRecord(BusI2CAddrAndSlot addrAndSlot) const
    {
        for (const BusI2CAddrStatus& addrStatus : _i2cAddrStatus)
        {
            if ((addrStatus.addrAndSlot.addr == addrAndSlot.addr) && 
                    (addrStatus.addrAndSlot.slotPlus1 == addrAndSlot.slotPlus1))
                return &addrStatus;
        }
        return nullptr;
    }

    // Find address record editable
    // Assumes semaphore already taken
    BusI2CAddrStatus* findAddrStatusRecordEditable(BusI2CAddrAndSlot addrAndSlot)
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

    // Last status update times us
    uint64_t _lastIdentPollUpdateTimeUs = 0;
    uint64_t _lastBusElemOnlineStatusUpdateTimeUs = 0;

    // Addresses found online on main bus at any time
    uint32_t _mainBusAddrBits[(I2C_BUS_ADDRESS_MAX+31)/32] = {0};
    static const uint32_t SIZE_OF_MAIN_BUS_ADDR_BITS_ARRAY = sizeof(_mainBusAddrBits)/sizeof(_mainBusAddrBits[0]);
};
