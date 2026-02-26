/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// I2C Bus Handler
//
// Rob Dobson 2020
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "RaftBus.h"
#include "RaftI2CCentralIF.h"
#include "BusRequestInfo.h"
#include "BusScanner.h"
#include "BusStatusMgr.h"
#include "BusMultiplexers.h"
#include "BusAccessor.h"
#include "DeviceIdentMgr.h"
#include "DevicePollingMgr.h"
#include "BusPowerController.h"
#include "BusStuckHandler.h"

// #define DEBUG_RAFT_BUSI2C_MEASURE_I2C_LOOP_TIME

class RaftI2CCentralIF;

class BusI2C : public RaftBus
{
public:
    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Constructor
    /// @param busElemStatusCB - callback for bus element status changes
    /// @param busOperationStatusCB - callback for bus operation status changes
    /// @param pI2CCentralIF - pointer to I2C central interface (if nullptr then use default I2C interface)
    BusI2C(BusElemStatusCB busElemStatusCB, BusOperationStatusCB busOperationStatusCB,
                RaftI2CCentralIF* pI2CCentralIF = nullptr);

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Destructor
    virtual ~BusI2C();

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Create function to create a new instance of this class
    /// @param busElemStatusCB - callback for bus element status changes
    /// @param busOperationStatusCB - callback for bus operation status changes
    static RaftBus* createFn(BusElemStatusCB busElemStatusCB, BusOperationStatusCB busOperationStatusCB)
    {
        return new BusI2C(busElemStatusCB, busOperationStatusCB);
    }

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief setup
    /// @param busNum - bus number
    /// @param config - configuration
    /// @return true if setup was successful
    virtual bool setup(BusNumType busNum, const RaftJsonIF& config) override final;

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Close bus
    virtual void close() override final;

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief loop (should be called frequently to service the bus)
    virtual void loop() override final;

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get bus devices interface
    virtual RaftBusDevicesIF* getBusDevicesIF() override final
    {
        return &_deviceIdentMgr;
    }

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief clear response queue (and optionally clear polling data)
    /// @param incPolling - clear polling data
    virtual void clear(bool incPolling) override final;

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief pause or resume bus
    /// @param pause - true to pause, false to resume
    virtual void pause(bool pause) override final
    {
        // Set pause flag - read in the worker
        _pauseRequested = pause;

        // Suspend bus accessor
        _busAccessor.pause(pause);
    }

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief isPaused
    /// @return true if the bus is paused
    virtual bool isPaused() const override final
    {
        return _isPaused;
    }

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Hiatus for a period in ms (stop bus activity for a period of time)
    /// @param forPeriodMs - period in ms
    virtual void hiatus(uint32_t forPeriodMs) override final;

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief isHiatus
    /// @return true if the bus is in hiatus
    virtual bool isHiatus() const override final
    {
        return _hiatusActive;
    }

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get bus name
    /// @return bus name
    virtual String getBusName() const override final
    {
        return _busName;
    }

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief isOperatingOk
    /// @return true if the bus is operating OK
    virtual BusOperationStatus isOperatingOk() const override final
    {
        return _busStatusMgr.isOperatingOk();
    }

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Request an action (like regular polling of a device or sending a single message and getting a response)
    /// @param busReqInfo - bus request information
    /// @return true if the request was added
    virtual bool addRequest(BusRequestInfo& busReqInfo) override final
    {
        return _busAccessor.addRequest(busReqInfo);
    }

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Check if an element is responding
    /// @param address - address of element
    /// @param pIsValid - (out) true if the address is valid
    /// @return true if the element is responding
    virtual bool isElemResponding(uint32_t address, bool* pIsValid) const override final;

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Request a change to bus scanning activity
    /// @param enableSlowScan - true to enable slow scan, false to disable
    /// @param requestFastScan - true to request a fast scan
    virtual void requestScan(bool enableSlowScan, bool requestFastScan) override final;

    ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Return addresses of devices attached to the bus
    /// @param addresses - vector to store the addresses of devices
    /// @param onlyAddressesWithIdentPollResponses - true to only return addresses with ident poll responses
    /// @return true if there are any ident poll responses available
    virtual bool getBusElemAddresses(std::vector<uint32_t>& addresses, bool onlyAddressesWithIdentPollResponses) const
    {
        return _busStatusMgr.getBusElemAddresses(addresses, onlyAddressesWithIdentPollResponses);
    }

    ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////    
    /// @brief Get bus element poll responses for a specific address
    /// @param address - address of device to get responses for
    /// @param onlineState - (out) device online state
    /// @param deviceTypeIndex - (out) device type index
    /// @param devicePollResponseData - (out) vector to store the device poll response data
    /// @param responseSize - (out) size of the response data
    /// @param maxResponsesToReturn - maximum number of responses to return (0 for no limit)
    /// @return number of responses returned
    virtual uint32_t getBusElemPollResponses(uint32_t address, DeviceOnlineState& onlineState, uint16_t& deviceTypeIndex, 
                std::vector<uint8_t>& devicePollResponseData, 
                uint32_t& responseSize, uint32_t maxResponsesToReturn) override final
    {
        return _busStatusMgr.getBusElemPollResponses(address, onlineState, deviceTypeIndex, devicePollResponseData, responseSize, maxResponsesToReturn);
    }

    ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get latest timestamp of change to device info (online/offline, new data, etc)
    /// @return timestamp of most recent device info in ms
    virtual uint32_t getDeviceInfoTimestampMs(bool includeElemOnlineStatusChanges, bool includeDeviceDataUpdates) const override final
    {
        return _busStatusMgr.getDeviceInfoTimestampMs(includeElemOnlineStatusChanges, includeDeviceDataUpdates);
    }

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Set device polling interval for an address
    /// @param address Composite address
    /// @param pollIntervalUs Polling interval in microseconds
    /// @return true if applied
    virtual bool setDevicePollIntervalUs(BusElemAddrType address, uint64_t pollIntervalUs) override final
    {
        return _busStatusMgr.setDevicePollIntervalUs(address, pollIntervalUs);
    }
    
    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get device polling interval for an address
    /// @param address Composite address
    /// @return Polling interval in microseconds (0 if not supported)
    virtual uint64_t getDevicePollIntervalUs(BusElemAddrType address) const override final
    {
        return _busStatusMgr.getDevicePollIntervalUs(address);
    }

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Set number of poll result samples to store for an address
    /// @param address Composite address
    /// @param numSamples Number of samples to store
    /// @return true if applied
    virtual bool setDeviceNumSamples(BusElemAddrType address, uint32_t numSamples) override final
    {
        return _busStatusMgr.setDeviceNumSamples(address, numSamples);
    }

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get number of poll result samples stored for an address
    /// @param address Composite address
    /// @return Number of samples (0 if not supported)
    virtual uint32_t getDeviceNumSamples(BusElemAddrType address) const override final
    {
        return _busStatusMgr.getDeviceNumSamples(address);
    }

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Set virtual pin levels on IO expander (pins must be on the same expander or on GPIO)
    /// @param numPins - number of pins to set
    /// @param pPinNums - array of pin numbers
    /// @param pLevels - array of levels (0 for low)
    /// @param pResultCallback - callback for result when complete/failed
    /// @param pCallbackData - callback data
    /// @return RAFT_OK if successful
    RaftRetCode virtualPinsSet(uint32_t numPins, const int* pPinNums, const uint8_t* pLevels, 
        VirtualPinSetCallbackType pResultCallback, void* pCallbackData)
    {
        return _busIOExpanders.virtualPinsSet(numPins, pPinNums, pLevels, pResultCallback, pCallbackData);
    }

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get virtual pin level on IO expander
    /// @param pinNum - pin number
    /// @param vPinCallback - callback for virtual pin changes
    /// @param pCallbackData - callback data
    /// @return RAFT_OK if successful
    virtual RaftRetCode virtualPinRead(int pinNum, VirtualPinReadCallbackType vPinCallback, void* pCallbackData = nullptr) override final
    {
        return _busIOExpanders.virtualPinRead(pinNum, _busReqAsyncFn, vPinCallback, pCallbackData);
    }

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Enable bus slot
    /// @param slotNum - slot number (0 is the main bus)
    /// @param enablePower - true to enable, false to disable
    /// @param enableData - true to enable data, false to disable
    /// @return RAFT_OK if successful
    virtual RaftRetCode enableSlot(uint32_t slotNum, bool enablePower, bool enableData)
    {
        RaftRetCode retc = _pBusPowerController->enableSlot(slotNum, enablePower);
        RaftRetCode retc2 = _busMultiplexers.enableSlot(slotNum, enableData);
        return retc == RAFT_OK ? retc2 : retc;
    }

private:

