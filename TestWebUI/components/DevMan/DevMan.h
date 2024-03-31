/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Device Manager
//
// Rob Dobson 2024
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "RaftUtils.h"
#include "RaftJsonNVS.h"
#include "RaftSysMod.h"

class APISourceInfo;

class DevMan : public RaftSysMod
{
public:
    static const int DEFAULT_MAX_ELEMS = 24;
    DevMan(const char *pModuleName, RaftJsonIF& sysConfig);

    // Create function (for use by SysManager factory)
    static RaftSysMod* create(const char* pModuleName, RaftJsonIF& sysConfig)
    {
        return new DevMan(pModuleName, sysConfig);
    }

protected:

    // Setup
    virtual void setup() override final;

    // Loop (called frequently)
    virtual void loop() override final;

    // Add endpoints
    virtual void addRestAPIEndpoints(RestAPIEndpointManager& pEndpoints) override final;

    // Status
    virtual String getStatusJSON() override final;
    
private:

    // Initialised flag
    bool _isInitialised = false;

    // Mutable data saving
    static const uint32_t MUTABLE_DATA_SAVE_MIN_MS = 5000;
    uint32_t _mutableDataChangeLastMs = 0;
    bool _mutableDataDirty = false;

    // Helper functions
    void deinit();
    RaftRetCode apiControl(const String &reqStr, String &respStr, const APISourceInfo& sourceInfo);
    void saveMutableData();
    void debugShowCurrentState();
    void getStatusHash(std::vector<uint8_t>& stateHash);

    // Pulse count
    RaftJsonNVS _devicesNVConfig;
};
