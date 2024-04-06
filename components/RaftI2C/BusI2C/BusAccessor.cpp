/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// I2C Bus Accessor
//
// Rob Dobson 2020-2024
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "BusAccessor.h"
#include "Logger.h"
#include "RaftUtils.h"

// Warnings
#define WARN_ON_REQUEST_BUFFER_FULL
#define WARN_ON_RESPONSE_BUFFER_FULL

// Debug
// #define DEBUG_BUS_I2C_POLLING
// #define DEBUG_SERVICE_RESPONSE_CALLBACK
// #define DEBUG_REQ_QUEUE_COMMANDS
// #define DEBUG_REQ_QUEUE_ONE_ADDR 0x1d
// #define DEBUG_POLL_TIME_FOR_ADDR 0x1d
// #define DEBUG_I2C_LENGTH_MISMATCH_WITH_BUTTON_A_PIN
// #define DEBUG_ADD_TO_QUEUED_REC_FIFO

static const char* MODULE_PREFIX = "BusAccessor";

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Constructor and destructor
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

BusAccessor::BusAccessor(BusBase& busBase, BusI2CReqAsyncFn busI2CReqAsyncFn) :
        _busBase(busBase),
        _busI2CReqAsyncFn(busI2CReqAsyncFn)
{
    // Create the mutex for the polling list
    _pollingMutex = xSemaphoreCreateMutex();
}

