/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// I2C Bus Accessor
//
// Rob Dobson 2020-2024
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "BusBase.h"
#include "BusI2CScheduler.h"
#include "ThreadSafeQueue.h"
#include "BusRequestResult.h"
#include "BusI2CRequestRec.h"
#include "RaftI2CCentralIF.h"

class BusAccessor {
public:
    // Constructor and destructor
    BusAccessor(BusBase& busBase, BusI2CRequestFn busI2CRequestFn);
    ~BusAccessor();

    // Setup and service
    void setup(const RaftJsonIF& config);
    void service();

    // Pause and clear
    void pause(bool pause);
    void clear(bool incPolling);

    // Requests and responses
    bool addRequest(BusRequestInfo& busReqInfo);
    void processRequestQueue(bool isPaused);
    void handleResponse(BusI2CRequestRec* pReqRec, RaftI2CCentralIF::AccessResultCode sendResult,
                uint8_t* pReadBuf, uint32_t numBytesRead);

    // Polling
    void processPolling();


private:
    // Bus base
    BusBase& _busBase;

    // Polling vector item
    class PollingVectorItem
    {
    public:
        PollingVectorItem()
        {
            clear();
        }
        void clear()
        {
            suspendCount = 0;
            pollReq.clear();
        }
        uint8_t suspendCount;
        BusI2CRequestRec pollReq;
    };

    // Polling vector and mutex controlling access
    std::vector<PollingVectorItem> _pollingVector;
    SemaphoreHandle_t _pollingMutex = nullptr;
    static const int MAX_POLLING_LIST_RECS = 30;
    static const int MAX_POLLING_LIST_RECS_LOW_LOAD = 4;
    static const int MAX_CONSEC_FAIL_POLLS_BEFORE_SUSPEND = 2;

    // Polling and queued requests
    static const int REQUEST_FIFO_SLOTS = 40;
    static const int REQUEST_FIFO_SLOTS_LOW_LOAD = 3;
    static const uint32_t ADD_REQ_TO_QUEUE_MAX_MS = 2;
    ThreadSafeQueue<BusI2CRequestRec> _requestQueue;

    // Response FIFO
    static const int RESPONSE_FIFO_SLOTS = 40;
    static const int RESPONSE_FIFO_SLOTS_LOW_LOAD = 3;
    static const uint32_t ADD_RESP_TO_QUEUE_MAX_MS = 2;
    ThreadSafeQueue<BusRequestResult> _responseQueue;

    // Buffer full warning last time
    static const uint32_t BETWEEN_BUF_FULL_WARNINGS_MIN_MS = 5000;
    uint32_t _respBufferFullLastWarnMs = 0;
    uint32_t _reqBufferFullLastWarnMs = 0;

    // Scheduling helper
    BusI2CScheduler _scheduler;

    // Bus i2c request function
    BusI2CRequestFn _busI2CRequestFn = nullptr;

    // Low-load bus indicates the bus should use minimal resources
    bool _lowLoadBus = false;

    // Helpers
    bool addToPollingList(BusRequestInfo& busReqInfo);
    bool addToQueuedReqFIFO(BusRequestInfo& busReqInfo);
};
