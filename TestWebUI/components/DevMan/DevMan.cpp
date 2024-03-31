/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// DevMan.cpp
//
// Rob Dobson 2024
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include <time.h>
#include "Logger.h"
#include "RaftArduino.h"
#include "DevMan.h"
#include "RaftUtils.h"
#include "RestAPIEndpointManager.h"
#include "SysManager.h"
#include "CommsChannelMsg.h"

static const char *MODULE_PREFIX = "DevMan";

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Constructor
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

DevMan::DevMan(const char *pModuleName, RaftJsonIF& sysConfig)
        : RaftSysMod(pModuleName, sysConfig),
          _devicesNVConfig("DevMan")
{
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Setup
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void DevMan::setup()
{
    // Debug
    LOG_I(MODULE_PREFIX, "setup enabled");

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

void DevMan::loop()
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
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Endpoints
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void DevMan::addRestAPIEndpoints(RestAPIEndpointManager &endpointManager)
{
    // Control shade
    endpointManager.addEndpoint("devmantest", RestAPIEndpoint::ENDPOINT_CALLBACK, RestAPIEndpoint::ENDPOINT_GET,
                            std::bind(&DevMan::apiControl, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3),
                            "devmantest");
    LOG_I(MODULE_PREFIX, "addRestAPIEndpoints devmantest");
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Control via API
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

RaftRetCode DevMan::apiControl(const String &reqStr, String &respStr, const APISourceInfo& sourceInfo)
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

String DevMan::getStatusJSON()
{
    // Pulse count JSON if enabled
    String pulseCountStr = R"("testingtesting":)" + String(123);

    // Add base JSON
    return "{" + pulseCountStr + "}";
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Check status change
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void DevMan::getStatusHash(std::vector<uint8_t>& stateHash)
{
    // TODO - implement state
    int v = 0;
    stateHash.clear();
    stateHash.push_back(v & 0xff);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Write mutable data
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void DevMan::saveMutableData()
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
// debug show states
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void DevMan::debugShowCurrentState()
{
    LOG_I(MODULE_PREFIX, "debugShowCurrentState testingtesting %d", 123);
}

