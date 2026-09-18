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
#include "SlotController.h"
#include "RaftThreading.h"
#include <atomic>

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
    /// @brief Check whether the I2C lines are physically stuck (SDA/SCL held low)
    virtual bool isBusStuck() const override final;

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Attempt to clear a stuck I2C bus by clocking it
    virtual bool clearBusStuck() override final;

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
    /// @brief Perform a synchronous (blocking) I2C transaction: write then optional read.
    /// @param pReqRec - bus request information (address+slot, write data, read length)
    /// @param pReadData - (out) buffer to receive read data (may be nullptr if no read required)
    /// @return result code
    /// @note Routes to the address's mux slot, performs the transaction, then clears all slots.
    ///       The whole sequence is performed holding the bus owner lock so it is serialised against the
    ///       bus worker task (and any other caller) and may be called from any task. RAFT_BUSY is returned
    ///       if the bus owner lock cannot be obtained in a reasonable time. The caller should still pause
    ///       the bus (pause()/isPaused()) if a SEQUENCE of transactions must not be interleaved with the
    ///       worker task's scanning/polling.
    virtual RaftRetCode busReqSync(const BusRequestInfo* pReqRec, std::vector<uint8_t>* pReadData) override final;

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
    /// @brief Get device type index for an address
    /// @param address Composite address
    /// @return Device type index (DEVICE_TYPE_INDEX_INVALID if not supported)
    virtual DeviceTypeIndexType getDeviceTypeIndex(BusElemAddrType address) const override final
    {
        return _busStatusMgr.getDeviceTypeIndexByAddr(address);
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
    /// @brief Set device poll bus frequency for an address
    /// @param address Composite address
    /// @param busHz Bus frequency in Hz (0 = use bus default)
    /// @return true if applied
    virtual bool setDevicePollBusHz(BusElemAddrType address, uint32_t busHz) override final
    {
        return _busStatusMgr.setDevicePollBusHz(address, busHz);
    }

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get device poll bus frequency for an address
    /// @param address Composite address
    /// @return Bus frequency in Hz (0 = use bus default / not supported)
    virtual uint32_t getDevicePollBusHz(BusElemAddrType address) const override final
    {
        return _busStatusMgr.getDevicePollBusHz(address);
    }

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Set device poll bus frequency slot mask for an address
    /// @param address Composite address
    /// @param slotMask Bitmask of slots where busHz applies (0 = all slots)
    /// @return true if applied
    virtual bool setDevicePollBusHzSlotMask(BusElemAddrType address, uint64_t slotMask) override final
    {
        return _busStatusMgr.setDevicePollBusHzSlotMask(address, slotMask);
    }

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get device poll bus frequency slot mask for an address
    /// @param address Composite address
    /// @return Bitmask of slots where busHz applies (0 = all slots / not supported)
    virtual uint64_t getDevicePollBusHzSlotMask(BusElemAddrType address) const override final
    {
        return _busStatusMgr.getDevicePollBusHzSlotMask(address);
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
        // If there is no bus power controller (no "pwr" config) then slot power is always on and cannot be disabled
        RaftRetCode retc = enablePower ? RAFT_OK : RAFT_INVALID_OBJECT;
        if (_pBusPowerController)
            retc = _pBusPowerController->enableSlot(slotNum, enablePower);
        RaftRetCode retc2 = _busMultiplexers.enableSlot(slotNum, enableData);
        return retc == RAFT_OK ? retc2 : retc;
    }

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get the slot controller (mode control for slots: I2C / SerialFull / SerialHalf)
    /// @return Pointer to the slot controller (always non-null; size 0 if no slots configured)
    SlotController* getSlotController()
    {
        return &_slotController;
    }

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Set the mode of a slot
    /// @param slotNum 1-based slot number
    /// @param pModeStr "i2c" | "serial-full"|"full" | "serial-half"|"half" | "serial" (legacy = full)
    /// @return RAFT_OK on success
    virtual RaftRetCode setSlotMode(uint32_t slotNum, const char* pModeStr) override final
    {
        SlotController::SlotMode mode = SlotController::SlotMode::I2C;
        if (!SlotController::parseModeStr(pModeStr, mode))
            return RAFT_INVALID_DATA;
        return _slotController.setMode(slotNum, mode);
    }

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get a JSON description of all slots and their modes
    virtual String getSlotModesJson() const override final
    {
        return _slotController.getStatusJson();
    }

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get bus status JSON - currently the poll-response integrity counters.
    /// All zero unless a device type record declares pollInfo.crc. "failed" counts
    /// first-read CRC failures, "recovered" those fixed by a re-read, and "dropped"
    /// those discarded (no correct data reached the decode). See
    /// devdocs/i2c-poll-data-integrity-crc-plan.md
    virtual String getBusStatusJson() const override final
    {
        const DevicePollingMgr::CrcStats& s = _devicePollingMgr.getCrcStats();
        char buf[160];
        snprintf(buf, sizeof(buf),
                 R"({"pollIntegrity":{"checked":%u,"failed":%u,"recovered":%u,"dropped":%u,"recoveries":%u}})",
                 (unsigned)s.checked, (unsigned)s.failed, (unsigned)s.recovered, (unsigned)s.dropped,
                 (unsigned)s.recoveries);
        return String(buf);
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
    static const uint32_t WAIT_FOR_TASK_EXIT_ON_DESTROY_MS = 10000;

    // Bus owner lock (recursive mutex)
    // The I2C hardware (and the state which goes with it - bus multiplexer slot selection, bus frequency, etc)
    // must only ever be driven by one task at a time. The bus worker task holds this lock for the active part
    // of each pass of its loop (NOT across its yield delay) and any other task which performs a transaction
    // inline (i2cSendAsync e.g. from virtualPinRead, i2cSendSync, busReqSync, clearBusStuck) holds it for the
    // whole slot-enable -> transaction -> slot-disable sequence. It is recursive because these functions nest
    // (e.g. i2cSendAsync -> enableOneSlot -> i2cSendSync) and because they are called on the worker task
    // which already holds the lock.
    //
    // LOCK ORDER: the bus owner lock is the OUTERMOST lock in RaftI2C. While holding it a task may take any one
    // of the following (which are only ever held briefly and never while waiting for the bus owner lock):
    //   BusAccessor::_pollingMutex, BusStatusMgr::_busElemStatusMutex (-> PollDataAggregator::_accessMutex),
    //   the BusAccessor request/response queue mutexes, DeviceIdentMgr::_handlerMutex,
    //   BusPowerController::_slotMutex (-> BusIOExpander::_regMutex), BusIOExpander::_regMutex
    // SlotController::_modeMutex is only used on the calling (main) task and is taken before
    // BusPowerController::_slotMutex / BusIOExpander::_regMutex - it is never held with the bus owner lock.
    // Code holding any of those locks MUST NOT perform a bus transaction or otherwise take the bus owner lock.
    // User callbacks are made without any of the inner locks held (poll-request and device-data callbacks
    // made on the worker task are made with the bus owner lock held which is safe because it is recursive).
    SemaphoreHandle_t _busOwnerMutex = nullptr;
    static const uint32_t BUS_OWNER_LOCK_MAX_WAIT_MS = 100;

    /// @brief Take the bus owner lock (recursive)
    /// @param maxWaitMs max time to wait (RAFT_MUTEX_WAIT_FOREVER to wait forever)
    /// @return true if the lock was obtained (busOwnerLockGive() must then be called)
    bool busOwnerLockTake(uint32_t maxWaitMs);

    /// @brief Give the bus owner lock
    void busOwnerLockGive();

    // RAII holder for the bus owner lock (released when it goes out of scope)
    class BusOwnerLock
    {
    public:
        BusOwnerLock(BusI2C& busI2C, uint32_t maxWaitMs) : _busI2C(busI2C)
        {
            _isLocked = _busI2C.busOwnerLockTake(maxWaitMs);
        }
        ~BusOwnerLock()
        {
            if (_isLocked)
                _busI2C.busOwnerLockGive();
        }
        BusOwnerLock(const BusOwnerLock&) = delete;
        BusOwnerLock& operator=(const BusOwnerLock&) = delete;
        bool isLocked() const
        {
            return _isLocked;
        }
    private:
        BusI2C& _busI2C;
        bool _isLocked = false;
    };

    // Pause/run status (pause is requested by any task and actioned by the worker task)
    std::atomic<bool> _pauseRequested{false};
    std::atomic<bool> _isPaused{false};

    // Haitus for period of ms (generally due to power cycling, etc)
    // The start time and period are written before the flag is set
    std::atomic<bool> _hiatusActive{false};
    std::atomic<uint32_t> _hiatusStartMs{0};
    std::atomic<uint32_t> _hiatusForMs{0};
    
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

    // Slot controller (per-slot mode: I2C / SerialFull / SerialHalf)
    SlotController _slotController;

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
