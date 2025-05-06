/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Bus Status Manager
//
// Rob Dobson 2020-2024
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <stdint.h>
#include "RaftThreading.h"
#include "RaftBus.h"
#include "RaftUtils.h"
#include "DeviceStatus.h"
#include "BusAddrStatus.h"
#include <list>

class BusStatusMgr {

public:
    // Constructor and destructor
    BusStatusMgr(RaftBus& raftBus);
    ~BusStatusMgr();

    // Setup & loop
    void setup(const RaftJsonIF& config);
    void loop(bool hwIsOperatingOk);

    // Get bus operation status
    BusOperationStatus isOperatingOk() const
    {
        return _busOperationStatus;
    }

    // Bus element access barring
    void barElemAccessSet(uint32_t timeNowMs, BusElemAddrType address, uint32_t barAccessAfterSendMs);
    bool barElemAccessGet(uint32_t timeNowMs, BusElemAddrType address);

    // Check if element is online
    BusOperationStatus isElemOnline(BusElemAddrType address) const;

    // Update bus element state
    // Returns true if state has changed
    bool updateBusElemState(BusElemAddrType address, bool elemResponding, bool& isOnline);

    // Get count of address status records
    uint32_t getAddrStatusCount() const;

    // Check if an address is being polled
    bool isAddrBeingPolled(BusElemAddrType address) const;

    // Set bus element device status (which includes device type and can be empty) for an address
    void setBusElemDeviceStatus(BusElemAddrType address, const DeviceStatus& deviceStatus);

    // Get device type index by address
    uint16_t getDeviceTypeIndexByAddr(BusElemAddrType address) const;

    // Get pending ident poll
    bool getPendingIdentPoll(uint64_t timeNowUs, DevicePollingInfo& pollInfo);

    /// @brief Handle poll result
    /// @param nextReqIdx index of next request to store (0 = full poll, 1+ = partial poll)
    /// @param timeNowUs time in us (passed in to aid testing)
    /// @param address address
    /// @param pollResultData poll result data
    /// @param pPollInfo pointer to device polling info (maybe nullptr)
    /// @param pauseAfterSendMs pause after send in ms
    /// @return true if result stored
    bool handlePollResult(uint32_t nextReqIdx, uint64_t timeNowUs, BusElemAddrType address, 
                    const std::vector<uint8_t>& pollResultData, const DevicePollingInfo* pPollInfo,
                    uint32_t pauseAfterSendMs);

    /// @brief Get latest timestamp of change to device info (online/offline, new data, etc)
    /// @param includeElemOnlineStatusChanges include changes in online status of elements
    /// @param includeDeviceDataUpdates include new data updates
    /// @return latest update time ms
    uint64_t getDeviceInfoTimestampMs(bool includeElemOnlineStatusChanges, bool includeDeviceDataUpdates) const;

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

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Register for device data notifications
    /// @param addrAndSlot address
    /// @param dataChangeCB Callback for data change
    /// @param minTimeBetweenReportsMs Minimum time between reports (ms)
    /// @param pCallbackInfo Callback info (passed to the callback)
    void registerForDeviceData(BusElemAddrType address, RaftDeviceDataChangeCB dataChangeCB,
                uint32_t minTimeBetweenReportsMs, const void* pCallbackInfo);

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Inform that an address is going offline
    /// @param addrList list of addresses
    void goingOffline(std::vector<BusElemAddrType>& addrList);

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Inform that the bus is stuck
    void informBusStuck();

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get debug JSON
    /// @return JSON string
    String getDebugJSON(bool includeBraces) const;

private:
    // Bus element status change mutex
    SemaphoreHandle_t _busElemStatusMutex = nullptr;

    // Bus base
    RaftBus& _raftBus;

    // Address status
    std::vector<BusAddrStatus> _addrStatus;
    static const uint32_t ADDR_STATUS_MAX = 50;

    // Find address record
    // Assumes semaphore already taken
    const BusAddrStatus* findAddrStatusRecord(BusElemAddrType address) const
    {
        for (const BusAddrStatus& addrStatus : _addrStatus)
        {
            if (addrStatus.address == address)
                return &addrStatus;
        }
        return nullptr;
    }

    // Find address record editable
    // Assumes semaphore already taken
    BusAddrStatus* findAddrStatusRecordEditable(BusElemAddrType address)
    {
        for (BusAddrStatus& addrStatus : _addrStatus)
        {
            if (addrStatus.address == address)
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
    uint32_t _lastIdentPollUpdateTimeMs = 0;
    uint32_t _lastBusElemOnlineStatusUpdateTimeMs = 0;
    uint32_t _lastPollOrStatusUpdateTimeMs = 0;

    // Debug
    static constexpr const char* MODULE_PREFIX = "RaftBusStMgr";    
};
