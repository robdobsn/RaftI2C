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
#include "BusRequestInfo.h"

// Debug
#define DEBUG_MAKE_BUS_REQUEST_VERBOSE
#define DEBUG_API_CMDRAW
#define DEBUG_CMD_RESULT

static const char *MODULE_PREFIX = "HWDevMan";

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
    _raftBusSystem.registerBus("I2C", BusI2C::createFn);

    // Setup buses
    _raftBusSystem.setup("Buses", modConfig(),
            std::bind(&HWDevMan::busElemStatusCB, this, std::placeholders::_1, std::placeholders::_2),
            std::bind(&HWDevMan::busOperationStatusCB, this, std::placeholders::_1, std::placeholders::_2)
    );

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
    _raftBusSystem.loop();
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Endpoints
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void HWDevMan::addRestAPIEndpoints(RestAPIEndpointManager &endpointManager)
{
    // REST API endpoints
    endpointManager.addEndpoint("devman", RestAPIEndpoint::ENDPOINT_CALLBACK, RestAPIEndpoint::ENDPOINT_GET,
                            std::bind(&HWDevMan::apiDevMan, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3),
                            "devman/typeinfo?bus=<busName>&type=<typename> - Get device info for type, devman/cmdraw?bus=<busName>&addr=<addr>&hexWr=<hexWriteData>&numToRd=<numBytesToRead>&msgKey=<msgKey> - Send raw command to device");
    LOG_I(MODULE_PREFIX, "addRestAPIEndpoints added devman");
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// REST API DevMan
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

RaftRetCode HWDevMan::apiDevMan(const String &reqStr, String &respStr, const APISourceInfo& sourceInfo)
{
    // Get device info
    std::vector<String> params;
    std::vector<RaftJson::NameValuePair> nameValues;
    RestAPIEndpointManager::getParamsAndNameValues(reqStr.c_str(), params, nameValues);
    RaftJson jsonParams = RaftJson::getJSONFromNVPairs(nameValues, true); 

    // Get command
    String cmdName = reqStr;
    if (params.size() > 1)
        cmdName = params[1];

    // Check command
    if (cmdName.equalsIgnoreCase("typeinfo"))
    {
        // Get bus name
        String busName = jsonParams.getString("bus", "");
        if (busName.length() == 0)
            return Raft::setJsonErrorResult(reqStr.c_str(), respStr, "failBusMissing");

        // Get device name
        String devTypeName = jsonParams.getString("type", "");
        if (devTypeName.length() == 0)
            return Raft::setJsonErrorResult(reqStr.c_str(), respStr, "failTypeMissing");

        // Find the bus
        RaftBus* pBus = _raftBusSystem.getBusByName(busName);
        if (!pBus)
            return Raft::setJsonErrorResult(reqStr.c_str(), respStr, "failBusNotFound");

        // Get devices interface
        RaftBusDevicesIF* pDevicesIF = pBus->getBusDevicesIF();
        if (!pDevicesIF)
            return Raft::setJsonErrorResult(reqStr.c_str(), respStr, "failTypeNotFound");

        // Get device info
        String devInfo = pDevicesIF->getDevTypeInfoJsonByTypeName(devTypeName, false);
        if (devInfo.length() == 0)
        {
            return Raft::setJsonErrorResult(reqStr.c_str(), respStr, "failTypeNotFound");
        }

        // Set result
        return Raft::setJsonBoolResult(reqStr.c_str(), respStr, true, ("\"devinfo\":" + devInfo).c_str());
    }

    // Check for raw command
    if (cmdName.equalsIgnoreCase("cmdraw"))
    {
        // Get bus name
        String busName = jsonParams.getString("bus", "");
        if (busName.length() == 0)
            return Raft::setJsonErrorResult(reqStr.c_str(), respStr, "failBusMissing");

        // Get args
        String addrStr = jsonParams.getString("addr", "");
        String hexWriteData = jsonParams.getString("hexWr", "");
        int numBytesToRead = jsonParams.getLong("numToRd", 0);
        // String msgKey = jsonParams.getString("msgKey", "");

        // Check valid
        if (addrStr.length() == 0)
            return Raft::setJsonErrorResult(reqStr.c_str(), respStr, "failMissingAddr");

        // Find the bus
        RaftBus* pBus = _raftBusSystem.getBusByName(busName);
        if (!pBus)
            return Raft::setJsonErrorResult(reqStr.c_str(), respStr, "failBusNotFound");

        // Convert address
        uint32_t addr = pBus->stringToAddr(addrStr);

        // Get bytes to write
        uint32_t numBytesToWrite = hexWriteData.length() / 2;
        std::vector<uint8_t> writeVec;
        writeVec.resize(numBytesToWrite);
        uint32_t writeBytesLen = Raft::getBytesFromHexStr(hexWriteData.c_str(), writeVec.data(), numBytesToWrite);
        writeVec.resize(writeBytesLen);

        // Store the msg key for response
        // TODO store the msgKey for responses
        // _cmdResponseMsgKey = msgKey;

        // Form HWElemReq
        static const uint32_t CMDID_CMDRAW = 100;
        HWElemReq hwElemReq = {writeVec, numBytesToRead, CMDID_CMDRAW, "cmdraw", 0};

        // Form request
        BusRequestInfo busReqInfo("", addr);
        busReqInfo.set(BUS_REQ_TYPE_STD, hwElemReq, 0, 
                [](void* pCallbackData, BusRequestResult& reqResult)
                    {
                        if (pCallbackData)
                            ((HWDevMan *)pCallbackData)->cmdResultReportCallback(reqResult);
                    }, 
                this);

#ifdef DEBUG_MAKE_BUS_REQUEST_VERBOSE
        String outStr;
        Raft::getHexStrFromBytes(hwElemReq._writeData.data(), 
                    hwElemReq._writeData.size() > 16 ? 16 : hwElemReq._writeData.size(),
                    outStr);
        LOG_I(MODULE_PREFIX, "apiHWDevice addr %s len %d data %s ...", 
                        addrStr.c_str(), 
                        hwElemReq._writeData.size(),
                        outStr.c_str());
#endif

        bool rslt = pBus->addRequest(busReqInfo);
        if (!rslt)
        {
            LOG_W(MODULE_PREFIX, "apiHWDevice failed send raw command");
        }

        // Debug
#ifdef DEBUG_API_CMDRAW
        LOG_I(MODULE_PREFIX, "apiHWDevice hexWriteData %s numToRead %d", hexWriteData.c_str(), numBytesToRead);
#endif

        // Set result
        return Raft::setJsonBoolResult(reqStr.c_str(), respStr, rslt);    
    }

    // Set result
    return Raft::setJsonErrorResult(reqStr.c_str(), respStr, "failUnknownCmd");
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Get JSON status
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

String HWDevMan::getStatusJSON() const
{
    String jsonStr;
    for (RaftBus* pBus : _raftBusSystem.getBusList())
    {
        if (!pBus)
            continue;
        // Get device interface
        RaftBusDevicesIF* pDevicesIF = pBus->getBusDevicesIF();
        if (!pDevicesIF)
            continue; 
        String jsonRespStr = pDevicesIF->getPollResponsesJson();
        if (jsonRespStr.length() > 0)
        {
            jsonStr += (jsonStr.length() == 0 ? "{\"" : ",\"") + pBus->getBusName() + "\":" + jsonRespStr;
        }
    }

    // LOG_I(MODULE_PREFIX, "getStatusJSON %s", (jsonStr.length() == 0 ? "{}" : (jsonStr + "}").c_str()));

    return jsonStr.length() == 0 ? "{}" : jsonStr + "}";

    // TODO - this is for non-JSON poll data

    // // Get addresses with poll data
    // std::vector<uint32_t> addresses;
    // ((BusI2C*)pBus)->getIdentPollResponseAddresses(addresses);
    // if (addresses.size() == 0)
    // {
    //     return "{}";
    // }

    // // Form a JSON reponse
    // String jsonResponse;

    // // Iterate addresses and get poll data
    // for (uint32_t addr : addresses)
    // {
    //     // Get poll data
    //     std::vector<uint8_t> pollResponseData;
    //     const uint32_t responseSize = 0;
    //     uint32_t numResponses = ((BusI2C*)pBus)->getIdentPollResponses(addr, pollResponseData, responseSize, 0);

    //     // Add to JSON
    //     jsonResponse += "\"" + String(addr) + "\":" + pollData.getJSON() + ",";
    // }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Check status change
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void HWDevMan::getStatusHash(std::vector<uint8_t>& stateHash)
{
    stateHash.clear();

    // Check all buses for data
    for (RaftBus* pBus : _raftBusSystem.getBusList())
    {
        // Check bus
        if (pBus)
        {
            // Check bus status
            uint32_t identPollLastMs = pBus->getLastStatusUpdateMs(true, true);
            stateHash.push_back(identPollLastMs & 0xff);
            stateHash.push_back((identPollLastMs >> 8) & 0xff);
        }
    }
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
    _raftBusSystem.deinit();

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
// Bus operation status callback
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void HWDevMan::busOperationStatusCB(RaftBus& bus, BusOperationStatus busOperationStatus)
{
    // Debug
    LOG_I(MODULE_PREFIX, "busOperationStatusInfo %s %s", bus.getBusName().c_str(), 
        RaftBus::busOperationStatusToString(busOperationStatus));
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Bus element status callback
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void HWDevMan::busElemStatusCB(RaftBus& bus, const std::vector<BusElemAddrAndStatus>& statusChanges)
{
    // Debug
    for (const auto& el : statusChanges)
    {
        LOG_I(MODULE_PREFIX, "busElemStatusInfo %s %s %s", bus.getBusName().c_str(), 
            bus.addrToString(el.address).c_str(), el.isChangeToOnline ? "Online" : ("Offline" + String(el.isChangeToOffline ? " (was online)" : "")).c_str());
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Cmd result report callbacks
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void HWDevMan::cmdResultReportCallback(BusRequestResult &reqResult)
{
#ifdef DEBUG_CMD_RESULT
    LOG_I(MODULE_PREFIX, "cmdResultReportCallback len %d", reqResult.getReadDataLen());
    Raft::logHexBuf(reqResult.getReadData(), reqResult.getReadDataLen(), MODULE_PREFIX, "cmdResultReportCallback");
#endif

    // // Generate hex response
    // String hexResp;
    // Raft::getHexStrFromBytes(reqResult.getReadData(), reqResult.getReadDataLen(), hexResp);

    // Report
    // TODO 
//     RICRESTMsg ricRESTMsg;
//     ricRESTMsg.setElemCode(RICRESTMsg::RICREST_ELEM_CODE_CMDRESPJSON);
//     CommsChannelMsg endpointMsg(MSG_CHANNEL_ID_ALL, MSG_PROTOCOL_RICREST, 0, MSG_TYPE_REPORT);
//     char msgBuf[200];
//     snprintf(msgBuf, sizeof(msgBuf), R"({"msgType":"raw","hexRd":"%s","elemName":"%s","IDNo":%d,"msgKey":"%s","addr":"0x%02x"})", 
//                 hexResp.c_str(), _name.c_str(), (int)_IDNo, _cmdResponseMsgKey.c_str(), _address);
//     ricRESTMsg.encode(msgBuf, endpointMsg, RICRESTMsg::RICREST_ELEM_CODE_CMDRESPJSON);

// #ifdef DEBUG_CMD_RESULT
//     LOG_I(MODULE_PREFIX, "cmdResultReportCallback responseJSON %s", msgBuf);
// #endif

//     // Send message on the appropriate channel
//     if (_pCommsCore)
//         _pCommsCore->outboundHandleMsg(endpointMsg);
}