/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// I2C Bus Handler
//
// Rob Dobson 2020
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include <Logger.h>
#include <BusI2C.h>
#include <ConfigBase.h>
#include <ConfigPinMap.h>
#include <RaftArduino.h>
#include "esp_task_wdt.h"

static const char* MODULE_PREFIX = "BusI2C";

// Use replacement I2C library - if not defined use original ESP IDF I2C implementation
#define I2C_USE_RAFT_I2C

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
// #define DEBUG_CONSECUTIVE_ERROR_HANDLING
// #define DEBUG_CONSECUTIVE_ERROR_HANDLING_ADDR 0x55
// #define DEBUG_LOCKUP_DETECTION
// #define DEBUG_ADD_TO_QUEUED_REC_FIFO
// #define DEBUG_ACCESS_BARRING_FOR_MS
// #define DEBUG_ELEM_SCAN_STATUS
// #define DEBUG_QUEUED_COMMANDS
// #define DEBUG_ONE_ADDR 0x1d
// #define DEBUG_NO_SCANNING
// #define DEBUG_NO_POLLING
// #define DEBUG_POLL_TIME_FOR_ADDR 0x1d
// #define DEBUG_I2C_SEND_HELPER
// #define DEBUG_I2C_LENGTH_MISMATCH_WITH_BUTTON_A_PIN
// #define DEBUG_SERVICE_RESPONSE
// #define DEBUG_SERVICE_BUS_ELEM_STATUS_CHANGE
// #define DEBUG_BUS_HIATUS
// #define DEBUG_DISABLE_INITIAL_FAST_SCAN

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construct / Destruct
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

BusI2C::BusI2C(BusElemStatusCB busElemStatusCB, BusOperationStatusCB busOperationStatusCB,
                RaftI2CCentralIF* pI2CInterface)
    : BusBase(busElemStatusCB, busOperationStatusCB)
{
    // Init
    _lastI2CCommsUs = micros();
    _busScanLastMs = millis();

    // Mutex for polling list
    _pollingMutex = xSemaphoreCreateMutex();

    // Bus element status change detection
    _busElemStatusMutex = xSemaphoreCreateMutex();

    // Clear barring
    for (uint32_t i = 0; i < ELEM_BAR_I2C_ADDRESS_MAX; i++)
        _busAccessBarMs[i] = 0;

    // Set the interface
    _pI2CDevice = pI2CInterface;
    if (!_pI2CDevice)
    {
#ifdef I2C_USE_RAFT_I2C
        _pI2CDevice = new RaftI2CCentral();
#else
        _pI2CDevice = new BusI2CESPIDF();
#endif
        _i2cDeviceNeedsDeleting = true;
    }

#ifdef DEBUG_DISABLE_INITIAL_FAST_SCAN
    _fastScanPendingCount = 0;
#endif
}

