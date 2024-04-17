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
#include "esp_task_wdt.h"
#include "BusI2CConsts.h"

static const char* MODULE_PREFIX = "BusI2C";

#ifdef I2C_USE_RAFT_I2C
#include "RaftI2CCentral.h"
#else
#include "BusI2CESPIDF.h"
#endif

// Magic constant controlling the yield rate of the I2C main worker
// This affects the number of I2C communications that occur in a cluster
// As of 2021-11-12 this is set conservatively to ensure two commnunications with 
// the same peripheral don't happen too close together (min is around 1ms - with 1
// loop before yield)
// Note: 10 seems the logical number because there are 9 servos and 1 accelerometer
// and all should be polled quickly but this would require more granular control
// of sending to slower peripherals as they can currently get swamped by multiple
// commands being sent with very little time between
static const uint32_t WORKER_LOOPS_BEFORE_YIELDING = 1;

// The following will define a minimum time between I2C comms activities
// #define ENFORCE_MIN_TIME_BETWEEN_I2C_COMMS_US 1

// Debug
// #define DEBUG_NO_POLLING
// #define DEBUG_I2C_ASYNC_SEND_HELPER
// #define DEBUG_I2C_SYNC_SEND_HELPER
// #define DEBUG_BUS_HIATUS

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Constructor / Destructor
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

BusI2C::BusI2C(BusElemStatusCB busElemStatusCB, BusOperationStatusCB busOperationStatusCB,
                RaftI2CCentralIF* pI2CCentralIF)
    : BusBase(busElemStatusCB, busOperationStatusCB),
        _busStatusMgr(*this),
        _busExtenderMgr(
            std::bind(&BusI2C::i2cSendSync, this, std::placeholders::_1, std::placeholders::_2)
        ),
        _deviceIdentMgr(_busExtenderMgr,
            std::bind(&BusI2C::i2cSendSync, this, std::placeholders::_1, std::placeholders::_2)
        ),
        _busScanner(_busStatusMgr, _busExtenderMgr, _deviceIdentMgr,
            std::bind(&BusI2C::i2cSendSync, this, std::placeholders::_1, std::placeholders::_2) 
        ),
        _devicePollingMgr(_busStatusMgr, _busExtenderMgr,
            std::bind(&BusI2C::i2cSendSync, this, std::placeholders::_1, std::placeholders::_2)
        ),
        _busAccessor(*this,
            std::bind(&BusI2C::i2cSendAsync, this, std::placeholders::_1, std::placeholders::_2)
        )
{
    // Init
    _lastI2CCommsUs = micros();

    // Clear barring
    for (uint32_t i = 0; i < ELEM_BAR_I2C_ADDRESS_MAX; i++)
        _busAccessBarMs[i] = 0;

    // Set the interface
    _pI2CCentral = pI2CCentralIF;
    if (!_pI2CCentral)
    {
#ifdef I2C_USE_RAFT_I2C
        _pI2CCentral = new RaftI2CCentral();
#else
        _pI2CCentral = new BusI2CESPIDF();
#endif
        _i2cCentralNeedsToBeDeleted = true;
    }
}

