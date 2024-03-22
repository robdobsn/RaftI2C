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

// Warnings
#define WARN_ON_REQUEST_BUFFER_FULL
#define WARN_ON_RESPONSE_BUFFER_FULL

// Debug
// #define DEBUG_BUS_I2C_POLLING
// #define DEBUG_ADD_TO_QUEUED_REC_FIFO
// #define DEBUG_QUEUED_COMMANDS
// #define DEBUG_ONE_ADDR 0x1d
// #define DEBUG_NO_POLLING
// #define DEBUG_POLL_TIME_FOR_ADDR 0x1d
// #define DEBUG_I2C_SEND_HELPER
// #define DEBUG_I2C_LENGTH_MISMATCH_WITH_BUTTON_A_PIN
// #define DEBUG_SERVICE_RESPONSE
// #define DEBUG_BUS_HIATUS

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Constructor / Destructor
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

BusI2C::BusI2C(BusElemStatusCB busElemStatusCB, BusOperationStatusCB busOperationStatusCB,
                RaftI2CCentralIF* pI2CCentralIF)
    : BusBase(busElemStatusCB, busOperationStatusCB),
        _busStatusMgr(*this),
        _busScanner(_busStatusMgr, 
            std::bind(&BusI2C::i2cSendHelper, this, std::placeholders::_1, std::placeholders::_2) 
        )
{
    // Init
    _lastI2CCommsUs = micros();

    // Mutex for polling list
    _pollingMutex = xSemaphoreCreateMutex();

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

    // Remove mutex
    if (_pollingMutex)
        vSemaphoreDelete(_pollingMutex);
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

    // Obtain semaphore to polling vector
    if (xSemaphoreTake(_pollingMutex, pdMS_TO_TICKS(10)) == pdTRUE)
    {
        _scheduler.clear();
        xSemaphoreGive(_pollingMutex);
    }

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
    _lowLoadBus = config.getLong("lowLoad", 0) != 0;

    // Bus status manager
    _busStatusMgr.setup(config);

    // Setup bus scanner
    _busScanner.setup(config);

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

    // Stats
    _busStats.respQueueCount(_responseQueue.count());
    _busStats.reqQueueCount(_requestQueue.count());

    // See if there are any results awaiting callback
    for (uint32_t i = 0; i < RESPONSE_FIFO_SLOTS; i++)
    {
        // Get response if available
        BusRequestResult reqResult;
        if (!_responseQueue.get(reqResult))
            break;

        // Check if callback is required
        BusRequestCallbackType callback = reqResult.getCallback();
        if (callback)
        {
            callback(reqResult.getCallbackParam(), reqResult);
#ifdef DEBUG_SERVICE_RESPONSE
            LOG_I(MODULE_PREFIX, "service response retval %d addr 0x%02x readLen %d", 
                    reqResult.isResultOk(), reqResult.getAddress(), reqResult.getReadDataLen());
#endif
        }
    }

    // Service bus scanner
    _busScanner.service();

    // Service bus status change detection
    _busStatusMgr.service(_pI2CCentral ? _pI2CCentral->isOperatingOk() : false);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Clear
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void BusI2C::clear(bool incPolling)
{
    // Check init
    if (!_initOk)
        return;

    // Clear the action queue
    _responseQueue.clear();

    // Clear polling list if required
    if (incPolling)
    {
        // We're going to mess with the polling list so obtain the semaphore
        if (xSemaphoreTake(_pollingMutex, pdMS_TO_TICKS(10)) == pdTRUE)
        {
            // Clear all lists
            _scheduler.clear();
            _pollingVector.clear();

            // Return semaphore
            xSemaphoreGive(_pollingMutex);
        }
    }
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
            _busScanner.service();
        }
#endif

        // Get from request FIFO
        BusI2CRequestRec reqRec;
        if (_requestQueue.get(reqRec))
        {
            // Debug
#ifdef DEBUG_QUEUED_COMMANDS
            String writeDataStr;
            Raft::getHexStrFromBytes(reqRec.getWriteData(), reqRec.getWriteDataLen(), writeDataStr);
            LOG_I(MODULE_PREFIX, "i2cWorkerTask reqQ got addr %02x write %s", reqRec.getAddress(), writeDataStr.c_str());
#endif

            // Debug one address only
#ifdef DEBUG_ONE_ADDR
            if (reqRec.getAddress() != DEBUG_ONE_ADDR)
                continue;
#endif

            // Check if paused
            if (_isPaused)
            {
                // Check is firmware update
                if (reqRec.isFWUpdate() || reqRec.shouldSendIfPaused())
                {
                    // Make the request
                    i2cSendHelper(&reqRec, 0);
                    // LOG_I(MODULE_PREFIX, "worker sending fw len %d", reqRec.getWriteDataLen());
                }
                else
                {
                    // Debug
                    // LOG_I(MODULE_PREFIX, "worker not sending as paused");
                }
            }
            else
            {
                // Make the request
                i2cSendHelper(&reqRec, 0);
            }
        }

        // Don't do any polling when paused
        if (_isPaused)
            continue;

#ifdef DEBUG_NO_POLLING
        continue;
#endif

        // Obtain semaphore to polling vector
        if (xSemaphoreTake(_pollingMutex, 0) == pdTRUE)
        {
            // Get the next element to poll
            int pollListIdx = _scheduler.getNext();
            if (pollListIdx >= 0)
            {
                // Check valid - if list is empty or has shrunk this test can fail
                // Polling can be suspended if too many failures occur
                BusI2CRequestRec* pReqRec = NULL;
                if ((pollListIdx < _pollingVector.size()) &&
                                (_pollingVector[pollListIdx].suspendCount < MAX_CONSEC_FAIL_POLLS_BEFORE_SUSPEND))
                {
                    // Get request details
                    pReqRec = &_pollingVector[pollListIdx].pollReq;
                }

                // Check ready to poll
                if (pReqRec)
                {
                    // Debug poll timing
#ifdef DEBUG_POLL_TIME_FOR_ADDR
                    if (pReqRec->isPolling() && (pReqRec->getAddress() == DEBUG_POLL_TIME_FOR_ADDR))
                    {
                        LOG_I(MODULE_PREFIX, "i2cWorker polling addr %02x elapsed %ld", 
                                    pReqRec->getAddress(), 
                                    Raft::timeElapsed(millis(), _debugLastPollTimeMs));
                        _debugLastPollTimeMs = millis();
                    }
#endif
                    // Send poll request
                    RaftI2CCentralIF::AccessResultCode sendResult = i2cSendHelper(pReqRec, pollListIdx);
                    // Check for failed send and not barred temporarily
                    if ((sendResult != RaftI2CCentralIF::ACCESS_RESULT_OK) && (sendResult != RaftI2CCentralIF::ACCESS_RESULT_BARRED))
                    {
                        // Increment the suspend count if required
                        if (pollListIdx < _pollingVector.size())
                            if (_pollingVector[pollListIdx].suspendCount < MAX_CONSEC_FAIL_POLLS_BEFORE_SUSPEND)
                                _pollingVector[pollListIdx].suspendCount++;
                    }
                }
            }

            // Free the semaphore
            xSemaphoreGive(_pollingMutex);
        }
    }

    LOG_I(MODULE_PREFIX, "i2cWorkerTask exiting");

    // Task has exited
    _i2cWorkerTaskHandle = nullptr;
    vTaskDelete(NULL);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Helper for sending over the bus
