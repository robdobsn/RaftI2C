/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Device Ident Manager
//
// Rob Dobson 2024
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "RaftBusDevicesIF.h"
#include "DeviceTypeRecord.h"
#include "BusStatusMgr.h"
#include "DeviceStatus.h"
#include "RaftJson.h"
#include "RaftThreading.h"
#include <vector>
#include <list>
#include <functional>
#include <atomic>

class DeviceIdentMgr : public RaftBusDevicesIF
{
public:
    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Constructor
    /// @param busStatusMgr bus status manager
    /// @param busReqSyncFn bus synchronous access request function
    /// @param busReqAsyncFn bus asynchronous access request function
    /// @param busReqEnqueueFn bus enqueue function (routes cross-task requests through the bus worker queue)
    DeviceIdentMgr(BusStatusMgr& busStatusMgr, BusReqSyncFn busReqSyncFn, BusReqAsyncFn busReqAsyncFn,
                BusReqEnqueueFn busReqEnqueueFn = nullptr);

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Destructor
    virtual ~DeviceIdentMgr();

    // Not copyable (owns a mutex)
    DeviceIdentMgr(const DeviceIdentMgr&) = delete;
    DeviceIdentMgr& operator=(const DeviceIdentMgr&) = delete;

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Setup
    /// @param config configuration
    void setup(const RaftJsonIF& config);

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get list of device addresses attached to the bus
    /// @param pAddrList pointer to array to receive addresses
    /// @param onlyAddressesWithIdentPollResponses true to only return addresses with ident poll responses    
    virtual void getDeviceAddresses(std::vector<BusElemAddrType>& addresses, bool onlyAddressesWithIdentPollResponses) const override final;

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get device type information by address
    /// @param address address of device to get information for
    /// @param includePlugAndPlayInfo true to include plug and play information
    /// @param deviceTypeIndex (out) device type index
    /// @return JSON string
    virtual String getDevTypeInfoJsonByAddr(BusElemAddrType address, bool includePlugAndPlayInfo, DeviceTypeIndexType& deviceTypeIndex) const override final;

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get device type information by device type name
    /// @param deviceType device type name
    /// @param includePlugAndPlayInfo true to include plug and play information
    /// @param deviceTypeIndex (out) device type index
    /// @return JSON string
    virtual String getDevTypeInfoJsonByTypeName(const String& deviceType, bool includePlugAndPlayInfo, DeviceTypeIndexType& deviceTypeIndex) const override final;

