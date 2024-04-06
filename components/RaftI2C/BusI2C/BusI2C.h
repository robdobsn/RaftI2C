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
#include "BusI2CRequestRec.h"
#include "BusScanner.h"
#include "BusStatusMgr.h"
#include "BusExtenderMgr.h"
#include "BusAccessor.h"
#include "DeviceIdentMgr.h"
#include "DevicePollingMgr.h"

class RaftI2CCentralIF;

class BusI2C : public BusBase
{
public:
    // Constructor
    BusI2C(BusElemStatusCB busElemStatusCB, BusOperationStatusCB busOperationStatusCB,
                RaftI2CCentralIF* pI2CCentralIF = nullptr);
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

        // Suspend bus accessor
        _busAccessor.pause(pause);
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
        return _busStatusMgr.isOperatingOk();
    }

    // Request bus action
    virtual bool addRequest(BusRequestInfo& busReqInfo) override final
    {
        return _busAccessor.addRequest(busReqInfo);
    }

    // Check bus element responding
    virtual bool isElemResponding(uint32_t address, bool* pIsValid) override final;

    // Request (or suspend) slow scanning and optionally request a fast scan
    virtual void requestScan(bool enableSlowScan, bool requestFastScan) override final;

    // Creator fn
    static BusBase* createFn(BusElemStatusCB busElemStatusCB, BusOperationStatusCB busOperationStatusCB)
    {
        return new BusI2C(busElemStatusCB, busOperationStatusCB);
    }

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    // Get the bus element address as a string
    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////

    virtual String busElemAddrAndStatusToString(BusElemAddrAndStatus busElemAddr) override
    {
        RaftI2CAddrAndSlot addrAndSlot = RaftI2CAddrAndSlot::fromCompositeAddrAndSlot(busElemAddr.address);
        return addrAndSlot.toString() + "=" + (busElemAddr.isChangeToOnline ? "Online" : "Offline");
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
    RaftI2CCentralIF* _pI2CCentral = nullptr;
    bool _i2cCentralNeedsToBeDeleted = false;

    // Last comms time uS
    uint64_t _lastI2CCommsUs = 0;
    static const uint32_t MIN_TIME_BETWEEN_I2C_COMMS_US = 1000;

    // Init ok
    bool _initOk = false;

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
    
    // Bus status
    BusStatusMgr _busStatusMgr;

    // Bus extender manager
    BusExtenderMgr _busExtenderMgr;

    // Device identifier
    DeviceIdentMgr _deviceIdentMgr;

    // Bus scanner
    BusScanner _busScanner;

    // Device polling manager
    DevicePollingMgr _devicePollingMgr;

    // Bus accessor
    BusAccessor _busAccessor;

    // Access barring time
    static const uint32_t ELEM_BAR_I2C_ADDRESS_MAX = 127;
    uint32_t _busAccessBarMs[ELEM_BAR_I2C_ADDRESS_MAX+1];

    // Debug
    uint32_t _debugLastBusLoopMs = 0;

    // Worker task (static version calls the other)
    static void i2cWorkerTaskStatic(void* pvParameters);
    void i2cWorkerTask();

    // Helpers
    RaftI2CCentralIF::AccessResultCode i2cSendAsync(const BusI2CRequestRec* pReqRec, uint32_t pollListIdx);
    RaftI2CCentralIF::AccessResultCode i2cSendSync(const BusI2CRequestRec* pReqRec, std::vector<uint8_t>* pReadData);
};