//
// Note: this is called from the worker task/thread so need to consider the response queue availability
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

RaftI2CCentralIF::AccessResultCode BusI2C::i2cSendHelper(BusI2CRequestRec* pReqRec, uint32_t pollListIdx)
{
#ifdef DEBUG_I2C_SEND_HELPER
    LOG_I(MODULE_PREFIX, "I2CSendHelper addr %02x writeLen %d readLen %d reqType %d pollListIdx %d",
                    pReqRec->getAddress(), pReqRec->getWriteDataLen(),
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
    RaftI2CAddrAndSlot addrAndSlot = pReqRec->getAddrAndSlot();
    if (_busStatusMgr.barElemAccessGet(addrAndSlot))
        return RaftI2CCentralIF::ACCESS_RESULT_BARRED;

    // Buffer for read
    uint32_t readReqLen = pReqRec->getReadReqLen();
    uint8_t readBuf[readReqLen];
    uint32_t writeReqLen = pReqRec->getWriteDataLen();
    uint32_t cmdId = pReqRec->getCmdId();
    uint32_t barAccessAfterSendMs = pReqRec->getBarAccessForMsAfterSend();

    // TODO handle addresses with slot specified
    uint16_t address = addrAndSlot.addr;

    // Access the bus
    uint32_t numBytesRead = 0;
    RaftI2CCentralIF::AccessResultCode rsltCode = RaftI2CCentralIF::AccessResultCode::ACCESS_RESULT_NOT_INIT;
    if (!_pI2CCentral)
        return rsltCode;
    rsltCode = _pI2CCentral->access(address, pReqRec->getWriteData(), writeReqLen, 
            readBuf, readReqLen, numBytesRead);

    // Handle bus element state changes - online/offline/etc
    bool accessOk = rsltCode == RaftI2CCentralIF::ACCESS_RESULT_OK;
    _busStatusMgr.handleBusElemStateChanges(addrAndSlot, accessOk);

    // Add the response to the response queue unless it was only a scan
    if (!pReqRec->isScan())
    {
        // Check if a response was expected but read length doesn't match
        if (readReqLen != numBytesRead)
        {
            // Stats
            _busStats.respLengthError();

#ifdef DEBUG_I2C_LENGTH_MISMATCH_WITH_BUTTON_A_PIN
            digitalWrite(5, 1);
            pinMode(5, OUTPUT);
            delayMicroseconds(10);
            digitalWrite(5, 0);
            delayMicroseconds(10);
            digitalWrite(5, 1);
#endif
        }
        else
        {
            // Create response
            BusRequestResult reqResult(address, cmdId, readBuf, readReqLen, accessOk,
                                pReqRec->getCallback(), pReqRec->getCallbackParam());

            // Check polling
            if (pReqRec->isPolling())
            {
                // Poll complete stats
                _busStats.pollComplete();

                // Check if callback is required
                BusRequestCallbackType callback = reqResult.getCallback();
                if (callback)
                {
                    callback(reqResult.getCallbackParam(), reqResult);
                    // LOG_D(MODULE_PREFIX, "service retval %d", reqResult.isResultOk());
                }
            }
            else
            {
                // Add to the response queue
                if (_responseQueue.put(reqResult, ADD_RESP_TO_QUEUE_MAX_MS))
                {
                    _busStats.cmdComplete();
                }
                else
                {
                    // Warn
#ifdef WARN_ON_RESPONSE_BUFFER_FULL
                    if (Raft::isTimeout(millis(), _respBufferFullLastWarnMs, BETWEEN_BUF_FULL_WARNINGS_MIN_MS))
                    {
                        int msgsWaiting = _responseQueue.count();
                        LOG_W(MODULE_PREFIX, "sendHelper %s resp buffer full - waiting %d",
                                _busName.c_str(), msgsWaiting
                            );
                        _respBufferFullLastWarnMs = millis();
                    }
#endif

                    // Stats
                    _busStats.respBufferFull();
                }
            }
        }
    }

    // Bar access to element if requested
    if (barAccessAfterSendMs > 0)
        _busStatusMgr.barElemAccessSet(addrAndSlot, barAccessAfterSendMs);

    // Record time of comms
    _lastI2CCommsUs = micros();
    return rsltCode;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Add a queued request
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool BusI2C::addRequest(BusRequestInfo& busReqInfo)
{
    // Check if this is a polling request
    if (busReqInfo.isPolling())
    {
        // Add to the polling list and update the scheduler
        return addToPollingList(busReqInfo);
    }
    // Add to queued request FIFO
    return addToQueuedReqFIFO(busReqInfo);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Add to the polling list
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool BusI2C::addToPollingList(BusRequestInfo& busReqInfo)
{
#ifdef DEBUG_BUS_I2C_POLLING
    LOG_I(MODULE_PREFIX, "addToPollingList elemAddr %x freqHz %.1f", busReqInfo.getAddressUint32(), busReqInfo.getPollFreqHz());
#endif

    // We're going to mess with the polling list so obtain the semaphore
    if (xSemaphoreTake(_pollingMutex, pdMS_TO_TICKS(50)) == pdTRUE)
    {
        // See if already in the list
        bool addedOk = false;
        for (PollingVectorItem& pollItem : _pollingVector)
        {
            if (pollItem.pollReq.getAddrAndSlot() == 
                        RaftI2CAddrAndSlot::fromCompositeAddrAndSlot(busReqInfo.getAddressUint32()))
            {
                // Replace request record
                addedOk = true;
                pollItem.pollReq = BusI2CRequestRec(busReqInfo);
                pollItem.suspendCount = 0;
                break;
            }
        }

        // Append
        if (!addedOk)
        {
            // Check limit on polling list size
            uint32_t maxPollListSize = _lowLoadBus ? MAX_POLLING_LIST_RECS_LOW_LOAD : MAX_POLLING_LIST_RECS;
            if (_pollingVector.size() < maxPollListSize)
            {
                // Create new record to track polling
                PollingVectorItem newPollingItem;
                newPollingItem.pollReq.set(busReqInfo);
                
                // Add to the polling list
                _pollingVector.push_back(newPollingItem);
                addedOk = true;
            }
        }

        // Update scheduler with the polling list if required
        if (addedOk)
        {
            _scheduler.clear();
            for (PollingVectorItem& pollItem : _pollingVector)
                _scheduler.addNode(pollItem.pollReq.getPollFreqHz());
        }

        // Return semaphore
        xSemaphoreGive(_pollingMutex);
        return true;
    }
    return false;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Add to the queued request FIFO
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool BusI2C::addToQueuedReqFIFO(BusRequestInfo& busReqInfo)
{
    // Check init
    if (!_initOk)
        return false;

    // Result
    bool retc = false;

    // Send to the request FIFO
    BusI2CRequestRec reqRec;
    reqRec.set(busReqInfo);
    retc = _requestQueue.put(reqRec, ADD_REQ_TO_QUEUE_MAX_MS);

#ifdef DEBUG_ADD_TO_QUEUED_REC_FIFO
    // Debug
    String writeDataStr;
    Raft::getHexStrFromBytes(reqRec.getWriteData(), reqRec.getWriteDataLen(), writeDataStr);
    LOG_I(MODULE_PREFIX, "addToQueuedRecFIFO addr %02x writeData %s readLen %d delayMs %d", 
                reqRec.getAddress(), writeDataStr.c_str(), reqRec.getReadReqLen(), reqRec.getBarAccessForMsAfterSend());
#endif

    // Msg buffer full
    if (retc != pdTRUE)
    {
        _busStats.reqBufferFull();

#ifdef WARN_ON_REQUEST_BUFFER_FULL
        if (Raft::isTimeout(millis(), _reqBufferFullLastWarnMs, BETWEEN_BUF_FULL_WARNINGS_MIN_MS))
        {
            int msgsWaiting = _requestQueue.count();
            LOG_W(MODULE_PREFIX, "addToQueuedReqFIFO %s req buffer full - waiting %d", 
                    _busName.c_str(), msgsWaiting
                );
            _reqBufferFullLastWarnMs = millis();
        }
#endif
    }

    return retc;
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
