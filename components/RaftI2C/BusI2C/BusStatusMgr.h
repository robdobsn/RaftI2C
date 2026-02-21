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
    /// @brief Constructor
    /// @param raftBus 
    BusStatusMgr(RaftBus& raftBus);

    /// @brief Destructor
    ~BusStatusMgr();

    /// @brief Setup
    /// @param config Configuration (JSON)
    void setup(const RaftJsonIF& config);

    /// @brief Loop (called frequently)
    /// @param hwIsOperatingOk true if the hardware is operating correctly
    void loop(bool hwIsOperatingOk);

    /// @brief Check if bus is operating ok
    /// @return true if bus is operating ok
    BusOperationStatus isOperatingOk() const
    {
        return _busOperationStatus;
    }

    /// @brief Bus element access barring
    /// @param timeNowMs current time in ms (used to calculate if barring is still in effect)
    /// @param address address of the bus element
    /// @param barAccessAfterSendMs time to bar access after send in ms
    void barElemAccessSet(uint32_t timeNowMs, BusElemAddrType address, uint32_t barAccessAfterSendMs);

    /// @brief Check if bus element access is barred
    /// @param timeNowMs current time in ms (used to calculate if barring is still in effect)
    /// @param address address of the bus element
    /// @return true if access is barred
    bool barElemAccessGet(uint32_t timeNowMs, BusElemAddrType address);

    /// @brief Check if a bus element is online
    /// @param address address of the bus element
    /// @return true if the bus element is online
    BusOperationStatus isElemOnline(BusElemAddrType address) const;

    /// @brief Update bus element state
    /// @param address address of the bus element
    /// @param elemResponding true if the element is responding
    /// @param isOnline (out) true if the element is online
    /// @return true if state has changed
    bool updateBusElemState(BusElemAddrType address, bool elemResponding, bool& isOnline);

    /// @brief Get count of address status records
    /// @return count of address status records
    uint32_t getAddrStatusCount() const;

    /// @brief Check if an address is being polled
    /// @param address address of the bus element
    /// @return true if the address is being polled
    bool isAddrBeingPolled(BusElemAddrType address) const;

    /// @brief Set bus element device status (which includes device type and can be empty) for an address
    /// @param address address of the bus element
    /// @param deviceStatus device status to set
    void setBusElemDeviceStatus(BusElemAddrType address, const DeviceStatus& deviceStatus);

    /// @brief Get device type index by address
    /// @param address address of the bus element
    /// @return device type index (DEVICE_TYPE_INDEX_INVALID if not known)
    uint16_t getDeviceTypeIndexByAddr(BusElemAddrType address) const;

    /// @brief Get pending ident poll (this is the poll of the device based on its identified type)
    /// @param timeNowUs current time in us
    /// @param pollInfo (out) device polling info
    /// @return true if there is a pending ident poll
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

    /// @brief Return addresses of devices attached to the bus
    /// @param addresses - vector to store the addresses of devices
    /// @param onlyAddressesWithIdentPollResponses - true to only return addresses with ident poll responses
    /// @return true if there are any ident poll responses available
    bool getBusElemAddresses(std::vector<uint32_t>& addresses, bool onlyAddressesWithIdentPollResponses) const;

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

    /// @brief Register for device data notifications
    /// @param addrAndSlot address
    /// @param dataChangeCB Callback for data change
    /// @param minTimeBetweenReportsMs Minimum time between reports (ms)
    /// @param pCallbackInfo Callback info (passed to the callback)
    void registerForDeviceData(BusElemAddrType addrAndSlot, RaftDeviceDataChangeCB dataChangeCB,
                uint32_t minTimeBetweenReportsMs, const void* pCallbackInfo);

    /// @brief Inform that an address is going offline
    /// @param addrList list of addresses
    void goingOffline(std::vector<BusElemAddrType>& addrList);

    /// @brief Inform that the bus is stuck
    void informBusStuck();

    /// @brief Get debug JSON
    /// @return JSON string
    String getDebugJSON(bool includeBraces) const;

    /// @brief Set device polling interval for a specific address
    /// @param address Composite address
    /// @param pollIntervalUs Poll interval in microseconds
    /// @return true if updated
    bool setDevicePollIntervalUs(BusElemAddrType address, uint32_t pollIntervalUs);

    /// @brief Get device polling interval for a specific address
    /// @param address Composite address
    /// @return Poll interval in microseconds (0 if not supported)
    uint64_t getDevicePollIntervalUs(BusElemAddrType address) const;
    
private:
    // Bus element status change mutex
    mutable RaftMutex _busElemStatusMutex;

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
    static constexpr const char* MODULE_PREFIX = "I2CBusStMgr";    
};