BusI2C::~BusI2C()
{
    // Close to stop task
    close();

    // Clean up
    if (_i2cCentralNeedsToBeDeleted)
        delete _pI2CCentral;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Setup
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool BusI2C::setup(const RaftJsonIF& config)
{
    // Note:
    // No attempt is made here to clean-up properly
    // The assumption is that if robot configuration is completely changed then
    // the firmware will be restarted from scratch

    // Check if already configured
    if (_initOk)
        return false;

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

    // Bus status manager
    _busStatusMgr.setup(config);

    // Bus extender setup
    _busExtenderMgr.setup(config);

    // Device ident manager
    _deviceIdentMgr.setup(config);

    // Setup bus scanner
    _busScanner.setup(config);

    // Setup device polling manager
    _devicePollingMgr.setup(config); 

    // Setup bus accessor
    _busAccessor.setup(config);

    // Check valid
    if ((_sdaPin < 0) || (_sclPin < 0))
    {
        LOG_W(MODULE_PREFIX, "setup INVALID PARAMS name %s port %d SDA %d SCL %d FREQ %d", _busName.c_str(), _i2cPort, _sdaPin, _sclPin, _freq);
        return false;
    }

    // Initialise bus
    if (!_pI2CCentral)
    {
        LOG_W(MODULE_PREFIX, "setup FAILED no device");
        return false;
    }

    // Init the I2C device
    if (!_pI2CCentral->init(_i2cPort, _sdaPin, _sclPin, _freq, _i2cFilter))
    {
        LOG_W(MODULE_PREFIX, "setup FAILED name %s port %d SDA %d SCL %d FREQ %d", _busName.c_str(), _i2cPort, _sdaPin, _sclPin, _freq);
        return false;
    }

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
    LOG_I(MODULE_PREFIX, "task setup %s(%d) name %s port %d SDA %d SCL %d FREQ %d FILTER %d portTICK_PERIOD_MS %d taskCore %d taskPriority %d stackBytes %d",
                (retc == pdPASS) ? "OK" : "FAILED", retc, _busName.c_str(), _i2cPort,
                _sdaPin, _sclPin, _freq, _i2cFilter, 
                portTICK_PERIOD_MS, taskCore, taskPriority, taskStackSize);

    // Ok
    return true;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Close
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

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
// Service
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void BusI2C::service()
{
    // Check ok
    if (!_initOk)
        return;

    // Service bus scanner
    _busScanner.service();

    // Service bus status change detection
    _busStatusMgr.service(_pI2CCentral ? _pI2CCentral->isOperatingOk() : false);

    // Service bus extender
    _busExtenderMgr.service();

    // Service bus accessor
    _busAccessor.service();
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Clear
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void BusI2C::clear(bool incPolling)
{
    // Check init
    if (!_initOk)
        return;

    // Clear accessor
    _busAccessor.clear(incPolling);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// RTOS task that handles all bus interaction
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Task that runs indefinitely to operate the bus
void BusI2C::i2cWorkerTaskStatic(void* pvParameters)
{
    // Get the object that requested the task
    BusI2C* pObjPtr = (BusI2C*)pvParameters;
    if (pObjPtr)
        pObjPtr->i2cWorkerTask();
}

void BusI2C::i2cWorkerTask()
{
    uint32_t yieldCount = 0;
    _debugLastBusLoopMs = millis();
    while (ulTaskNotifyTake(pdTRUE, 0) == 0)
    {        
        // Allow other threads a look in - this time is in freeRTOS ticks
        // The queue and semaphore below have 0 ticksToWait so they will not block
        // Hence we need to block to avoid starving other processes
        // It may be that there is a better way to do this - e.g. blocking on a compound flag like WiFi code does?
        yieldCount++;
        if (yieldCount == WORKER_LOOPS_BEFORE_YIELDING)
        {
            yieldCount = 0;
            vTaskDelay(1);
        }

        // Stats
        _busStats.activity();

        // Check I2C initialised
        if (!_initOk)
            continue;

        // Check bus hiatus
        if (_hiatusActive)
        {
            if (!Raft::isTimeout(millis(), _hiatusStartMs, _hiatusForMs))
                continue;
            _hiatusActive = false;
#ifdef DEBUG_BUS_HIATUS
            LOG_I(MODULE_PREFIX, "i2cWorkerTask hiatus over");
#endif
        }

        // Check pause status - request
        if ((_isPaused) && (!_pauseRequested))
            _isPaused = false;
        else if ((!_isPaused) && (_pauseRequested))
            _isPaused = true;

        // Handle bus scanning
#ifndef DEBUG_NO_SCANNING
        if (!_isPaused)
        {
            _busScanner.taskService();
        }
#endif

        // Handle requests
        _busAccessor.processRequestQueue(_isPaused);

        // Don't do any polling when paused
        if (_isPaused)
            continue;

#ifdef DEBUG_NO_POLLING
        continue;
#endif

        // Device polling
        _devicePollingMgr.taskService(micros());

        // Perform polling
        // TODO - remove or reconsider how polling works
        _busAccessor.processPolling();
    }

    LOG_I(MODULE_PREFIX, "i2cWorkerTask exiting");

    // Task has exited
    _i2cWorkerTaskHandle = nullptr;
    vTaskDelete(NULL);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Helper for sending over the bus (async)
//
// Note: this is called from the worker task/thread so need to consider the response queue availability
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

RaftI2CCentralIF::AccessResultCode BusI2C::i2cSendAsync(const BusI2CRequestRec* pReqRec, uint32_t pollListIdx)
{
#ifdef DEBUG_I2C_ASYNC_SEND_HELPER
    LOG_I(MODULE_PREFIX, "I2CSendAsync addr@slot+1 %s writeLen %d readLen %d reqType %d pollListIdx %d",
                    pReqRec->getAddrAndSlot().toString().c_str(), pReqRec->getWriteDataLen(),
                    pReqRec->getReadReqLen(), pReqRec->getReqType(), pollListIdx);
#endif

#ifdef ENFORCE_MIN_TIME_BETWEEN_I2C_COMMS_US
    // Check the last time a communication occurred - if less than the minimum between sends
    // then delay
    while (!Raft::isTimeout(micros(), _lastI2CCommsUs, MIN_TIME_BETWEEN_I2C_COMMS_US))
    {
    }
#endif

    // Check if this address is barred for a period
    BusI2CAddrAndSlot addrAndSlot = pReqRec->getAddrAndSlot();
    if (_busStatusMgr.barElemAccessGet(millis(), addrAndSlot))
        return RaftI2CCentralIF::ACCESS_RESULT_BARRED;

    // Check if a bus extender slot is specified
    if (addrAndSlot.slotPlus1 > 0)
        _busExtenderMgr.enableOneSlot(addrAndSlot.slotPlus1);

    // Buffer for read
    uint32_t readReqLen = pReqRec->getReadReqLen();
    uint8_t readBuf[readReqLen];
    uint32_t writeReqLen = pReqRec->getWriteDataLen();
    uint32_t barAccessAfterSendMs = pReqRec->getBarAccessForMsAfterSend();

    // Get address
    uint32_t address = addrAndSlot.addr;

    // Access the bus
    uint32_t numBytesRead = 0;
    RaftI2CCentralIF::AccessResultCode rsltCode = RaftI2CCentralIF::AccessResultCode::ACCESS_RESULT_NOT_INIT;
    if (!_pI2CCentral)
        return rsltCode;
    rsltCode = _pI2CCentral->access(address, pReqRec->getWriteData(), writeReqLen, 
            readBuf, readReqLen, numBytesRead);

    // Restore the bus extender(s) if necessary
    if (addrAndSlot.slotPlus1 > 0)
        _busExtenderMgr.setAllChannels(true);

    // Check for scanning
    if (!pReqRec->isScan())
    {
        // If not scanning handle the response (there is no response for scanning)
        _busAccessor.handleResponse(pReqRec, rsltCode, readBuf, numBytesRead);
    }

    // Bar access to element if requested
    if (barAccessAfterSendMs > 0)
        _busStatusMgr.barElemAccessSet(millis(), addrAndSlot, barAccessAfterSendMs);

    // Record time of comms
    _lastI2CCommsUs = micros();
    return rsltCode;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Helper for sending over the bus (sync)
//
// Note: this is called from the worker task/thread so need to consider the response queue availability
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

RaftI2CCentralIF::AccessResultCode BusI2C::i2cSendSync(const BusI2CRequestRec* pReqRec, std::vector<uint8_t>* pReadData)
{
#ifdef DEBUG_I2C_SYNC_SEND_HELPER
    LOG_I(MODULE_PREFIX, "I2CSendSync addr@slot+1 writeLen %d readLen %d reqType %d",
                    pReqRec->getAddrAndSlot().toString().c_str(), pReqRec->getWriteDataLen(),
                    pReqRec->getReadReqLen(), pReqRec->getReqType());
#endif

    // Check if this address is barred for a period
    BusI2CAddrAndSlot addrAndSlot = pReqRec->getAddrAndSlot();
    if (_busStatusMgr.barElemAccessGet(millis(), addrAndSlot))
        return RaftI2CCentralIF::ACCESS_RESULT_BARRED;

    // Buffer for read
    uint32_t readReqLen = 0;
    uint8_t pDummyReadBuf[1];
    if (pReadData && pReqRec->getReadReqLen() > 0)
    {
        readReqLen = pReqRec->getReadReqLen();
        pReadData->resize(readReqLen);
    }
    uint32_t writeReqLen = pReqRec->getWriteDataLen();
    uint32_t barAccessAfterSendMs = pReqRec->getBarAccessForMsAfterSend();

    // Access the bus
    uint32_t numBytesRead = 0;
    RaftI2CCentralIF::AccessResultCode rsltCode = RaftI2CCentralIF::AccessResultCode::ACCESS_RESULT_NOT_INIT;
    if (!_pI2CCentral)
        return rsltCode;
    rsltCode = _pI2CCentral->access(addrAndSlot.addr, pReqRec->getWriteData(), writeReqLen, 
            pReadData ? pReadData->data() : pDummyReadBuf, readReqLen, numBytesRead);

    // Bar access to element if requested
    if (barAccessAfterSendMs > 0)
        _busStatusMgr.barElemAccessSet(millis(), addrAndSlot, barAccessAfterSendMs);

    // Record time of comms
    _lastI2CCommsUs = micros();
    return rsltCode;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Is elem reponding
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool BusI2C::isElemResponding(uint32_t address, bool* pIsValid)
{
    if (pIsValid)
        *pIsValid = true;
    return _busStatusMgr.isElemOnline(BusI2CAddrAndSlot::fromCompositeAddrAndSlot(address)) == BusOperationStatus::BUS_OPERATION_OK;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Request bus scan
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void BusI2C::requestScan(bool enableSlowScan, bool requestFastScan)
{
    _busScanner.requestScan(enableSlowScan, requestFastScan);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Hiatus for period of ms
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

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
/// @brief Get JSON for device type info
/// @param address Address of element
/// @return JSON string
String BusI2C::getDevTypeInfoJsonByAddr(uint32_t address, bool includePlugAndPlayInfo) const
{
    // Get device type index
    uint16_t deviceTypeIdx = _busStatusMgr.getDeviceTypeIndexByAddr(BusI2CAddrAndSlot::fromCompositeAddrAndSlot(address));
    if (deviceTypeIdx == DeviceStatus::DEVICE_TYPE_INDEX_INVALID)
        return "{}";

    // Get device type info
    return _deviceIdentMgr.getDevTypeInfoJsonByTypeIdx(deviceTypeIdx, includePlugAndPlayInfo);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get JSON for device type info
/// @param deviceType Device type
/// @return JSON string
String BusI2C::getDevTypeInfoJsonByTypeName(const String& deviceType, bool includePlugAndPlayInfo) const
{
    // Get device type info
    return _deviceIdentMgr.getDevTypeInfoJsonByTypeName(deviceType, includePlugAndPlayInfo);
}

