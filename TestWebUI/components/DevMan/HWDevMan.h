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
#include "SupervisorStats.h"
#include "BusRequestResult.h"

class APISourceInfo;

// Bus factory creator function
typedef BusBase* (*BusFactoryCreatorFn)(BusElemStatusCB busElemStatusCB, BusOperationStatusCB busOperationStatusCB);

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

    // Supervisor statistics
    SupervisorStats _supervisorStats;
    uint8_t _supervisorBusFirstIdx = 0;

    // Bus Factory
    BusBase* busFactoryCreate(const char* busName, BusElemStatusCB busElemStatusCB, 
                        BusOperationStatusCB busOperationStatusCB);
    class BusFactoryTypeDef
    {
    public:
        BusFactoryTypeDef(const String& name, BusFactoryCreatorFn createFn)
        {
            _name = name;
            _createFn = createFn;
        }
        bool isIdenticalTo(const BusFactoryTypeDef& other) const
        {
            if (!_name.equalsIgnoreCase(other._name))
                return false;
            return _createFn == other._createFn;
        }
        bool nameMatch(const String& name) const
        {
            return _name.equalsIgnoreCase(name);
        }
        String _name;
        BusFactoryCreatorFn _createFn;
    };

    // List of bus types that can be created
    std::list<BusFactoryTypeDef> _busFactoryTypeList;

    // List of buses
    std::list<BusBase*> _busList;

    // Helper functions
    void deinit();
    RaftRetCode apiDevMan(const String &reqStr, String &respStr, const APISourceInfo& sourceInfo);
    void saveMutableData();
    void debugShowCurrentState();
    void getStatusHash(std::vector<uint8_t>& stateHash);
    void busRegister(const char* busConstrName, BusFactoryCreatorFn busCreateFn);
    void setupBuses();
    BusBase* getBusByName(const String& busName);
    void cmdResultReportCallback(BusRequestResult& reqResult);
 
    // Bus operation and status functions
    void busElemStatusCB(BusBase& bus, const std::vector<BusElemAddrAndStatus>& statusChanges);
    void busOperationStatusCB(BusBase& bus, BusOperationStatus busOperationStatus);

    // Pulse count
    RaftJsonNVS _devicesNVConfig;
};
