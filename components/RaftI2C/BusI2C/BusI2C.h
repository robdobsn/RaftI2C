/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// I2C Bus Handler
//
// Rob Dobson 2020
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "BusBase.h"
#include "RaftI2CCentralIF.h"
#include "BusRequestResult.h"
#include "BusI2CRequestRec.h"
#include "BusI2CScheduler.h"
#include "ThreadSafeQueue.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

class RaftI2CCentralIF;

class BusI2C : public BusBase
{
public:
    // Constructor
    BusI2C(BusElemStatusCB busElemStatusCB, BusOperationStatusCB busOperationStatusCB,
                RaftI2CCentralIF* pI2CInterface = nullptr);
    virtual ~BusI2C();

    // Setup
    virtual bool setup(const RaftJsonIF& config) override final;

    // Close bus
    virtual void close() override final;

    // Service
    virtual void service() override final;

    // Clear
    virtual void clear(bool incPolling) override final;

    // Pause
    virtual void pause(bool pause) override final
    {
        // Set pause flag - read in the worker
        _pauseRequested = pause;

        // Suspend all polling
        for (PollingVectorItem& pollItem : _pollingVector)
            pollItem.suspendCount = MAX_CONSEC_FAIL_POLLS_BEFORE_SUSPEND;
    }

    // IsPaused
    virtual bool isPaused() override final
    {
        return _isPaused;
    }

    // Hiatus for period of ms
    virtual void hiatus(uint32_t forPeriodMs) override final;

    // IsHiatus
    virtual bool isHiatus() override final
    {
        return _hiatusActive;
    }

    // Get bus name
    virtual String getBusName() const override final
    {
        return _busName;
    }

    // isOperatingOk
    virtual BusOperationStatus isOperatingOk() const override final
    {
        return _busOperationStatus;
    }

    // Request bus action
    virtual bool addRequest(BusRequestInfo& busReqInfo) override final;

    // Check bus element responding
    virtual bool isElemResponding(uint32_t address, bool* pIsValid) override final;

    // Request (or suspend) slow scanning and optionally request a fast scan
    virtual void requestScan(bool enableSlowScan, bool requestFastScan) override final;

    // Creator fn
    static BusBase* createFn(BusElemStatusCB busElemStatusCB, BusOperationStatusCB busOperationStatusCB)
    {
        return new BusI2C(busElemStatusCB, busOperationStatusCB);
    }

private:
    // Settings
    int _i2cPort = 0;
    int _sdaPin = -1;
    int _sclPin = -1;
    uint32_t _freq = 100000;
    uint32_t _i2cFilter = RaftI2CCentralIF::DEFAULT_BUS_FILTER_LEVEL;
    String _busName;

    // I2C device
    RaftI2CCentralIF* _pI2CDevice = nullptr;
    bool _i2cDeviceNeedsDeleting = false;

    // Low-load bus indicates the bus should use minimal resources
    bool _lowLoadBus = false;

    // Address to use for lockup-detection - this should be the address of a device
    // that is permanently connected to the bus
    uint8_t _addrForLockupDetect = 0;
    bool _addrForLockupDetectValid = false;

    // Scan boost - used to increase the rate of scanning on these addresses
    uint16_t _scanBoostCount = 0;
    std::vector<uint8_t> _scanBoostAddresses;
    static const uint32_t SCAN_BOOST_FACTOR = 10;
    uint8_t _scanBoostCurAddrIdx = 0;

    // Last comms time uS
    uint64_t _lastI2CCommsUs = 0;
    static const uint32_t MIN_TIME_BETWEEN_I2C_COMMS_US = 1000;

    // Init ok
    bool _initOk = false;

    // Scheduling helper
    BusI2CScheduler _scheduler;

    // Task that operates the bus
    volatile TaskHandle_t _i2cWorkerTaskHandle = nullptr;
    static const int DEFAULT_TASK_CORE = 0;
    static const int DEFAULT_TASK_PRIORITY = 1;
    static const int DEFAULT_TASK_STACK_SIZE_BYTES = 10000;
    static const uint32_t WAIT_FOR_TASK_EXIT_MS = 1000;

    // Pause/run status
    volatile bool _pauseRequested = false;
    volatile bool _isPaused = false;

    // Haitus for period of ms (generally due to power cycling, etc)
    volatile bool _hiatusActive = false;
    uint32_t _hiatusStartMs = 0;
    uint32_t _hiatusForMs = 0;

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

    // Scanning
    static const uint32_t BUS_SCAN_PERIOD_MS = 10;
    static const uint32_t BUS_SCAN_I2C_ADDRESS_MIN = 4;
    static const uint32_t BUS_SCAN_I2C_ADDRESS_MAX = 0x77;
    uint32_t _busScanCurAddr = BUS_SCAN_I2C_ADDRESS_MIN;
    uint32_t _busScanLastMs = 0;

    // Enable slow scanning initially
    bool _slowScanEnabled = true;

    // Set bus scanning so enough fast scans are done initially to
    // detect online/offline status of all bus elements
    uint32_t _fastScanPendingCount = I2C_ADDR_RESP_COUNT_FAIL_MAX;

    // I2C address response status
    class I2CAddrRespStatus
    {
    public:
        I2CAddrRespStatus()
        {
            clear();
        }
        void clear()
        {
            count = 0;
            isChange = false;
            isOnline = false;
            isValid = false;
        }
        uint8_t count : 5;
        bool isChange : 1;
        bool isOnline : 1;
        bool isValid : 1;
    };

    // I2C address response status
    I2CAddrRespStatus _i2cAddrResponseStatus[BUS_SCAN_I2C_ADDRESS_MAX+1];
    static const uint32_t I2C_ADDR_RESP_COUNT_FAIL_MAX = 3;
    static const uint32_t I2C_ADDR_RESP_COUNT_OK_MAX = 2;

    // Bus element status change detection
    SemaphoreHandle_t _busElemStatusMutex = nullptr;
    bool _busElemStatusChangeDetected = false;

    // Bus operation status
    BusOperationStatus _busOperationStatus = BUS_OPERATION_UNKNOWN;

    // Access barring time
    static const uint32_t ELEM_BAR_I2C_ADDRESS_MAX = 127;
    uint32_t _busAccessBarMs[ELEM_BAR_I2C_ADDRESS_MAX+1];

    // Debug
    uint32_t _debugLastBusLoopMs = 0;
    uint32_t _debugLastPollTimeMs = 0;

    // Worker task (static version calls the other)
    static void i2cWorkerTaskStatic(void* pvParameters);
    void i2cWorkerTask();

    // Helpers
    bool addToPollingList(BusRequestInfo& busReqInfo);
    bool addToQueuedReqFIFO(BusRequestInfo& busReqInfo);
    RaftI2CCentralIF::AccessResultCode i2cSendHelper(BusI2CRequestRec* pReqRec, uint32_t pollListIdx);
    void scanNextAddress(bool isFastScan);
    void handleBusElemStateChanges(uint32_t address, bool elemResponding);
};
