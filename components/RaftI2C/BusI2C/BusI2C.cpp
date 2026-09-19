/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// I2C Bus Handler
//
// Rob Dobson 2020
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "Logger.h"
#include "BusI2C.h"
#include "ConfigPinMap.h"
#include "RaftArduino.h"
#include "RaftJsonPrefixed.h"
#include "esp_task_wdt.h"
#include "BusI2CConsts.h"
#include "BusI2CAddrAndSlot.h"

// Auto-select I2C implementation based on chip if not explicitly defined
#if !defined(I2C_USE_RAFT_I2C) && !defined(I2C_USE_ESP_IDF)
    #if defined(CONFIG_IDF_TARGET_ESP32C6) || defined(CONFIG_IDF_TARGET_ESP32C5)
        #define I2C_USE_ESP_IDF
    #else
        #define I2C_USE_RAFT_I2C
    #endif
#endif

#if defined(I2C_USE_RAFT_I2C)
#include "RaftI2CCentral.h"
#elif defined(I2C_USE_ESP_IDF)
#include "RaftI2CCentral_ESPIDF.h"
#endif

// The following will define a minimum time between I2C comms activities
// #define ENFORCE_MIN_TIME_BETWEEN_I2C_COMMS_US 1

// Warn
#define WARN_IF_ADDR_OUTSIDE_VALID_RANGE