    ///////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get device type info JSON by device type index
    /// @param deviceTypeIdx device type index
    /// @param includePlugAndPlayInfo include plug and play info
    /// @return JSON string
    virtual String getDevTypeInfoJsonByTypeIdx(DeviceTypeIndexType deviceTypeIdx, bool includePlugAndPlayInfo) const override final;
     
    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get queued device data in JSON format
    /// @return JSON string
    virtual String getQueuedDeviceDataJson() override final;

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get queued device data in binary format
    /// @param connMode connection mode (inc bus number)
    /// @return Binary data vector
    virtual std::vector<uint8_t> getQueuedDeviceDataBinary(uint32_t connMode) override final;

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get decoded poll responses
    /// @param address address of device to get data from
    /// @param pStructOut pointer to structure (or array of structures) to receive decoded data
    /// @param structOutSize size of structure (in bytes) to receive decoded data
    /// @param maxRecCount maximum number of records to decode
    /// @param decodeState decode state for this device
    /// @return number of records decoded
    /// @note the pStructOut should generally point to structures of the correct type for the device data and the
    ///       decodeState should be maintained between calls for the same device
    virtual uint32_t getDecodedPollResponses(BusElemAddrType address, 
                    void* pStructOut, uint32_t structOutSize, 
                    uint16_t maxRecCount, RaftBusDeviceDecodeState& decodeState) const override final;

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get latest decoded poll response (non-destructive peek at most recent value)
    /// @param address address of device to get data from
    /// @param pStructOut pointer to structure to receive decoded data
    /// @param structOutSize size of structure (in bytes) to receive decoded data
    /// @param decodeState decode state for this device
    /// @return true if a value was successfully decoded
    virtual bool getLatestDecodedPollResponse(BusElemAddrType address,
                    void* pStructOut, uint32_t structOutSize,
                    RaftBusDeviceDecodeState& decodeState) const override final;

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Handle poll results
    /// @param timeNowUs time in us (passed in to aid testing)
    /// @param address address
    /// @param pollResultData poll result data
    /// @param pPollInfo pointer to device polling info (maybe nullptr) 
    /// @return true if result stored
    virtual bool handlePollResult(uint64_t timeNowUs, BusElemAddrType address, 
                            const std::vector<uint8_t>& pollResultData, const DevicePollingInfo* pPollInfo) override final
    {
        return _busStatusMgr.handlePollResult(0, timeNowUs, address, pollResultData, pPollInfo, 0);
    }

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Register for device data notifications
    /// @param addressAndSlot address of device
    /// @param dataChangeCB Callback for data change
    /// @param minTimeBetweenReportsMs Minimum time between reports (ms)
    /// @param pCallbackInfo Callback info (passed to the callback)
    virtual void registerForDeviceData(BusElemAddrType addressAndSlot, RaftDeviceDataChangeCB dataChangeCB, 
                uint32_t minTimeBetweenReportsMs, const void* pCallbackInfo) override final
    {
        _busStatusMgr.registerForDeviceData(addressAndSlot, dataChangeCB, minTimeBetweenReportsMs, pCallbackInfo);
    }

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Unregister for device data notifications for a specific address
    /// @param addressAndSlot address of device
    /// @param pCallbackInfo Callback info that was passed when registering (identifies the subscriber)
    /// @return true if a matching registration was found (and disarmed)
    /// @note On return the callback is not in progress and will not be called again for this address (so the
    ///       subscriber can be destroyed) - see BusStatusMgr::unregisterForDeviceData
    virtual bool unregisterForDeviceData(BusElemAddrType addressAndSlot, const void* pCallbackInfo) override final
    {
        return _busStatusMgr.unregisterForDeviceData(addressAndSlot, pCallbackInfo);
    }

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Unregister for device data notifications on all addresses
    /// @param pCallbackInfo Callback info that was passed when registering (identifies the subscriber)
    /// @return number of registrations disarmed
    virtual uint32_t unregisterForDeviceDataAll(const void* pCallbackInfo) override final
    {
        return _busStatusMgr.unregisterForDeviceDataAll(pCallbackInfo);
    }

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get debug JSON
    /// @return JSON string
    virtual String getDebugJSON(bool includeBraces) const override final;

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Send command to device on bus
    /// @param cmdJSON Command JSON string
    /// @param respMsg (out) response message from the device
    /// @return Result code
    /// @note The JSON string should include:
    ///       - "hexWr": hex string of data to write to the device
    ///       - "numToRd": number of bytes to read from the device (optional)
    virtual RaftRetCode sendCmdToDevice(RaftDeviceID deviceID, const char* cmdJSON, String* respMsg) override final;

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Register a handler invoked before default identification for newly-detected devices
    /// @param newDeviceIdentFn handler function (nullptr to clear)
    /// @param pCtx opaque context passed to the handler
    /// @note The handler is called on the bus task. The function and context are stored as one unit under a
    ///       mutex so the bus task can never call the function with a mismatched context. If a previously
    ///       registered handler is in progress on the bus task this waits (bounded) for it to return so that,
    ///       after clearing/replacing a handler, the old context can safely be destroyed.
    virtual void registerNewDeviceIdentHandler(RaftNewDeviceIdentFn newDeviceIdentFn, void* pCtx) override final;

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Register a handler serviced on the bus task, for application work that drives
    ///        the bus itself
    /// @param busTaskServiceFn handler function (nullptr to clear)
    /// @param pCtx opaque context passed to the handler
    /// @note The handler is called on the bus task - see registerNewDeviceIdentHandler for registration semantics
    virtual void registerBusTaskServiceHandler(RaftBusTaskServiceFn busTaskServiceFn, void* pCtx) override final;

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Discard what has been concluded about every device on the bus, so each is identified again
    /// @return number of devices currently known to the bus (the number scheduled for re-identification)
    /// @note Safe to call from any task. This records an atomic request; the I2C worker clears
    ///       identification at the next safe boundary. The scanner then picks the work up on its
    ///       next sweep - BusScanner already retries online-but-unidentified elements.
    virtual uint32_t reIdentifyDevices() override final
    {
        _reIdentifyRequested = true;
        return _busStatusMgr.getAddrStatusCount();
    }

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Consume a pending re-identification request
    /// @return number of device identifications cleared, or zero if no request was pending
    /// @note Must be called by the I2C worker only, between complete worker operations. If a request
    ///       arrives during identification, that result may finish publishing first, but this method
    ///       clears it on the following worker pass so an old result cannot survive the request.
    uint32_t serviceReIdentifyRequest()
    {
        if (!_reIdentifyRequested.exchange(false))
            return 0;
        return _busStatusMgr.clearAllDeviceIdentifications();
    }

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Service any registered bus-task handler (called from the bus worker loop only)
    bool serviceBusTaskHandler();

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Set a function used to (re)select a device's multiplexer slot
    /// @param reselectSlotFn function taking a slot number and enabling only that slot
    /// @note Used to restore the scanner's pre-selected slot after a new-device identification
    ///       handler runs, since a handler may perform bus transactions (e.g. a synchronous probe)
    ///       that reset the multiplexer slot selection which the default identification relies on.
    void setReselectSlotFn(std::function<RaftRetCode(uint32_t slotNum)> reselectSlotFn)
    {
        _reselectSlotFn = reselectSlotFn;
    }

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Identify device
    /// @param address address
    /// @param deviceStatus (out) device status
    void identifyDevice(BusElemAddrType address, DeviceStatus& deviceStatus);

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Check device type match (communicates with the device to check its type)
    /// @param address address
    /// @param pDevTypeRec device type record
    /// @return true if device type matches
    bool checkDeviceTypeMatch(BusElemAddrType address, const DeviceTypeRecord* pDevTypeRec);

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Process device initialisation
    /// @param address address
    /// @param pDevTypeRec device type record
    /// @return true if device initialisation was successful
    bool processDeviceInit(BusElemAddrType address, const DeviceTypeRecord* pDevTypeRec);

private:
    // Device indentification enabled
    bool _isEnabled = false;