    // Yield value on each bus processing loop
    static const uint32_t I2C_BUS_LOOP_YIELD_MS = 5;

    // Max fast scanning without yielding
    static const uint32_t I2C_BUS_FAST_MAX_UNYIELD_DEFAUT_MS = 10;
    static const uint32_t I2C_BUS_SLOW_MAX_UNYIELD_DEFAUT_MS = 2;

    // Settings
    int _i2cPort = 0;
    int _sdaPin = -1;
    int _sclPin = -1;
    uint32_t _freq = 100000;
    uint32_t _i2cFilter = RaftI2CCentralIF::DEFAULT_BUS_FILTER_LEVEL;
    String _busName;

    // I2C device
    RaftI2CCentralIF* _pI2CCentral = nullptr;
    bool _i2cCentralNeedsToBeDeleted = false;

    // Last comms time uS
    uint64_t _lastI2CCommsUs = 0;
    static const uint32_t MIN_TIME_BETWEEN_I2C_COMMS_US = 1000;

    // I2C loop control
    uint32_t _loopFastUnyieldUs = I2C_BUS_FAST_MAX_UNYIELD_DEFAUT_MS * 1000;
    uint32_t _loopSlowUnyieldUs = I2C_BUS_SLOW_MAX_UNYIELD_DEFAUT_MS * 1000;
    uint32_t _loopYieldMs = I2C_BUS_LOOP_YIELD_MS;

