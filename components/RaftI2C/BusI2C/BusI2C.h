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
    static BusBase* createFn(BusElemStatusCB busElemStatusCB, BusOperationStatusCB busOperationStatusCB)
    {
        return new BusI2C(busElemStatusCB, busOperationStatusCB);
    }

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief setup
    /// @param config - configuration
    /// @return true if setup was successful
    virtual bool setup(const RaftJsonIF& config) override final;

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Close bus
    /// @return true if close was successful
    virtual void close() override final;

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief service (should be called frequently to service the bus)
    virtual void service() override final;

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
    virtual bool isPaused() override final
    {
        return _isPaused;
    }

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Hiatus for period in ms (a hiatus is a suspension of activity for a period of time - generally due to power cycling, etc)
    /// @param forPeriodMs - period in ms
    virtual void hiatus(uint32_t forPeriodMs) override final;

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief isHiatus
    /// @return true if the bus is in hiatus
    virtual bool isHiatus() override final
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
    /// @brief Add a request to the bus (may be a one-off or a poll request that is repeated at intervals)
    /// @param busReqInfo - bus request information
    /// @return true if the request was added
    virtual bool addRequest(BusRequestInfo& busReqInfo) override final
    {
        return _busAccessor.addRequest(busReqInfo);
    }

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Check if an element is responding
    /// @param address - address of element
    /// @param pIsValid - (out) true if the address is valid
    /// @return true if the element is responding
    virtual bool isElemResponding(uint32_t address, bool* pIsValid) override final;

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Request (or suspend) slow scanning and optionally request a fast scan
    /// @param enableSlowScan - true to enable slow scan, false to disable
    /// @param requestFastScan - true to request a fast scan
    virtual void requestScan(bool enableSlowScan, bool requestFastScan) override final;

    // TODO - make these override base-class methods

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get device type information by address
    /// @param address - address of device to get information for
    /// @param includePlugAndPlayInfo - true to include plug and play information
    /// @return JSON string
    String getDevTypeInfoJsonByAddr(uint32_t address, bool includePlugAndPlayInfo) const;

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get device type information by device type name
    /// @param deviceType - device type name
    /// @param includePlugAndPlayInfo - true to include plug and play information
    /// @return JSON string
    String getDevTypeInfoJsonByTypeName(const String& deviceType, bool includePlugAndPlayInfo) const;

    ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////    
    /// @brief Get bus element status for a specific address
    /// @param address - address of device to get responses for
    /// @param isOnline - (out) true if device is online
    /// @param deviceTypeIndex - (out) device type index
    /// @param devicePollResponseData - (out) vector to store the device poll response data
    /// @param responseSize - (out) size of the response data
    /// @param maxResponsesToReturn - maximum number of responses to return (0 for no limit)
    /// @return number of responses returned
    uint32_t getBusElemStatus(uint32_t address, bool& isOnline, uint16_t& deviceTypeIndex, 
                std::vector<uint8_t>& devicePollResponseData, 
                uint32_t& responseSize, uint32_t maxResponsesToReturn)
    {
        return _busStatusMgr.getBusElemStatus(address, isOnline, deviceTypeIndex, devicePollResponseData, responseSize, maxResponsesToReturn);
    }

    ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get time of last bus status update
    /// @return time of last bus status update in ms
    uint32_t getLastStatusUpdateMs(bool includeElemOnlineStatusChanges, bool includePollDataUpdates)
    {
        return _busStatusMgr.getLastStatusUpdateMs(includeElemOnlineStatusChanges, includePollDataUpdates);
    }

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get bus status JSON for all detected bus elements
    /// @return JSON string
    String getBusStatusJson()
    {
        return _busStatusMgr.getBusStatusJson(_deviceIdentMgr);
    }

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    // Get the bus element address as a string
    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////

    virtual String busElemAddrAndStatusToString(BusElemAddrAndStatus busElemAddr) override
    {
        BusI2CAddrAndSlot addrAndSlot = BusI2CAddrAndSlot::fromCompositeAddrAndSlot(busElemAddr.address);
        return addrAndSlot.toString() + ":" +
                        (busElemAddr.isChangeToOnline ? "Online" : "Offline" + String(busElemAddr.isChangeToOffline ? " (was online)" : ""));
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