    // Bus status
    BusStatusMgr& _busStatusMgr;

    // Bus request functions
    BusReqSyncFn _busReqSyncFn = nullptr;
    BusReqAsyncFn _busReqAsyncFn = nullptr;
    BusReqEnqueueFn _busReqEnqueueFn = nullptr;

    // Optional new-device identification handler (device-agnostic delegation hook) and bus-task service handler
    // These are registered from another task (generally the main task) and called on the bus task so each
    // {fn, ctx} pair is only accessed under _handlerMutex - it is copied out under the mutex and called outside it
    RaftMutex _handlerMutex;
    RaftNewDeviceIdentFn _newDeviceIdentFn = nullptr;
    RaftBusTaskServiceFn _busTaskServiceFn = nullptr;
    void* _busTaskServiceCtx = nullptr;
    void* _newDeviceIdentCtx = nullptr;

    // Handler in-progress tracking (set under _handlerMutex when the handler is copied out by the bus task)
    // used to wait for an in-flight call to complete when a handler is cleared/replaced
    std::atomic<bool> _newDeviceIdentInProgress{false};
    std::atomic<bool> _busTaskServiceInProgress{false};
    std::atomic<uint32_t> _newDeviceIdentCallCount{0};
    std::atomic<uint32_t> _busTaskServiceCallCount{0};
    std::atomic<void*> _handlerCallingTask{nullptr};
    std::atomic<bool> _reIdentifyRequested{false};
    static const uint32_t HANDLER_QUIESCE_MAX_MS = 1000;

    /// @brief Wait (bounded) for a handler call in progress on another task to complete
    /// @param inProgressFlag in-progress flag for the handler
    /// @param callCount count of calls made to the handler
    /// @param callCountAtReg value of callCount (read under the mutex) when the handler was changed
    /// @param handlerName name for logging
    void waitForHandlerQuiescence(const std::atomic<bool>& inProgressFlag,
                const std::atomic<uint32_t>& callCount, uint32_t callCountAtReg, const char* handlerName);

    // Optional function to (re)select a device's multiplexer slot after the handler runs
    std::function<RaftRetCode(uint32_t slotNum)> _reselectSlotFn = nullptr;

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Format device status to JSON
    /// @param address address
    /// @param onlineState device online state
    /// @param deviceTypeIndex index of device type
    /// @param devicePollResponseData poll response data
    /// @param responseSize size of poll response data
    /// @return JSON string
    String deviceStatusToJson(BusElemAddrType address, DeviceOnlineState onlineState, DeviceTypeIndexType deviceTypeIndex, 
                    const std::vector<uint8_t>& devicePollResponseData, uint32_t responseSize) const;

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Decode one or more poll responses for a device
    /// @param deviceTypeIndex index of device type
    /// @param pPollBuf buffer containing poll responses
    /// @param pollBufLen length of poll response buffer
    /// @param pStructOut pointer to structure (or array of structures) to receive decoded data
    /// @param structOutSize size of structure (in bytes) to receive decoded data
    /// @param maxRecCount maximum number of records to decode
    /// @return number of records decoded
    uint32_t decodePollResponses(DeviceTypeIndexType deviceTypeIndex, 
                    const uint8_t* pPollBuf, uint32_t pollBufLen, 
                    void* pStructOut, uint32_t structOutSize, 
                    uint16_t maxRecCount, RaftBusDeviceDecodeState& decodeState) const;

    /// @brief Callback for command result reports
    /// @param reqResult Result of the bus request
    void cmdResultReportCallback(BusRequestResult &reqResult);                    

    // Debug
    static constexpr const char* MODULE_PREFIX = "I2CDevIdentMgr";
};