    // Init ok
    bool _initOk = false;

    // Task that operates the bus
    volatile TaskHandle_t _i2cWorkerTaskHandle = nullptr;
    static const int DEFAULT_TASK_CORE = 0;
    static const int DEFAULT_TASK_PRIORITY = 5;
    static const int DEFAULT_TASK_STACK_SIZE_BYTES = 5000;
    static const uint32_t WAIT_FOR_TASK_EXIT_MS = 1000;

    // Pause/run status
    volatile bool _pauseRequested = false;
    volatile bool _isPaused = false;

    // Haitus for period of ms (generally due to power cycling, etc)
    volatile bool _hiatusActive = false;
    uint32_t _hiatusStartMs = 0;
    uint32_t _hiatusForMs = 0;
    
    // Measurement of loop time
#ifdef DEBUG_RAFT_BUSI2C_MEASURE_I2C_LOOP_TIME
    uint32_t _i2cDebugLastReportMs = 0;
    uint64_t _i2cLoopWorstTimeUs = 0;
    uint32_t _i2cMainLoopCount = 0;
#endif

    // I2C send sync function
    BusReqSyncFn _busReqSyncFn = nullptr;

    // I2C send async function
    BusReqAsyncFn _busReqAsyncFn = nullptr;

    // Bus status
    BusStatusMgr _busStatusMgr;

    // Bus elem tracker
    BusI2CElemTracker _busElemTracker;

    // Bus stuck handler
    BusStuckHandler _busStuckHandler;
    
    // Bus multiplexers (mux)
    BusMultiplexers _busMultiplexers;

    // Device identifier
    DeviceIdentMgr _deviceIdentMgr;

    // Bus scanner
    BusScanner _busScanner;

    // Device polling manager
    DevicePollingMgr _devicePollingMgr;

    // Bus accessor
    BusAccessor _busAccessor;

    // Bus IO Expanders
    BusIOExpanders _busIOExpanders;

    // Bus power controller
    BusPowerController* _pBusPowerController = nullptr;

    // Access barring time
    static const uint32_t ELEM_BAR_I2C_ADDRESS_MAX = 127;
    uint32_t _busAccessBarMs[ELEM_BAR_I2C_ADDRESS_MAX+1];

    // Debug
    uint32_t _debugLastBusLoopMs = 0;

    // Worker task (static version calls the other)
    static void i2cWorkerTaskStatic(void* pvParameters);
    void i2cWorkerTask();

    /// @brief Send I2C message asynchronously and store result in the response queue
    /// @param pReqRec
    /// @param pollListIdx 
    /// @return result code
    RaftRetCode i2cSendAsync(const BusRequestInfo* pReqRec, uint32_t pollListIdx);

    /// @brief Send I2C message synchronously
    /// @param pReqRec
    /// @param pReadData
    /// @return result code
    RaftRetCode i2cSendSync(const BusRequestInfo* pReqRec, std::vector<uint8_t>* pReadData);

    // Helpers
    RaftRetCode checkAddrValidAndNotBarred(BusElemAddrType address);

    // Debug
    static constexpr const char* MODULE_PREFIX = "RaftI2CBusI2C";    
};