// Debug
// #define DEBUG_NO_POLLING
// #define DEBUG_I2C_ASYNC_SEND_HELPER
// #define DEBUG_I2C_SYNC_SEND_HELPER
// #define DEBUG_I2C_SEND_HELPERS_DETAIL_MAX_BYTES 32
// #define DEBUG_I2C_SYNC_SEND_HELPER_ADDR_LIST { 0x6a }
// #define DEBUG_I2C_SYNC_SEND_HELPER_ADDR_LIST { 0x6a, 0x25, 0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77 }
// #define DEBUG_I2C_SYNC_SEND_HELPER_ADDR_LIST { 0x48, 0x49, 0x26, 0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77 }
// #define DEBUG_BUS_HIATUS
// #define DEBUG_LOOP_TIMING_WITH_GPIO_NUM 19

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Constructor
BusI2C::BusI2C(BusElemStatusCB busElemStatusCB, BusOperationStatusCB busOperationStatusCB,
                RaftI2CCentralIF* pI2CCentralIF)
    : RaftBus(busElemStatusCB, busOperationStatusCB),
        _busReqSyncFn(std::bind(&BusI2C::i2cSendSync, this, std::placeholders::_1, std::placeholders::_2)),
        _busReqAsyncFn(std::bind(&BusI2C::i2cSendAsync, this, std::placeholders::_1, std::placeholders::_2)),
        _busStatusMgr(*this),
        _busStuckHandler(_busReqSyncFn),
        _busMultiplexers(_busStuckHandler, _busStatusMgr, _busElemTracker, _busReqSyncFn),
        _deviceIdentMgr(_busStatusMgr, _busReqSyncFn, _busReqAsyncFn,
                        [this](BusRequestInfo& busReqInfo) { return _busAccessor.addRequest(busReqInfo); }),
        _busScanner(_busStatusMgr, _busElemTracker, _busMultiplexers, _busIOExpanders, _deviceIdentMgr, _busReqSyncFn),
        _devicePollingMgr(_busStatusMgr, _busMultiplexers, _busReqSyncFn, nullptr),
        _busAccessor(*this, _busReqAsyncFn)
{
    // Init
    _lastI2CCommsUs = micros();

    // Bus owner lock (recursive mutex - see notes in header)
    _busOwnerMutex = xSemaphoreCreateRecursiveMutex();

    // Clear barring
    for (uint32_t i = 0; i < ELEM_BAR_I2C_ADDRESS_MAX; i++)
        _busAccessBarMs[i] = 0;

    // Set the interface
    _pI2CCentral = pI2CCentralIF;
    if (!_pI2CCentral)
    {
#if defined(I2C_USE_RAFT_I2C)
        _pI2CCentral = new RaftI2CCentral();
#elif defined(I2C_USE_ESP_IDF)
        _pI2CCentral = new RaftI2CCentral_ESPIDF();
#endif
        _i2cCentralNeedsToBeDeleted = true;
    }

    // Provide the I2C central to the polling manager for bus-frequency switching
    _devicePollingMgr.setI2CCentral(_pI2CCentral);

    // Allow the device identification manager to re-select a device's multiplexer slot after a
    // registered new-device identification handler runs (a handler's synchronous bus probe may
    // reset the slot selection which the scanner had established and which identification relies on)
    _deviceIdentMgr.setReselectSlotFn([this](uint32_t slotNum) { return _busMultiplexers.enableOneSlot(slotNum); });
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Destructor
BusI2C::~BusI2C()
{
    // Close to stop task
    close();

    // Check the worker task has exited (it clears the task handle as the last thing it does)
    // If not then wait longer as the worker may be part way through a long operation
    if (_i2cWorkerTaskHandle != nullptr)
    {
        LOG_W(MODULE_PREFIX, "destructor worker task still running - waiting");
        uint32_t waitStartMs = millis();
        while ((_i2cWorkerTaskHandle != nullptr) &&
                    !Raft::isTimeout(millis(), waitStartMs, WAIT_FOR_TASK_EXIT_ON_DESTROY_MS))
        {
            vTaskDelay(1);
        }
    }

    // If the worker still hasn't exited then it must not be allowed to continue to use this object
    // (or the I2C central) - so suspend it permanently and leak the objects it may be using rather than
    // deleting them while they may be in use
    TaskHandle_t workerTaskHandle = _i2cWorkerTaskHandle;
    if (workerTaskHandle != nullptr)
    {
        LOG_E(MODULE_PREFIX, "destructor worker task FAILED TO EXIT - suspending it and leaking I2C central");
        vTaskSuspend(workerTaskHandle);
        return;
    }

    // Clean up
    if (_i2cCentralNeedsToBeDeleted)
        delete _pI2CCentral;

    // Check if bus power controller needs to be deleted
    if (_pBusPowerController)
        delete _pBusPowerController;

    // Remove bus owner lock
    if (_busOwnerMutex)
        vSemaphoreDelete(_busOwnerMutex);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Setup
/// @param busNum - bus number
/// @param config Configuration
/// @return true if successful
bool BusI2C::setup(BusNumType busNum, const RaftJsonIF& config)
{
    // Note:
    // No attempt is made here to clean-up properly
    // The assumption is that if robot configuration is completely changed then
    // the firmware will be restarted from scratch

    // Check if already configured
    if (_initOk)
        return false;
    _busNum = busNum;

    // Get bus details
    _i2cPort = config.getLong("i2cPort", 0);
    String pinName = config.getString("sdaPin", "");
    _sdaPin = ConfigPinMap::getPinFromName(pinName.c_str());
    pinName = config.getString("sclPin", "");
    _sclPin = ConfigPinMap::getPinFromName(pinName.c_str());
    _freq = config.getLong("i2cFreq", 100000);
    _i2cFilter = config.getLong("i2cFilter", RaftI2CCentralIF::DEFAULT_BUS_FILTER_LEVEL);
    _busName = config.getString("name", "");
    UBaseType_t taskCore = config.getLong("taskCore", DEFAULT_TASK_CORE);
    BaseType_t taskPriority = config.getLong("taskPriority", DEFAULT_TASK_PRIORITY);
    int taskStackSize = config.getLong("taskStack", DEFAULT_TASK_STACK_SIZE_BYTES);

    // Yield values
    _loopYieldMs = config.getLong("loopYieldMs", I2C_BUS_LOOP_YIELD_MS);
    _loopFastUnyieldUs = config.getLong("fastScanMaxUnyieldMs", I2C_BUS_FAST_MAX_UNYIELD_DEFAUT_MS) * 1000;
    _loopSlowUnyieldUs = config.getLong("slowScanMaxUnyieldMs", I2C_BUS_SLOW_MAX_UNYIELD_DEFAUT_MS) * 1000;

    // Bus status manager
    _busStatusMgr.setup(config);

    // Bus mux setup
    RaftJsonPrefixed busExtenderConfig(config, "mux");
    _busMultiplexers.setup(busExtenderConfig);

    // IO expanders
    RaftJsonPrefixed busIOConfig(config, "ioExps");
    _busIOExpanders.setup(busIOConfig);

    // Bus power controller setup
    int arrayLen = 0;
    if (config.getType("pwr", arrayLen) == RaftJsonIF::RAFT_JSON_OBJECT)
    {
        RaftJsonPrefixed busPowerConfig(config, "pwr");
        _pBusPowerController = new BusPowerController(_busIOExpanders);
        _busMultiplexers.setBusPowerController(_pBusPowerController);
        if (_pBusPowerController)
            _pBusPowerController->setup(busPowerConfig);
    }

    // Bus stuck handler setup
    _busStuckHandler.setup(config);

    // Device ident manager
    _deviceIdentMgr.setup(config);

    // Setup bus scanner
    _busScanner.setup(config);

    // Setup device polling manager
    _devicePollingMgr.setup(config); 

    // Setup bus accessor
    _busAccessor.setup(config);

    // Slot controller (per-slot mode: I2C / SerialFull / SerialHalf)
    RaftJsonPrefixed slotControlConfig(config, "slotControl");
    _slotController.setup(slotControlConfig, this);

    // Check valid
    if ((_sdaPin < 0) || (_sclPin < 0))
    {
        LOG_W(MODULE_PREFIX, "setup INVALID PARAMS name %s port %d SDA %d SCL %d FREQ %d", _busName.c_str(), _i2cPort, _sdaPin, _sclPin, _freq);
        return false;
    }

    // Check central is valid
    if (!_pI2CCentral)
    {
        LOG_W(MODULE_PREFIX, "setup FAILED no device");
        return false;
    }

    // Init the I2C bus
    if (!_pI2CCentral->init(_i2cPort, _sdaPin, _sclPin, _freq, _i2cFilter))
    {
        LOG_W(MODULE_PREFIX, "setup FAILED name %s port %d SDA %d SCL %d FREQ %d", _busName.c_str(), _i2cPort, _sdaPin, _sclPin, _freq);
        return false;
    }

    // Run post-setup on the bus power controller
    if (_pBusPowerController)
        _pBusPowerController->postSetup();

    // Apply slot controller defaults (defaultMode for each slot). Done after the bus is
    // initialised so that any virtual-pin writes are queued onto a working bus.
    _slotController.applyDefaults();

    // Ok
    _initOk = true;

    // Reset pause status
    _pauseRequested = false;
    _isPaused = false;

    // Start the worker task
    BaseType_t retc = pdPASS;
    if (_i2cWorkerTaskHandle == nullptr)
    {
        retc = xTaskCreatePinnedToCore(
                    i2cWorkerTaskStatic,
                    "I2CTask",             // task name
                    taskStackSize,                          // stack size of task
                    this,                                   // parameter passed to task on execute
                    taskPriority,                           // priority
                    (TaskHandle_t*)&_i2cWorkerTaskHandle,   // task handle
                    taskCore);                              // pin task to core N
    }

    // Debug
    LOG_I(MODULE_PREFIX, "setup %s(%d) name %s port %d SDA %d SCL %d FREQ %d FILTER %d portTICK_PERIOD_MS %d taskCore %d taskPriority %d stackBytes %d loopYieldMs %d fastUnyieldMs %d slowUnyieldMs %d",
                (retc == pdPASS) ? "OK" : "FAILED", retc, _busName.c_str(), _i2cPort,
                _sdaPin, _sclPin, _freq, _i2cFilter, 
                portTICK_PERIOD_MS, taskCore, taskPriority, taskStackSize,
                _loopYieldMs, (uint32_t) (_loopFastUnyieldUs/1000), (uint32_t) (_loopSlowUnyieldUs/1000));

    // Ok
    return true;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Close
void BusI2C::close()
{
    if (_i2cWorkerTaskHandle != nullptr) 
    {
        // Shutdown task
        xTaskNotifyGive(_i2cWorkerTaskHandle);
        uint32_t waitStartMs = millis();
        while (!Raft::isTimeout(millis(), waitStartMs, WAIT_FOR_TASK_EXIT_MS))
        {
            if (_i2cWorkerTaskHandle == nullptr)
                break;
            vTaskDelay(1);
        }
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Service (called frequently from main loop)
void BusI2C::loop()
{
    // Check ok
    if (!_initOk)
        return;

    // Service bus scanner
    _busScanner.loop();

    // Service bus status change detection
    _busStatusMgr.loop(_pI2CCentral ? _pI2CCentral->isOperatingOk() : false);

    // Service bus mux
    _busMultiplexers.loop();

    // Bus power controller loop
    if (_pBusPowerController)
        _pBusPowerController->loop();

    // Service bus accessor
    _busAccessor.loop();
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Clear bus activity
/// @param incPolling - true to clear polling
void BusI2C::clear(bool incPolling)
{
    // Check init
    if (!_initOk)
        return;

    // Clear accessor
    _busAccessor.clear(incPolling);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Static RTOS task function that handles all bus interaction (runs indefinitely - or until notified to stop)
/// @param pvParameters - pointer to the object that requested the task
void BusI2C::i2cWorkerTaskStatic(void* pvParameters)
{
    // Get the object that requested the task
    BusI2C* pObjPtr = (BusI2C*)pvParameters;
    if (pObjPtr)
        pObjPtr->i2cWorkerTask();
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief RTOS task function that handles all bus interaction (runs indefinitely - or until notified to stop)
void BusI2C::i2cWorkerTask()
{
#ifdef DEBUG_LOOP_TIMING_WITH_GPIO_NUM
    pinMode(DEBUG_LOOP_TIMING_WITH_GPIO_NUM, OUTPUT);
    digitalWrite(DEBUG_LOOP_TIMING_WITH_GPIO_NUM, 0);
#endif

    // Allocate core-specific resources for the I2C central (e.g. the I2C interrupt) on the core that this
    // task runs on - so the ISR runs on the same core as the task which performs bus accesses
    if (_pI2CCentral && !_pI2CCentral->initOnBusTask())
    {
        LOG_W(MODULE_PREFIX, "i2cWorkerTask initOnBusTask failed");
    }

    _debugLastBusLoopMs = millis();
    while (ulTaskNotifyTake(pdTRUE, 0) == 0)
    {
#ifdef DEBUG_LOOP_TIMING_WITH_GPIO_NUM
        for (int ii = 0; ii < 5; ii++)
        {
            digitalWrite(DEBUG_LOOP_TIMING_WITH_GPIO_NUM, 1);
            delayMicroseconds(1);
            digitalWrite(DEBUG_LOOP_TIMING_WITH_GPIO_NUM, 0);
            delayMicroseconds(1);
        }
#endif        
        // Allow other tasks to run
        vTaskDelay(pdMS_TO_TICKS(_loopYieldMs));

#ifdef DEBUG_LOOP_TIMING_WITH_GPIO_NUM
        digitalWrite(DEBUG_LOOP_TIMING_WITH_GPIO_NUM, 1);
        delayMicroseconds(1);
        digitalWrite(DEBUG_LOOP_TIMING_WITH_GPIO_NUM, 0);
        delayMicroseconds(1);
#endif        

#ifdef DEBUG_RAFT_BUSI2C_MEASURE_I2C_LOOP_TIME
        uint64_t startUs = micros();
#endif

        // Check I2C initialised
        if (!_initOk)
            continue;

        // Cur loop microseconds
        uint64_t curTimeUs = micros();
        uint32_t curTimeMs = curTimeUs / 1000;

        // Check bus hiatus
        if (_hiatusActive)
        {
            uint32_t hiatusStartMs = _hiatusStartMs;
            uint32_t hiatusForMs = _hiatusForMs;
            if (!Raft::isTimeout(curTimeMs, hiatusStartMs, hiatusForMs))
                continue;
            _hiatusActive = false;
#ifdef DEBUG_BUS_HIATUS
            LOG_I(MODULE_PREFIX, "i2cWorkerTask hiatus over");
#endif
        }

        // Stats
        _busStats.activity();

        // Check pause status
        if ((_isPaused) && (!_pauseRequested))
            _isPaused = false;
        else if ((!_isPaused) && (_pauseRequested))
            _isPaused = true;
        bool isPaused = _isPaused;

        // Obtain the bus owner lock for the active part of this pass of the loop so that a transaction
        // performed inline by any other task is serialised against everything done here (scanning,
        // identification, queued requests and polling - including slot selection and bus frequency changes)
        // The lock is released at the end of the pass (including on continue) and hence is
        // NOT held across the yield delay at the top of the loop
        BusOwnerLock busOwnerLock(*this, RAFT_MUTEX_WAIT_FOREVER);
        if (!busOwnerLock.isLocked())
            continue;

#ifdef DEBUG_LOOP_TIMING_WITH_GPIO_NUM
        digitalWrite(DEBUG_LOOP_TIMING_WITH_GPIO_NUM, 1);
        delayMicroseconds(1);
        digitalWrite(DEBUG_LOOP_TIMING_WITH_GPIO_NUM, 0);
        delayMicroseconds(1);
#endif

        // Service any application handler registered to run on the bus task. It may issue
        // synchronous bus transactions safely because it IS the bus task - the same reason the
        // new-device identification hook can.
        //
        // Returning true means the handler is mid-operation, which holds off DEVICE POLLING
        // only - see below. Scanning is deliberately NOT held off, and that is load-bearing:
        // BusMultiplexers::taskService() is empty, so a multiplexer wedged by a device reset is
        // re-detected and recovered solely through the scanner calling elemStateChange().
        // Suspending scanning during RSAO address assignment therefore blocked the very
        // recovery the assignment depends on, and every transaction NACKed until it gave up.
        bool busTaskHandlerBusy = false;
        if (!isPaused)
            busTaskHandlerBusy = _deviceIdentMgr.serviceBusTaskHandler();

        // Handle bus scanning
#ifndef DEBUG_NO_SCANNING
        if (!isPaused)
        {
            // Service bus scanner
            if (_busScanner.isScanPending(curTimeMs))
            {
                _busScanner.taskService(curTimeUs, _loopFastUnyieldUs, _loopSlowUnyieldUs);
            }
        }
#endif

        // Handle requests
        _busAccessor.processRequestQueue(isPaused);

        // Don't do any polling when paused
        if (isPaused)
            continue;

#ifdef DEBUG_NO_POLLING
        continue;
#endif

#ifdef DEBUG_LOOP_TIMING_WITH_GPIO_NUM
        digitalWrite(DEBUG_LOOP_TIMING_WITH_GPIO_NUM, 1);
        delayMicroseconds(1);
        digitalWrite(DEBUG_LOOP_TIMING_WITH_GPIO_NUM, 0);
        delayMicroseconds(1);
#endif

        // Bus mux loop
        _busMultiplexers.taskService();

#ifdef DEBUG_LOOP_TIMING_WITH_GPIO_NUM
        digitalWrite(DEBUG_LOOP_TIMING_WITH_GPIO_NUM, 1);
        delayMicroseconds(1);
        digitalWrite(DEBUG_LOOP_TIMING_WITH_GPIO_NUM, 0);
        delayMicroseconds(1);
#endif

        // Bus power controller loop
        if (_pBusPowerController)
            _pBusPowerController->taskService(millis());

#ifdef DEBUG_LOOP_TIMING_WITH_GPIO_NUM
        digitalWrite(DEBUG_LOOP_TIMING_WITH_GPIO_NUM, 1);
        delayMicroseconds(1);
        digitalWrite(DEBUG_LOOP_TIMING_WITH_GPIO_NUM, 0);
        delayMicroseconds(1);
#endif

        // Update IO expanders (if dirty)
        _busIOExpanders.syncI2CIOStateChanges(false, std::bind(&BusI2C::i2cSendSync, this, std::placeholders::_1, std::placeholders::_2));

        // Device polling - held off while the bus-task handler is mid-operation (see above).
        // Polling is pure bus load with no role in recovery, so unlike scanning it is safe to
        // suspend: it is what makes the bus busy (a single VCP at the default rates occupies
        // ~50% of a 100kHz bus), and a timed assignment sequence should not queue behind it.
        if (!busTaskHandlerBusy)
            _devicePollingMgr.taskService(micros());

#ifdef DEBUG_LOOP_TIMING_WITH_GPIO_NUM
        digitalWrite(DEBUG_LOOP_TIMING_WITH_GPIO_NUM, 1);
        delayMicroseconds(1);
        digitalWrite(DEBUG_LOOP_TIMING_WITH_GPIO_NUM, 0);
        delayMicroseconds(1);
#endif

        // Perform any user-defined access
        // TODO - remove or reconsider how polling works
        _busAccessor.processPolling();

        // Service bus stuck handler
        _busStuckHandler.loopSync();

#ifdef DEBUG_RAFT_BUSI2C_MEASURE_I2C_LOOP_TIME
        // Debug
        uint64_t timeUs = micros() - startUs;
        if (timeUs > _i2cLoopWorstTimeUs)
            _i2cLoopWorstTimeUs = timeUs;
        _i2cMainLoopCount++;
        if (millis() - _i2cDebugLastReportMs > 30000)
        {
            _i2cDebugLastReportMs = millis();
            LOG_I(MODULE_PREFIX, "i2cWorkerTask timeUs %lld worstTimeUs %lld loopCount %d", 
                        timeUs, _i2cLoopWorstTimeUs, _i2cMainLoopCount);
            _i2cLoopWorstTimeUs = 0;
        }
#endif
    }

    LOG_I(MODULE_PREFIX, "i2cWorkerTask exiting");

    // Task has exited
    _i2cWorkerTaskHandle = nullptr;
    vTaskDelete(NULL);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Send I2C message synchronously
/// @param pReqRec - contains the request details including address, write data, read data length, etc
/// @param pReadData - pointer to buffer for read data
/// @return result code
/// @note This is called from the worker task/thread and does not set the bus extender so if a slotted address
///       is used then the bus extender must be set before calling this function
///       The bus owner lock is held for the transaction (it is recursive so this is a nested take when called
///       on the worker task or from busReqSync/i2cSendAsync which already hold it)
RaftRetCode BusI2C::i2cSendSync(const BusRequestInfo* pReqRec, std::vector<uint8_t>* pReadData)
{
    // Check valid
    if (!_pI2CCentral)
        return RAFT_BUS_NOT_INIT;

    // Obtain the bus owner lock
    BusOwnerLock busOwnerLock(*this, BUS_OWNER_LOCK_MAX_WAIT_MS);
    if (!busOwnerLock.isLocked())
        return RAFT_BUSY;

    // Get address
    BusElemAddrType address = pReqRec->getAddress();
    uint16_t i2cAddr = BusI2CAddrAndSlot::getI2CAddr(address);

    // Check address is within valid range
    RaftRetCode rsltCode = RAFT_BUS_NOT_INIT;
    bool addrOk = checkAddrValidAndNotBarred(address) == RAFT_OK;
    if (addrOk)
    {
        // Buffer for read
        uint32_t readReqLen = 0;
        uint8_t pDummyReadBuf[1];
        if (pReadData && pReqRec->getReadReqLen() > 0)
        {
            readReqLen = pReqRec->getReadReqLen();
            pReadData->resize(readReqLen);
        }
        uint32_t writeReqLen = pReqRec->getWriteDataLen();

        // Access the bus
        uint32_t numBytesRead = 0;
        rsltCode = _pI2CCentral->access(i2cAddr, pReqRec->getWriteData(), writeReqLen, 
                pReadData ? pReadData->data() : pDummyReadBuf, readReqLen, numBytesRead);

        // Record time of comms
        _lastI2CCommsUs = micros();
    }

#ifdef DEBUG_I2C_SYNC_SEND_HELPER
#ifdef DEBUG_I2C_SYNC_SEND_HELPER_ADDR_LIST
    std::vector<int> addrList = { DEBUG_I2C_SYNC_SEND_HELPER_ADDR_LIST };
    if (std::find(addrList.begin(), addrList.end(), BusI2CAddrAndSlot::getI2CAddr(address)) != addrList.end())
    {
#endif
#ifdef DEBUG_I2C_SEND_HELPERS_DETAIL_MAX_BYTES
    String writeDataHexStr;
    Raft::getHexStrFromBytes(pReqRec->getWriteData(), Raft::clamp(pReqRec->getWriteDataLen(), 0, DEBUG_I2C_SEND_HELPERS_DETAIL_MAX_BYTES), writeDataHexStr);
    LOG_I(MODULE_PREFIX, "I2CSendSync %s i2cAddr %s writeLen %d readLen %d reqType %d writeData %s",
                    addrOk ? Raft::getRetCodeStr(rsltCode) : "INVALID ADDR",
                    BusI2CAddrAndSlot::toString(address).c_str(), pReqRec->getWriteDataLen(),
                    pReqRec->getReadReqLen(), pReqRec->getBusReqType(), writeDataHexStr.c_str());
#else
    LOG_I(MODULE_PREFIX, "I2CSendSync %s addr %s writeLen %d readLen %d reqType %d",
                    addrOk ? Raft::getRetCodeStr(rsltCode) : "INVALID ADDR",
                    BusI2CAddrAndSlot::toString(address).c_str(), pReqRec->getWriteDataLen(),
                    pReqRec->getReadReqLen(), pReqRec->getBusReqType());
#endif
#ifdef DEBUG_I2C_SYNC_SEND_HELPER_ADDR_LIST
    }
#endif
#endif

    return rsltCode;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Perform a synchronous (blocking) I2C transaction: write then optional read
/// @param pReqRec - bus request information (address+slot, write data, read length)
/// @param pReadData - pointer to buffer for read data (may be nullptr)
/// @return result code
/// @note Generic, device-agnostic blocking transaction. Routes to the address's mux slot,
///       performs the access, then clears all slots. The whole sequence is performed holding the
///       bus owner lock so it cannot race the worker task (or any other caller). The caller should
///       still pause the bus (pause()/isPaused()) if a sequence of transactions must not be
///       interleaved with scanning/polling.
RaftRetCode BusI2C::busReqSync(const BusRequestInfo* pReqRec, std::vector<uint8_t>* pReadData)
{
    if (!_pI2CCentral)
        return RAFT_BUS_NOT_INIT;

    // Obtain the bus owner lock for the whole slot-enable -> transaction -> slot-disable sequence
    BusOwnerLock busOwnerLock(*this, BUS_OWNER_LOCK_MAX_WAIT_MS);
    if (!busOwnerLock.isLocked())
        return RAFT_BUSY;

    // Route to the slot for this address (slot 0 = main bus, a no-op enable)
    const uint16_t slotNum = BusI2CAddrAndSlot::getSlotNum(pReqRec->getAddress());
    RaftRetCode rslt = _busMultiplexers.enableOneSlot(slotNum);
    if (rslt != RAFT_OK)
        return rslt;

    // Perform the synchronous transaction
    rslt = i2cSendSync(pReqRec, pReadData);

    // Clear all slots so the bus is left in a known state
    _busMultiplexers.disableAllSlots(false);
    return rslt;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Send I2C message asynchronously and store result in the response queue
/// @param pReqRec - contains the request details including address, write data, read data length, etc
/// @param pollListIdx - index into the polling list - used to reference back to the response queue
/// @return result code
RaftRetCode BusI2C::i2cSendAsync(const BusRequestInfo* pReqRec, uint32_t pollListIdx)
{
    // Check address is within valid range and not barred
    BusElemAddrType address = pReqRec->getAddress();
    uint16_t i2cAddr = BusI2CAddrAndSlot::getI2CAddr(address);
    uint16_t slotNum = BusI2CAddrAndSlot::getSlotNum(address);

#ifdef DEBUG_I2C_ASYNC_SEND_HELPER
    LOG_I(MODULE_PREFIX, "I2CSendAsync addr %s writeLen %d readLen %d reqType %d pollListIdx %d",
                    BusI2CAddrAndSlot::toString(address).c_str(), pReqRec->getWriteDataLen(),
                    pReqRec->getReadReqLen(), pReqRec->getBusReqType(), pollListIdx);
#endif

    auto rslt = checkAddrValidAndNotBarred(address);
    if (rslt != RAFT_OK)
        return rslt;

    // Obtain the bus owner lock for the whole slot-enable -> transaction -> slot-disable sequence
    // This function is called on the worker task (which already holds the lock - it is recursive) but can
    // also be called inline on another task (e.g. virtualPinRead) which must be serialised against the worker
    BusOwnerLock busOwnerLock(*this, BUS_OWNER_LOCK_MAX_WAIT_MS);
    if (!busOwnerLock.isLocked())
        return RAFT_BUSY;

    // Check if a bus mux slot is specified
    rslt = _busMultiplexers.enableOneSlot(slotNum);
    if (rslt != RAFT_OK)
        return rslt;

    // Buffer for read and address
    uint32_t readReqLen = pReqRec->getReadReqLen();
    uint8_t readBuf[readReqLen];
    uint32_t writeReqLen = pReqRec->getWriteDataLen();
    uint32_t barAccessAfterSendMs = pReqRec->getBarAccessForMsAfterSend();

    // Access the bus
    uint32_t numBytesRead = 0;
    rslt = RAFT_BUS_NOT_INIT;
    if (!_pI2CCentral)
        return rslt;
    rslt = _pI2CCentral->access(i2cAddr, pReqRec->getWriteData(), writeReqLen, 
            readBuf, readReqLen, numBytesRead);

    // Reset bus multiplexers to turn off all slots
    _busMultiplexers.disableAllSlots(false);

    // Check for scanning
    if (!pReqRec->isScan())
    {
        // If not scanning handle the response (there is no response for scanning)
        _busAccessor.handleResponse(pReqRec, rslt, readBuf, numBytesRead);
    }

    // Bar access to element if requested
    if (barAccessAfterSendMs > 0)
        _busStatusMgr.barElemAccessSet(millis(), address, barAccessAfterSendMs);

    // Record time of comms
    _lastI2CCommsUs = micros();
    return rslt;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Check address is valid and not barred
/// @param addrAndSlot - address to check
/// @return result code
RaftRetCode BusI2C::checkAddrValidAndNotBarred(BusElemAddrType address)
{
#ifdef ENFORCE_MIN_TIME_BETWEEN_I2C_COMMS_US
    // Check the last time a communication occurred - if less than the minimum between sends
    // then delay
    while (!Raft::isTimeout(micros(), _lastI2CCommsUs, MIN_TIME_BETWEEN_I2C_COMMS_US))
    {
        vTaskDelay(0);
    }
#endif

    // Check if this address is barred for a period
    if (_busStatusMgr.barElemAccessGet(millis(), address))
        return RAFT_BUS_BARRED;

    return RAFT_OK;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Check if a bus element is responding
/// @param address - address of element
/// @param pIsValid - pointer to bool to receive validity flag
/// @return true if element is responding
bool BusI2C::isElemResponding(uint32_t address, bool* pIsValid) const
{
    if (pIsValid)
        *pIsValid = true;
    return _busStatusMgr.isElemOnline(address) == BusOperationStatus::BUS_OPERATION_OK;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Request a bus scan
/// @param enableSlowScan - true to enable slow scan
/// @param requestFastScan - true to request a fast scan
void BusI2C::requestScan(bool enableSlowScan, bool requestFastScan)
{
    _busScanner.requestScan(enableSlowScan, requestFastScan);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Hiatus for period of ms
/// @param forPeriodMs - period in ms
void BusI2C::hiatus(uint32_t forPeriodMs)
{
    _hiatusStartMs = millis();
    _hiatusForMs = forPeriodMs;
    _hiatusActive = true;
#ifdef DEBUG_BUS_HIATUS
    LOG_I("BusI2C", "hiatus req for %dms", forPeriodMs);
#endif
}


/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Check whether the I2C lines are physically stuck (SDA/SCL held low)
/// @return true if stuck
/// @note Reads the SDA/SCL GPIOs directly, so it is valid whenever the caller owns the bus -
///       including while the worker is paused under a lease, which is exactly when a caller
///       running its own transactions needs to tell "wedged bus" from "device answering
///       badly". The two look identical on the wire: an ACK is the line pulled low, so a
///       stuck bus ACKs every address and reads back zeros.
bool BusI2C::isBusStuck() const
{
    return const_cast<BusStuckHandler&>(_busStuckHandler).isStuck();
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Attempt to clear a stuck I2C bus by clocking it
/// @return true if the bus is no longer stuck afterwards
bool BusI2C::clearBusStuck()
{
    // Obtain the bus owner lock as clocking the bus involves bus transactions
    BusOwnerLock busOwnerLock(*this, BUS_OWNER_LOCK_MAX_WAIT_MS);
    if (!busOwnerLock.isLocked())
    {
        LOG_W(MODULE_PREFIX, "clearBusStuck failed to obtain bus owner lock");
        return !_busStuckHandler.isStuck();
    }
    _busStuckHandler.clearStuckByClocking();
    return !_busStuckHandler.isStuck();
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Take the bus owner lock (recursive)
/// @param maxWaitMs max time to wait (RAFT_MUTEX_WAIT_FOREVER to wait forever)
/// @return true if the lock was obtained (busOwnerLockGive() must then be called)
bool BusI2C::busOwnerLockTake(uint32_t maxWaitMs)
{
    // Check mutex was created
    if (!_busOwnerMutex)
        return false;
    TickType_t ticksToWait = 0;
    if (maxWaitMs == RAFT_MUTEX_WAIT_FOREVER)
    {
        ticksToWait = portMAX_DELAY;
    }
    else if (maxWaitMs != 0)
    {
        // Ensure a non-zero wait never degrades to a try-lock (when tick rate < 1kHz)
        ticksToWait = pdMS_TO_TICKS(maxWaitMs);
        if (ticksToWait == 0)
            ticksToWait = 1;
    }
    return xSemaphoreTakeRecursive(_busOwnerMutex, ticksToWait) == pdTRUE;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Give the bus owner lock
void BusI2C::busOwnerLockGive()
{
    if (_busOwnerMutex)
        xSemaphoreGiveRecursive(_busOwnerMutex);
}
