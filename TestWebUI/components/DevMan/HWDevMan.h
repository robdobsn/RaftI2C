/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Hardware Device Manager
//
// Rob Dobson 2024
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "RaftUtils.h"
#include "RaftJsonNVS.h"
#include "RaftSysMod.h"
#include "BusBase.h"
#include "BusManager.h"
#include "BusRequestResult.h"

class APISourceInfo;

class HWDevMan : public RaftSysMod
{
public:
    // Constructor and destructor
    HWDevMan(const char *pModuleName, RaftJsonIF& sysConfig);
    virtual ~HWDevMan();

    // Create function (for use by SysManager factory)
    static RaftSysMod* create(const char* pModuleName, RaftJsonIF& sysConfig)
    {
        return new HWDevMan(pModuleName, sysConfig);
    }

protected:

    // Setup
    virtual void setup() override final;

    // Loop (called frequently)
    virtual void loop() override final;

    // Add endpoints
    virtual void addRestAPIEndpoints(RestAPIEndpointManager& pEndpoints) override final;

    // Status
    virtual String getStatusJSON() const override final;
    
private:

    // Initialised flag
    bool _isInitialised = false;

    // Mutable data saving
    static const uint32_t MUTABLE_DATA_SAVE_MIN_MS = 5000;
    uint32_t _mutableDataChangeLastMs = 0;
    bool _mutableDataDirty = false;

    // Bus manager
    BusManager _busManager;

    // Helper functions
    void deinit();
    RaftRetCode apiDevMan(const String &reqStr, String &respStr, const APISourceInfo& sourceInfo);
    void saveMutableData();
    void debugShowCurrentState();
    void getStatusHash(std::vector<uint8_t>& stateHash);
    void cmdResultReportCallback(BusRequestResult& reqResult);
 
    // Bus operation and status functions
    void busElemStatusCB(BusBase& bus, const std::vector<BusElemAddrAndStatus>& statusChanges);
    void busOperationStatusCB(BusBase& bus, BusOperationStatus busOperationStatus);

    // Pulse count
    RaftJsonNVS _devicesNVConfig;
};