BusAccessor::~BusAccessor()
{
    // Remove mutex
    if (_pollingMutex)
        vSemaphoreDelete(_pollingMutex);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Setup
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void BusAccessor::setup(const RaftJsonIF& config)
{
    // Setup
    _lowLoadBus = config.getLong("lowLoad", 0) != 0;

    // Obtain semaphore to polling vector
    if (xSemaphoreTake(_pollingMutex, pdMS_TO_TICKS(10)) == pdTRUE)
    {
        _scheduler.clear();
        xSemaphoreGive(_pollingMutex);
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Service
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void BusAccessor::service()
{
    // Stats
    _busBase.getBusStats().respQueueCount(_responseQueue.count());
    _busBase.getBusStats().reqQueueCount(_requestQueue.count());

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
#ifdef DEBUG_SERVICE_RESPONSE_CALLBACK
            LOG_I(MODULE_PREFIX, "service response retval %d addr 0x%02x readLen %d", 
                    reqResult.isResultOk(), reqResult.getAddress(), reqResult.getReadDataLen());
#endif
        }
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Pause
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void BusAccessor::pause(bool pause)
{
    // Suspend all polling (or unsuspend)
    for (PollingVectorItem& pollItem : _pollingVector)
        pollItem.suspendCount = pause ? MAX_CONSEC_FAIL_POLLS_BEFORE_SUSPEND : 0;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Clear
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void BusAccessor::clear(bool incPolling)
{
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
// Process request queue
// Called from thread function in BusI2C
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void BusAccessor::processRequestQueue(bool isPaused)
{
    // Get from request FIFO
    BusI2CRequestRec reqRec;
    if (_requestQueue.get(reqRec))
    {
        // Debug
#ifdef DEBUG_REQ_QUEUE_COMMANDS
        String writeDataStr;
        Raft::getHexStrFromBytes(reqRec.getWriteData(), reqRec.getWriteDataLen(), writeDataStr);
        LOG_I(MODULE_PREFIX, "i2cWorkerTask reqQ got addr@slot+1 %s write %s", reqRec.getAddrAndSlot().toString().c_str(), writeDataStr.c_str());
#endif

        // Debug one address only
#ifdef DEBUG_REQ_QUEUE_ONE_ADDR
        if (reqRec.getAddrAndSlot().addr != DEBUG_REQ_QUEUE_ONE_ADDR)
            return;
#endif

        // Check if paused
        if (isPaused)
        {
            // Check is firmware update
            if (reqRec.isFWUpdate() || reqRec.shouldSendIfPaused())
            {
                // Make the request
                _busI2CReqAsyncFn(&reqRec, 0);
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
            _busI2CReqAsyncFn(&reqRec, 0);
        }
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Process polling
// Called from thread function in BusI2C
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void BusAccessor::processPolling()
{
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
                if (pReqRec->isPolling() && (pReqRec->getAddrAndSlot().addr == DEBUG_POLL_TIME_FOR_ADDR))
                {
                    LOG_I(MODULE_PREFIX, "i2cWorker polling addr@slot+1 %s elapsed %ld", 
                                pReqRec->getAddrAndSlot().toString().c_str(), 
                                Raft::timeElapsed(millis(), _debugLastPollTimeMs));
                    _debugLastPollTimeMs = millis();
                }
#endif
                // Send poll request
                RaftI2CCentralIF::AccessResultCode sendResult = _busI2CReqAsyncFn(pReqRec, pollListIdx);
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

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Handle response to I2C request
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void BusAccessor::handleResponse(const BusI2CRequestRec* pReqRec, RaftI2CCentralIF::AccessResultCode sendResult,
                uint8_t* pReadBuf, uint32_t numBytesRead)
{
    // Check if a response was expected but read length doesn't match
    if (pReqRec->getReadReqLen() != numBytesRead)
    {
        // Stats
        _busBase.getBusStats().respLengthError();

#ifdef DEBUG_I2C_LENGTH_MISMATCH_WITH_BUTTON_A_PIN
        digitalWrite(5, 1);
        pinMode(5, OUTPUT);
        delayMicroseconds(10);
        digitalWrite(5, 0);
        delayMicroseconds(10);
        digitalWrite(5, 1);
#endif
        return;
    }

    // Create response
    BusRequestResult reqResult(pReqRec->getAddrAndSlot().toCompositeAddrAndSlot(), 
                    pReqRec->getCmdId(), 
                    pReadBuf, 
                    numBytesRead, 
                    sendResult == RaftI2CCentralIF::ACCESS_RESULT_OK,
                    pReqRec->getCallback(), 
                    pReqRec->getCallbackParam());

    // Check polling
    if (pReqRec->isPolling())
    {
        // Poll complete stats
        _busBase.getBusStats().pollComplete();

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
            _busBase.getBusStats().cmdComplete();
        }
        else
        {
            // Warn
#ifdef WARN_ON_RESPONSE_BUFFER_FULL
            if (Raft::isTimeout(millis(), _respBufferFullLastWarnMs, BETWEEN_BUF_FULL_WARNINGS_MIN_MS))
            {
                int msgsWaiting = _responseQueue.count();
                LOG_W(MODULE_PREFIX, "sendHelper %s resp buffer full - waiting %d",
                        _busBase.getBusName().c_str(), msgsWaiting
                    );
                _respBufferFullLastWarnMs = millis();
            }
#endif

            // Stats
            _busBase.getBusStats().respBufferFull();
        }
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Add a queued request
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool BusAccessor::addRequest(BusRequestInfo& busReqInfo)
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

bool BusAccessor::addToPollingList(BusRequestInfo& busReqInfo)
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

bool BusAccessor::addToQueuedReqFIFO(BusRequestInfo& busReqInfo)
{
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
    LOG_I(MODULE_PREFIX, "addToQueuedRecFIFO addr@slot+1 %s writeData %s readLen %d delayMs %d", 
                reqRec.getAddrAndSlot().toString().c_str(), writeDataStr.c_str(), reqRec.getReadReqLen(), reqRec.getBarAccessForMsAfterSend());
#endif

    // Msg buffer full
    if (retc != pdTRUE)
    {
        _busBase.getBusStats().reqBufferFull();

#ifdef WARN_ON_REQUEST_BUFFER_FULL
        if (Raft::isTimeout(millis(), _reqBufferFullLastWarnMs, BETWEEN_BUF_FULL_WARNINGS_MIN_MS))
        {
            int msgsWaiting = _requestQueue.count();
            LOG_W(MODULE_PREFIX, "addToQueuedReqFIFO %s req buffer full - waiting %d", 
                    _busBase.getBusName().c_str(), msgsWaiting
                );
            _reqBufferFullLastWarnMs = millis();
        }
#endif
    }

    return retc;
}