BusI2C::~BusI2C()
{
    // Close to stop task
    close();

    // Clean up
    if (_i2cDeviceNeedsDeleting)
        delete _pI2CDevice;

    // Remove mutex
    if (_pollingMutex)
        vSemaphoreDelete(_pollingMutex);
    if (_busElemStatusMutex)
        vSemaphoreDelete(_busElemStatusMutex);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Setup
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool BusI2C::setup(ConfigBase& config, const char* pConfigPrefix)
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
    _i2cPort = config.getLong("i2cPort", 0, pConfigPrefix);
    String pinName = config.getString("sdaPin", "", pConfigPrefix);
    _sdaPin = ConfigPinMap::getPinFromName(pinName.c_str());
    pinName = config.getString("sclPin", "", pConfigPrefix);
    _sclPin = ConfigPinMap::getPinFromName(pinName.c_str());
    _freq = config.getLong("i2cFreq", 100000, pConfigPrefix);
    _i2cFilter = config.getLong("i2cFilter", RaftI2CCentralIF::DEFAULT_BUS_FILTER_LEVEL, pConfigPrefix);
    _busName = config.getString("name", "", pConfigPrefix);
    UBaseType_t taskCore = config.getLong("taskCore", DEFAULT_TASK_CORE, pConfigPrefix);
    BaseType_t taskPriority = config.getLong("taskPriority", DEFAULT_TASK_PRIORITY, pConfigPrefix);
    int taskStackSize = config.getLong("taskStack", DEFAULT_TASK_STACK_SIZE_BYTES, pConfigPrefix);
    _lowLoadBus = config.getLong("lowLoad", 0, pConfigPrefix) != 0;

    // Scan boost
    std::vector<String> scanBoostAddrStrs;
    config.getArrayElems("scanBoost", scanBoostAddrStrs, pConfigPrefix);
    _scanBoostAddresses.resize(scanBoostAddrStrs.size());
    for (uint32_t i = 0; i < scanBoostAddrStrs.size(); i++)
    {
        _scanBoostAddresses[i] = strtoul(scanBoostAddrStrs[i].c_str(), nullptr, 0);
        // LOG_I(MODULE_PREFIX, "setup scanBoost %02x", _scanBoostAddresses[i]);
    }
    _scanBoostCount = 0;

    // Get address to use for lockup detection
    _addrForLockupDetect = 0;
    _addrForLockupDetectValid = false;
#ifdef I2C_USE_RAFT_I2C
    uint32_t address = strtoul(config.getString("lockupDetect", "0xffffffff", pConfigPrefix).c_str(), NULL, 0);
    if (address != 0xffffffff)
    {
        _addrForLockupDetect = address;
        _addrForLockupDetectValid = true;
    }
#endif
    _busOperationStatus = BUS_OPERATION_UNKNOWN;

    // Check valid
    if ((_sdaPin < 0) || (_sclPin < 0))
    {
        LOG_W(MODULE_PREFIX, "setup INVALID PARAMS name %s port %d SDA %d SCL %d FREQ %d", _busName.c_str(), _i2cPort, _sdaPin, _sclPin, _freq);
        return false;
    }

    // Initialise bus
    if (!_pI2CDevice)
    {
        LOG_W(MODULE_PREFIX, "setup FAILED no device");
        return false;
    }

    // Init the I2C device
    if (!_pI2CDevice->init(_i2cPort, _sdaPin, _sclPin, _freq, _i2cFilter))
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
    LOG_I(MODULE_PREFIX, "task setup %s(%d) name %s port %d SDA %d SCL %d FREQ %d FILTER %d lockupDetAddr %02x (valid %s) portTICK_PERIOD_MS %d taskCore %d taskPriority %d stackBytes %d",
                (retc == pdPASS) ? "OK" : "FAILED", retc, _busName.c_str(), _i2cPort,
                _sdaPin, _sclPin, _freq, _i2cFilter, 
                _addrForLockupDetect, _addrForLockupDetectValid ? "Y" : "N",
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

    // Obtain semaphore controlling access to busElemChange list and flag
    // so we can update bus and element operation status - don't worry if we can't
    // access the list as there will be other service loops
    std::vector<BusElemAddrAndStatus> statusChanges;
    BusOperationStatus newBusOperationStatus = _busOperationStatus; 
    if (xSemaphoreTake(_busElemStatusMutex, 0) == pdTRUE)
    {    
        // Check for any changes
        if (_busElemStatusChangeDetected)
        {
            // Go through once and look for changes
            uint32_t numChanges = 0;
            for (uint16_t i = 0; i <= BUS_SCAN_I2C_ADDRESS_MAX; i++)
            {
                if (_i2cAddrResponseStatus[i].isChange)
                    numChanges++;
            }

            // Create change vector if required
            if (numChanges > 0)
            {
                statusChanges.reserve(numChanges);
                for (uint16_t i2cAddr = 0; i2cAddr <= BUS_SCAN_I2C_ADDRESS_MAX; i2cAddr++)
                {
                    if (_i2cAddrResponseStatus[i2cAddr].isChange)
                    {
                        // Handle element change
                        BusElemAddrAndStatus statusChange = 
                            {
                                i2cAddr, 
                                _i2cAddrResponseStatus[i2cAddr].isOnline
                            };
#ifdef DEBUG_SERVICE_BUS_ELEM_STATUS_CHANGE
                        LOG_I(MODULE_PREFIX, "service addr 0x%02x status change to %s", 
                                    i2cAddr, statusChange.isChangeToOnline ? "online" : "offline");
#endif
                        statusChanges.push_back(statusChange);
                        _i2cAddrResponseStatus[i2cAddr].isChange = false;

                        // Check if this is the addrForLockupDetect
                        if (_addrForLockupDetectValid && (i2cAddr == _addrForLockupDetect) && (_i2cAddrResponseStatus[i2cAddr].isValid))
                        {
                            newBusOperationStatus = _i2cAddrResponseStatus[i2cAddr].isOnline ?
                                        BUS_OPERATION_OK : BUS_OPERATION_FAILING;
                        }
                    }
                }
            }

            // No more changes
            _busElemStatusChangeDetected = false;
        }

        // If no lockup detection addresses are used then rely on the bus's isOperatingOk() result
        if (!_addrForLockupDetectValid && _pI2CDevice)
        {
            newBusOperationStatus = _pI2CDevice->isOperatingOk() ?
                        BUS_OPERATION_OK : BUS_OPERATION_FAILING;
            
        }

        // Return semaphore
        xSemaphoreGive(_busElemStatusMutex);

        // Elem change callback if required
        if ((statusChanges.size() > 0) && _busElemStatusCB)
            _busElemStatusCB(*this, statusChanges);

        // Bus operation change callback if required
        if (_busOperationStatus != newBusOperationStatus)
        {
            _busOperationStatus = newBusOperationStatus;
            if (_busOperationStatusCB)
                _busOperationStatusCB(*this, _busOperationStatus);
        }
    }
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
            // Perform fast scans of the bus if requested
            while (_fastScanPendingCount > 0)
                scanNextAddress(true);

            // Perform regular scans at BUS_SCAN_PERIOD_MS intervals
            if (_slowScanEnabled)
            {
                if (Raft::isTimeout(millis(), _busScanLastMs, BUS_SCAN_PERIOD_MS))
                    scanNextAddress(false);
            }
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
    uint32_t address = pReqRec->getAddress() & 0x7f;
    if (_busAccessBarMs[address] != 0)
    {
        // Check if time has elapsed and ignore request if not
        if (millis() < _busAccessBarMs[address])
        {
#ifdef DEBUG_ACCESS_BARRING_FOR_MS
            LOG_W(MODULE_PREFIX, "i2cSendHelper access barred for address %02x for %ldms", address, _busAccessBarMs[address]-millis());
#endif
            return RaftI2CCentralIF::ACCESS_RESULT_BARRED;
        }
#ifdef DEBUG_ACCESS_BARRING_FOR_MS
        LOG_W(MODULE_PREFIX, "i2cSendHelper access bar released for address %02x for %ldms", address, _busAccessBarMs[address]-millis());
#endif
        _busAccessBarMs[address] = 0;
    }

    // Buffer for read
    uint32_t readReqLen = pReqRec->getReadReqLen();
    uint8_t readBuf[readReqLen];
    uint32_t writeReqLen = pReqRec->getWriteDataLen();
    uint32_t cmdId = pReqRec->getCmdId();
    uint32_t barAccessAfterSendMs = pReqRec->getBarAccessForMsAfterSend();

    // Access the bus
    uint32_t numBytesRead = 0;
    RaftI2CCentralIF::AccessResultCode rsltCode = RaftI2CCentralIF::AccessResultCode::ACCESS_RESULT_NOT_INIT;
    if (!_pI2CDevice)
        return rsltCode;
    rsltCode = _pI2CDevice->access(address, pReqRec->getWriteData(), writeReqLen, 
            readBuf, readReqLen, numBytesRead);

    // Handle bus element state changes - online/offline/etc
    bool accessOk = rsltCode == RaftI2CCentralIF::ACCESS_RESULT_OK;
    handleBusElemStateChanges(address, accessOk);

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

    #ifdef DEBUG_LOCKUP_DETECTION
                if (_addrForLockupDetectValid && (address == _addrForLockupDetect))
                {
                    LOG_I(MODULE_PREFIX, "i2cSendHelper lockup addr %02x accessOk %d readReqLen %d", 
                                address, accessOk, readReqLen);
                }
    #endif

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
    {
        _busAccessBarMs[address] = millis() + barAccessAfterSendMs;
        // Check we're not using the value that indicates no barring of access due to wrap-around
        // of milliseconds
        if (_busAccessBarMs[address] == 0)
            _busAccessBarMs[address] = 1;
#ifdef DEBUG_ACCESS_BARRING_FOR_MS
        LOG_W(MODULE_PREFIX, "i2cSendHelper barring bus access for address %02x for %dms", address, barAccessAfterSendMs);
#endif
    }

    // Record time of comms
    _lastI2CCommsUs = micros();
    return rsltCode;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Scan a single address
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void BusI2C::scanNextAddress(bool isFastScan)
{
    // Scan the addresses specified in scan-boost more frequently than normal
    uint8_t scanAddress = _busScanCurAddr;
    _scanBoostCount++;
    if ((_scanBoostCount >= SCAN_BOOST_FACTOR) && (_fastScanPendingCount == 0))
    {
        // Check if the scan addr index is beyond the end of the array
        if (_scanBoostCurAddrIdx >= _scanBoostAddresses.size())
        {
            // Reset the scan address index
            _scanBoostCurAddrIdx = 0;
            _scanBoostCount = 0;
        }
        else
        {
            scanAddress = _scanBoostAddresses[_scanBoostCurAddrIdx++];
        }
    }

    // Scan the address
    BusI2CRequestRec reqRec(isFastScan ? BUS_REQ_TYPE_FAST_SCAN : BUS_REQ_TYPE_SLOW_SCAN, scanAddress,
                0, 0, nullptr, 0, 0, nullptr, this);
    i2cSendHelper(&reqRec, 0);

    // Bump address if next in regular scan sequence
    if (scanAddress == _busScanCurAddr)
    {
        _busScanCurAddr++;
        if (_busScanCurAddr > BUS_SCAN_I2C_ADDRESS_MAX)
        {
            _busScanCurAddr = BUS_SCAN_I2C_ADDRESS_MIN;
            if (_fastScanPendingCount > 0)
                _fastScanPendingCount--;
        }
    }
    _busScanLastMs = millis();
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Add a request - queued or polling
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
            if (pollItem.pollReq.getAddress() == busReqInfo.getAddressUint32())
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
// Bus element state changes
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void BusI2C::handleBusElemStateChanges(uint32_t address, bool elemResponding)
{
    // Check valid
    if (address > BUS_SCAN_I2C_ADDRESS_MAX)
        return;

    // Obtain semaphore controlling access to busElemChange list and flag
    if (xSemaphoreTake(_busElemStatusMutex, pdMS_TO_TICKS(1)) == pdTRUE)
    {    
        // Init state change detection
        uint8_t count = _i2cAddrResponseStatus[address].count;
        bool isOnline = _i2cAddrResponseStatus[address].isOnline;
        bool isChange = _i2cAddrResponseStatus[address].isChange;
        bool isValid = _i2cAddrResponseStatus[address].isValid;

        // Check coming online - the count must reach ok level to indicate online
        // If a change is detected then we toggle the change indicator - if it was already
        // set then we must have a double-change which is equivalent to no change
        // We also set the _busElemStatusChangedDetected even if there might have been a
        // double-change as it is not safe to clear it because there may be other changes
        // - the service loop will sort out what has really happened
        if (elemResponding && !isOnline)
        {
            count = (count < I2C_ADDR_RESP_COUNT_OK_MAX) ? count+1 : count;
            if (count >= I2C_ADDR_RESP_COUNT_OK_MAX)
            {
                // Now online
                isChange = !isChange;
                count = 0;
                isOnline = true;
                isValid = true;
                _busElemStatusChangeDetected = true;
            }
        }
        else if (!elemResponding && (isOnline || !isValid))
        {
            // Bump the failure count indicator and check if we reached failure level
            count = (count < I2C_ADDR_RESP_COUNT_FAIL_MAX) ? count+1 : count;
            if (count >= I2C_ADDR_RESP_COUNT_FAIL_MAX)
            {
                // Now offline 
                isChange = !isChange;
                isOnline = false;
                isValid = true;
                _busElemStatusChangeDetected = true;
            }
        }
        
#ifdef DEBUG_CONSECUTIVE_ERROR_HANDLING
        uint32_t prevCount = count;
        bool prevOnline = isOnline;
        bool prevChange = isChange;
        bool prevValid = isValid;
#endif
        // Update status
        _i2cAddrResponseStatus[address].count = count;
        _i2cAddrResponseStatus[address].isOnline = isOnline;
        _i2cAddrResponseStatus[address].isChange = isChange;
        _i2cAddrResponseStatus[address].isValid = isValid;

        // Return semaphore
        xSemaphoreGive(_busElemStatusMutex);

#ifdef DEBUG_CONSECUTIVE_ERROR_HANDLING
#ifdef DEBUG_CONSECUTIVE_ERROR_HANDLING_ADDR
        if (address == DEBUG_CONSECUTIVE_ERROR_HANDLING_ADDR)
#endif
        if (isChange)
        {
            LOG_I(MODULE_PREFIX, "handleBusElemStateChanges addr %02x failCount %d(was %d) isOnline %d(was %d) isChange %d (was %d) isValid %d (was %d) paused %d isResponding %d",
                        address, count, prevCount, isOnline, prevOnline, isChange, prevChange, isValid, prevValid,
                        _isPaused, elemResponding);
        }
#endif
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Check bus element is responding
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool BusI2C::isElemResponding(uint32_t address, bool* pIsValid)
{
#ifdef DEBUG_NO_SCANNING
    return true;
#endif
    if (pIsValid)
        *pIsValid = false;
    if (address > BUS_SCAN_I2C_ADDRESS_MAX)
        return false;
        
    // Obtain semaphore controlling access to busElemChange list and flag
    bool isOnline = false;
    if (xSemaphoreTake(_busElemStatusMutex, pdMS_TO_TICKS(1)) == pdTRUE)
    {    
        isOnline = _i2cAddrResponseStatus[address].isOnline;
        if (pIsValid)
            *pIsValid = _i2cAddrResponseStatus[address].isValid;

        // Return semaphore
        xSemaphoreGive(_busElemStatusMutex);        
    }
    return isOnline;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Request bus scan
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void BusI2C::requestScan(bool enableSlowScan, bool requestFastScan)
{
    // Perform fast scans to detect online/offline status
    if (requestFastScan)
        _fastScanPendingCount = I2C_ADDR_RESP_COUNT_FAIL_MAX;
    _slowScanEnabled = enableSlowScan;
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
