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
#include "RaftJsonIF.h"
#include "BusI2CAddrAndSlot.h"

// Warnings
#define WARN_ON_REQUEST_BUFFER_FULL
#define WARN_ON_RESPONSE_BUFFER_FULL

// Debug
// #define DEBUG_BUS_I2C_POLLING
// #define DEBUG_SERVICE_RESPONSE_CALLBACK
// #define DEBUG_REQ_QUEUE_COMMANDS
// #define DEBUG_REQ_QUEUE_ONE_ADDR 0x1d
// #define DEBUG_POLL_TIME_FOR_ADDR 0x1d
// #define DEBUG_I2C_LENGTH_MISMATCH_WITH_BUTTON_GPIO_NUM 5
// #define DEBUG_ADD_TO_QUEUED_REC_FIFO

/// @brief Constructor
/// @param raftBus RaftBus instance
/// @param busI2CReqAsyncFn Function to call to send I2C request async
BusAccessor::BusAccessor(RaftBus& raftBus, BusReqAsyncFn busI2CReqAsyncFn) :
        _raftBus(raftBus),
        _busI2CReqAsyncFn(busI2CReqAsyncFn)
{
    // Create the mutex for the polling list
    RaftMutex_init(_pollingMutex);
}

/// @brief Destructor
BusAccessor::~BusAccessor()
{
    // Remove mutex
    RaftMutex_destroy(_pollingMutex);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Setup
/// @param config Configuration
void BusAccessor::setup(const RaftJsonIF& config)
{
    // Setup
    _lowLoadBus = config.getLong("lowLoad", 0) != 0;

    // Obtain semaphore to polling vector
    if (RaftMutex_lock(_pollingMutex, 50))
    {
        _scheduler.clear();
        RaftMutex_unlock(_pollingMutex);
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Loop function
void BusAccessor::loop()
{
    // Stats
    _raftBus.getBusStats().respQueueCount(_responseQueue.count());
    _raftBus.getBusStats().reqQueueCount(_requestQueue.count());

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
            LOG_I(MODULE_PREFIX, "loop response retval %d addr 0x%02x readLen %d", 
                    reqResult.isResultOk(), reqResult.getAddress(), reqResult.getReadDataLen());
#endif
        }
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Pause or resume bus operation
void BusAccessor::pause(bool pause)
{
    // Suspend all polling (or unsuspend)
    for (PollingVectorItem& pollItem : _pollingVector)
        pollItem.suspendCount = pause ? MAX_CONSEC_FAIL_POLLS_BEFORE_SUSPEND : 0;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Clear queues
/// @param incPolling Clear polling list if true
void BusAccessor::clear(bool incPolling)
{
    // Clear the action queue
    _responseQueue.clear();

    // Clear polling list if required
    if (incPolling)
    {
        // We're going to mess with the polling list so obtain the semaphore
        if (RaftMutex_lock(_pollingMutex, 10))
        {
            // Clear all lists
            _scheduler.clear();
            _pollingVector.clear();

            // Return semaphore
            RaftMutex_unlock(_pollingMutex);
        }
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Process request queue (called from thread function)
/// @param isPaused True if bus is paused
void BusAccessor::processRequestQueue(bool isPaused)
{
    // Get from request FIFO
    BusRequestInfo reqRec;
    if (_requestQueue.get(reqRec))
    {
#ifdef DEBUG_REQ_QUEUE_COMMANDS
        // Address and slot
        BusElemAddrType address = reqRec.getAddress();
        // Debug
        String writeDataStr;
        Raft::getHexStrFromBytes(reqRec.getWriteData(), reqRec.getWriteDataLen(), writeDataStr);
        LOG_I(MODULE_PREFIX, "i2cWorkerTask reqQ got addr@slotNum %s write %s", 
                    BusI2CAddrAndSlot::toString(address).c_str(), writeDataStr.c_str());
#endif

        // Debug one address only
#ifdef DEBUG_REQ_QUEUE_ONE_ADDR
        if (BusI2CAddrAndSlot::getI2CAddr(address) != DEBUG_REQ_QUEUE_ONE_ADDR)
            return;
#endif

        // Check if paused
        if (isPaused)
        {
            // Check is firmware update
            if (reqRec.isFWUpdate() || (reqRec.getBusReqType() == BUS_REQ_TYPE_SEND_IF_PAUSED))
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
/// @brief Process polling (called from thread function)
void BusAccessor::processPolling()
{
    // Obtain semaphore to polling vector
    if (RaftMutex_lock(_pollingMutex, 10))
    {
        // Get the next element to poll
        int pollListIdx = _scheduler.getNext();
        if (pollListIdx >= 0)
        {
            // Check valid - if list is empty or has shrunk this test can fail
            // Polling can be suspended if too many failures occur
            BusRequestInfo* pReqRec = NULL;
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
                BusElemAddrType address = pReqRec->getAddress();
                if (pReqRec->isPolling() && (BusI2CAddrAndSlot::getI2CAddr(address) == DEBUG_POLL_TIME_FOR_ADDR))
                {
                    LOG_I(MODULE_PREFIX, "i2cWorker polling addr@slotNum %s elapsed %ld", 
                                BusI2CAddrAndSlot::toString(address).c_str(), 
                                Raft::timeElapsed(millis(), _debugLastPollTimeMs));
                    _debugLastPollTimeMs = millis();
                }
#endif
                // Send poll request
                RaftRetCode sendResult = _busI2CReqAsyncFn(pReqRec, pollListIdx);
                // Check for failed send and not barred temporarily
                if ((sendResult != RAFT_OK) && (sendResult != RAFT_BUS_BARRED))
                {
                    // Increment the suspend count if required
                    if (pollListIdx < _pollingVector.size())
                        if (_pollingVector[pollListIdx].suspendCount < MAX_CONSEC_FAIL_POLLS_BEFORE_SUSPEND)
                            _pollingVector[pollListIdx].suspendCount++;
                }
            }
        }

        // Free the semaphore
        RaftMutex_unlock(_pollingMutex);
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///@brief Handle response to I2C request
///@param pReqRec Request record
///@param sendResult Result of the send
///@param pReadBuf Read buffer
///@param numBytesRead Number of bytes read
void BusAccessor::handleResponse(const BusRequestInfo* pReqRec, RaftRetCode sendResult,
                uint8_t* pReadBuf, uint32_t numBytesRead)
{
    // Check if a response was expected but read length doesn't match
    if (pReqRec->getReadReqLen() != numBytesRead)
    {
        // Stats
        _raftBus.getBusStats().respLengthError();

#ifdef DEBUG_I2C_LENGTH_MISMATCH_WITH_BUTTON_A_PIN
        digitalWrite(DEBUG_I2C_LENGTH_MISMATCH_WITH_BUTTON_GPIO_NUM, 1);
        pinMode(DEBUG_I2C_LENGTH_MISMATCH_WITH_BUTTON_GPIO_NUM, OUTPUT);
        delayMicroseconds(10);
        digitalWrite(DEBUG_I2C_LENGTH_MISMATCH_WITH_BUTTON_GPIO_NUM, 0);
        delayMicroseconds(10);
        digitalWrite(DEBUG_I2C_LENGTH_MISMATCH_WITH_BUTTON_GPIO_NUM, 1);
#endif
        return;
    }

    // Create response
    BusRequestResult reqResult(pReqRec->getAddress(), 
                    pReqRec->getCmdId(), 
                    pReadBuf, 
                    numBytesRead, 
                    sendResult == RAFT_OK,
                    pReqRec->getCallback(), 
                    pReqRec->getCallbackParam());

    // Check polling
    if (pReqRec->isPolling())
    {
        // Poll complete stats
        _raftBus.getBusStats().pollComplete();

        // Check if callback is required
        BusRequestCallbackType callback = reqResult.getCallback();
        if (callback)
        {
            callback(reqResult.getCallbackParam(), reqResult);
            // LOG_D(MODULE_PREFIX, "loop retval %d", reqResult.isResultOk());
        }
    }
    else
    {
        // Add to the response queue
        if (_responseQueue.put(reqResult, ADD_RESP_TO_QUEUE_MAX_MS))
        {
            _raftBus.getBusStats().cmdComplete();
        }
        else
        {
            // Warn
#ifdef WARN_ON_RESPONSE_BUFFER_FULL
            if (Raft::isTimeout(millis(), _respBufferFullLastWarnMs, BETWEEN_BUF_FULL_WARNINGS_MIN_MS))
            {
                int msgsWaiting = _responseQueue.count();
                LOG_W(MODULE_PREFIX, "sendHelper %s resp buffer full - waiting %d",
                        _raftBus.getBusName().c_str(), msgsWaiting
                    );
                _respBufferFullLastWarnMs = millis();
            }
#endif

            // Stats
            _raftBus.getBusStats().respBufferFull();
        }
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Add a queued request (to be processed asynchronously)
/// @param busReqInfo Request record
/// @return True if added
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
/// @brief Add to the polling list
/// @param busReqInfo Request record
/// @return True if added
bool BusAccessor::addToPollingList(BusRequestInfo& busReqInfo)
{
#ifdef DEBUG_BUS_I2C_POLLING
    LOG_I(MODULE_PREFIX, "addToPollingList elemAddr %x freqHz %.1f", busReqInfo.getAddress(), busReqInfo.getPollFreqHz());
#endif

    // We're going to mess with the polling list so obtain the semaphore
    if (RaftMutex_lock(_pollingMutex, 50))
    {
        // See if already in the list
        bool addedOk = false;
        for (PollingVectorItem& pollItem : _pollingVector)
        {
            if (pollItem.pollReq.getAddress() == busReqInfo.getAddress())
            {
                // Replace request record
                addedOk = true;
                pollItem.pollReq = BusRequestInfo(busReqInfo);
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
                newPollingItem.pollReq = busReqInfo;
                
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
        RaftMutex_unlock(_pollingMutex);
        return true;
    }
    return false;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Add to the queued request FIFO
/// @param reqRec Request record
/// @return True if added
bool BusAccessor::addToQueuedReqFIFO(BusRequestInfo& reqRec)
{
    // Send to the request FIFO
    bool putOk = _requestQueue.put(reqRec, ADD_REQ_TO_QUEUE_MAX_MS);

#ifdef DEBUG_ADD_TO_QUEUED_REC_FIFO
    // Debug
    String writeDataStr;
    Raft::getHexStrFromBytes(reqRec.getWriteData(), reqRec.getWriteDataLen(), writeDataStr);
    LOG_I(MODULE_PREFIX, "addToQueuedRecFIFO addr@slotNum %s writeData %s readLen %d delayMs %d", 
                BusI2CAddrAndSlot::fromBusElemAddrType(reqRec.getAddress()).toString().c_str(),
                writeDataStr.c_str(), reqRec.getReadReqLen(), reqRec.getBarAccessForMsAfterSend());
#endif

    // Msg buffer full
    if (!putOk)
    {
        _raftBus.getBusStats().reqBufferFull();

#ifdef WARN_ON_REQUEST_BUFFER_FULL
        if (Raft::isTimeout(millis(), _reqBufferFullLastWarnMs, BETWEEN_BUF_FULL_WARNINGS_MIN_MS))
        {
            int msgsWaiting = _requestQueue.count();
            LOG_W(MODULE_PREFIX, "addToQueuedReqFIFO %s req buffer full - waiting %d", 
                    _raftBus.getBusName().c_str(), msgsWaiting
                );
            _reqBufferFullLastWarnMs = millis();
        }
#endif
    }

    return putOk;
}
