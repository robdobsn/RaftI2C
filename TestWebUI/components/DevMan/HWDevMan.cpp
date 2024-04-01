/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Hardware Device Manager
//
// Rob Dobson 2024
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include <time.h>
#include "Logger.h"
#include "RaftArduino.h"
#include "HWDevMan.h"
#include "RaftUtils.h"
#include "RestAPIEndpointManager.h"
#include "SysManager.h"
#include "CommsChannelMsg.h"
#include "BusI2C.h"

// Warn
#define WARN_ON_NO_BUSES_DEFINED

// Debug
#define DEBUG_BUSES_CONFIGURATION

static const char *MODULE_PREFIX = "HWDevMan";

// Debug supervisor step (for hangup detection within a service call)
// Uses global logger variables - see logger.h
#define DEBUG_GLOB_HWDEVMAN 2

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Constructor
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

HWDevMan::HWDevMan(const char *pModuleName, RaftJsonIF& sysConfig)
        : RaftSysMod(pModuleName, sysConfig),
          _devicesNVConfig("HWDevMan")
{
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Destructor
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

HWDevMan::~HWDevMan()
{
    deinit();
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Setup
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void HWDevMan::setup()
{
    // Debug
    LOG_I(MODULE_PREFIX, "setup enabled");

    // Register BusI2C
    busRegister("I2C", BusI2C::createFn);

    // Setup buses
    _supervisorBusFirstIdx = _supervisorStats.getCount();
    setupBuses();

    // Debug show state
    debugShowCurrentState();

    // Setup publisher with callback functions
    SysManager* pSysManager = getSysManager();
    if (pSysManager)
    {
        // Register publish message generator
        pSysManager->sendMsgGenCB("Publish", "devices", 
            [this](const char* messageName, CommsChannelMsg& msg) {
                String statusStr = getStatusJSON();
                msg.setFromBuffer((uint8_t*)statusStr.c_str(), statusStr.length());
                return true;
            },
            [this](const char* messageName, std::vector<uint8_t>& stateHash) {
                return getStatusHash(stateHash);
            }
        );
    }

    // HW Now initialised
    _isInitialised = true;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Loop
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void HWDevMan::loop()
{
    // Check init
    if (!_isInitialised)
        return;

    // Check if mutable data changed
    if (_mutableDataDirty)
    {
        // Check if min time has passed
        if (Raft::isTimeout(millis(), _mutableDataChangeLastMs, MUTABLE_DATA_SAVE_MIN_MS))
        {
            // Save mutable data
            saveMutableData();
            _mutableDataDirty = false;
        }
    }

    // Service the buses
    uint32_t busIdx = 0;
    for (BusBase* pBus : _busList)
    {
        if (pBus)
        {
            SUPERVISE_LOOP_CALL(_supervisorStats, _supervisorBusFirstIdx+busIdx, DEBUG_GLOB_HWDEVMAN, pBus->service())
        }
        busIdx++;
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Endpoints
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void HWDevMan::addRestAPIEndpoints(RestAPIEndpointManager &endpointManager)
{
    // Control shade
    endpointManager.addEndpoint("devmantest", RestAPIEndpoint::ENDPOINT_CALLBACK, RestAPIEndpoint::ENDPOINT_GET,
                            std::bind(&HWDevMan::apiControl, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3),
                            "devmantest");
    LOG_I(MODULE_PREFIX, "addRestAPIEndpoints devmantest");
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Control via API
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

RaftRetCode HWDevMan::apiControl(const String &reqStr, String &respStr, const APISourceInfo& sourceInfo)
{
    // Check for set pulse count
    String cmdStr = RestAPIEndpointManager::getNthArgStr(reqStr.c_str(), 1);
    LOG_I(MODULE_PREFIX, "apiControl cmdStr %s", cmdStr.c_str());

    // Set result
    return Raft::setJsonBoolResult(reqStr.c_str(), respStr, false);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Get JSON status
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

String HWDevMan::getStatusJSON()
{
    // Pulse count JSON if enabled
    String pulseCountStr = R"("testingtesting":)" + String(123);

    // Add base JSON
    return "{" + pulseCountStr + "}";
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Check status change
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void HWDevMan::getStatusHash(std::vector<uint8_t>& stateHash)
{
    // TODO - implement state
    int v = 0;
    stateHash.clear();
    stateHash.push_back(v & 0xff);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Write mutable data
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void HWDevMan::saveMutableData()
{
    // Save pulse count
    String jsonConfig = "\"testingtesting\":" + String(123);

    // Add outer brackets
    jsonConfig = "{" + jsonConfig + "}";
#ifdef DEBUG_PULSE_COUNTER_MUTABLE_DATA
    LOG_I(MODULE_PREFIX, "saveMutableData %s", jsonConfig.c_str());
#endif

    // Set JSON
    _devicesNVConfig.setJsonDoc(jsonConfig.c_str());
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Deinit
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void HWDevMan::deinit()
{
    // Deinit buses
    for (BusBase* pBus : _busList)
    {
        delete pBus;
    }
    _busList.clear();

    // Deinit done
    _isInitialised = false;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// debug show states
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void HWDevMan::debugShowCurrentState()
{
    LOG_I(MODULE_PREFIX, "debugShowCurrentState testingtesting %d", 123);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Setup Buses
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void HWDevMan::setupBuses()
{
    // Config prefixed for buses
    RaftJsonPrefixed busesConfig(modConfig(), "Buses");

    // Buses list
    std::vector<String> busesListJSONStrings;
    if (!busesConfig.getArrayElems("buslist", busesListJSONStrings))
    {
#ifdef WARN_ON_NO_BUSES_DEFINED
        LOG_W(MODULE_PREFIX, "No buses defined");
#endif
        return;
    }

    // Iterate bus configs
    for (RaftJson busConfig : busesListJSONStrings)
    {
        // Get bus type
        String busType = busConfig.getString("type", "");

#ifdef DEBUG_BUSES_CONFIGURATION
        LOG_I(MODULE_PREFIX, "setting up bus type %s with %s", busType.c_str(), busConfig.c_str());
#endif

        // Create bus
        BusBase* pNewBus = busFactoryCreate(busType.c_str(), 
            std::bind(&HWDevMan::busElemStatusCB, this, std::placeholders::_1, std::placeholders::_2),
            std::bind(&HWDevMan::busOperationStatusCB, this, std::placeholders::_1, std::placeholders::_2)
        );

        // Setup if valid
        if (pNewBus)
        {
            if (pNewBus->setup(busConfig))
            {
                // Add to bus list
                _busList.push_back(pNewBus);

                // Add to supervisory
                _supervisorStats.add(pNewBus->getBusName().c_str());
            }
        }
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Register Bus type
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void HWDevMan::busRegister(const char* busConstrName, BusFactoryCreatorFn busCreateFn)
{
    // See if already registered
    BusFactoryTypeDef newElem(busConstrName, busCreateFn);
    for (const BusFactoryTypeDef& el : _busFactoryTypeList)
    {
        if (el.isIdenticalTo(newElem))
            return;
    }
    _busFactoryTypeList.push_back(newElem);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Create bus of specified type
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

BusBase* HWDevMan::busFactoryCreate(const char* busConstrName, BusElemStatusCB busElemStatusCB, 
                        BusOperationStatusCB busOperationStatusCB)
{
    for (const auto& el : _busFactoryTypeList)
    {
        if (el.nameMatch(busConstrName))
        {
#ifdef DEBUG_BUS_FACTORY_CREATE
            LOG_I(MODULE_PREFIX, "create bus %s", busConstrName);
#endif
            return el._createFn(busElemStatusCB, busOperationStatusCB);
        }
    }
    return nullptr;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Bus operation status callback
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void HWDevMan::busOperationStatusCB(BusBase& bus, BusOperationStatus busOperationStatus)
{
    // Debug
    LOG_I(MODULE_PREFIX, "busOperationStatusInfo %s %s", bus.getBusName().c_str(), 
        BusBase::busOperationStatusToString(busOperationStatus));
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Bus element status callback
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void HWDevMan::busElemStatusCB(BusBase& bus, const std::vector<BusElemAddrAndStatus>& statusChanges)
{
    // Debug
    for (const auto& el : statusChanges)
    {
        LOG_I(MODULE_PREFIX, "busElemStatusInfo %s %s", bus.getBusName().c_str(), 
            bus.busElemAddrAndStatusToString(el).c_str());
    }
}
