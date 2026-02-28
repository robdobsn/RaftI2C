/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Device Ident Manager
//
// Rob Dobson 2024
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "DeviceIdentMgr.h"
#include "DeviceTypeRecords.h"
#include "BusRequestInfo.h"
#include "BusRequestResult.h"
#include "RaftDevice.h"
#include "BusI2CAddrAndSlot.h"
#include "PollDataAggregator.h"
#include "Logger.h"
#include <memory>

// Info
#define INFO_NEW_DEVICE_IDENTIFIED

// Debug
// #define DEBUG_DEVICE_IDENT_MGR
// #define DEBUG_DEVICE_IDENT_MGR_DETAIL
// #define DEBUG_HANDLE_BUS_DEVICE_INFO
// #define DEBUG_GET_DECODED_POLL_RESPONSES
// #define DEBUG_MAKE_BUS_REQUEST
// #define DEBUG_MAKE_BUS_REQUEST_VERBOSE
// #define DEBUG_CMD_RESULT_CALLBACK

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Constructor
///////////////////////////////////////////////////////////////////////////////////////////////////////////////

DeviceIdentMgr::DeviceIdentMgr(BusStatusMgr& BusStatusMgr, BusReqSyncFn busReqSyncFn, BusReqAsyncFn busReqAsyncFn) :
    _busStatusMgr(BusStatusMgr),
    _busReqSyncFn(busReqSyncFn),
    _busReqAsyncFn(busReqAsyncFn)
{
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Setup
///////////////////////////////////////////////////////////////////////////////////////////////////////////////

void DeviceIdentMgr::setup(const RaftJsonIF& config)
{
    // Enabled
    _isEnabled = config.getBool("identEnable", true);

    // Debug
    LOG_I(MODULE_PREFIX, "setup %s", _isEnabled ? "enabled" : "disabled");
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get list of device addresses attached to the bus
/// @param pAddrList pointer to array to receive addresses
/// @param onlyAddressesWithIdentPollResponses true to only return addresses with ident poll responses    
void DeviceIdentMgr::getDeviceAddresses(std::vector<BusElemAddrType>& addresses, bool onlyAddressesWithIdentPollResponses) const
{
    // Get list of all bus element addresses
    _busStatusMgr.getBusElemAddresses(addresses, onlyAddressesWithIdentPollResponses);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Identify device
/// @param address address of device
/// @param deviceStatus (out) device status
/// @note This is called from within the scanning code so the device should already be selected if it is on a bus extender, etc.
void DeviceIdentMgr::identifyDevice(BusElemAddrType address, DeviceStatus& deviceStatus)
{
    // Clear device status
    deviceStatus.clear();

    // Check if enabled
    if (!_isEnabled)
    {
#ifdef DEBUG_DEVICE_IDENT_MGR
        LOG_I(MODULE_PREFIX, "identifyDevice disabled");
#endif
        return;
    }

    // Get the raw I2C address (excluding slot number)
    uint32_t i2cAddr = BusI2CAddrAndSlot::getI2CAddr(address);

    // Check if this address is in the range of any known device
    std::vector<uint16_t> deviceTypesForAddr = deviceTypeRecords.getDeviceTypeIdxsForAddr(i2cAddr);
    for (const auto& deviceTypeIdx : deviceTypesForAddr)
    {
        // Get JSON definition for device
        DeviceTypeRecord devTypeRec;
        if (!deviceTypeRecords.getDeviceInfo(deviceTypeIdx, devTypeRec))
            continue;

#ifdef DEBUG_DEVICE_IDENT_MGR
        LOG_I(MODULE_PREFIX, "identifyDevice potential deviceType %s address %s", 
                    devTypeRec.deviceType ? devTypeRec.deviceType : "NO NAME", BusI2CAddrAndSlot::toString(address).c_str());
#endif

        // Check if the detection value(s) match responses from the device
        // Generate a bus request to read the detection value
        if (checkDeviceTypeMatch(address, &devTypeRec))
        {
#ifdef DEBUG_DEVICE_IDENT_MGR_DETAIL
            LOG_I(MODULE_PREFIX, "identifyDevice FOUND %s", devTypeRec.devInfoJson ? devTypeRec.devInfoJson : "NO INFO");
#endif
#ifdef INFO_NEW_DEVICE_IDENTIFIED
            LOG_I(MODULE_PREFIX, "identifyDevice new device %s at address %s", 
                    devTypeRec.deviceType ? devTypeRec.deviceType : "NO NAME", 
                    BusI2CAddrAndSlot::toString(address).c_str());
#endif

            // Initialise the device if required
            processDeviceInit(address, &devTypeRec);

            // Set device type index
            deviceStatus.deviceTypeIndex = deviceTypeIdx;

            // Get polling info
            deviceTypeRecords.getPollInfo(address, &devTypeRec, deviceStatus.deviceIdentPolling);

            // Set polling results size
            auto pDataAggregator = std::make_shared<PollDataAggregator>(
                    deviceStatus.deviceIdentPolling.numPollResultsToStore,
                    deviceStatus.deviceIdentPolling.pollResultSizeIncTimestamp);
            deviceStatus.setAndOwnPollDataAggregator(pDataAggregator);

#ifdef DEBUG_HANDLE_BUS_DEVICE_INFO
            LOG_I(MODULE_PREFIX, "setBusElemDevInfo address %s numPollResToStore %d pollResSizeIncTimestamp %d", 
                    BusI2CAddrAndSlot::toString(address).c_str(),
                    deviceStatus.deviceIdentPolling.numPollResultsToStore,
                    deviceStatus.deviceIdentPolling.pollResultSizeIncTimestamp);
#endif
            // Break out of the loop
            break;
        }
        else
        {
#ifdef DEBUG_DEVICE_IDENT_MGR_DETAIL
            LOG_I(MODULE_PREFIX, "identifyDevice CHECK FAILED %s", devTypeRec.devInfoJson ? devTypeRec.devInfoJson : "NO INFO");
#endif
        }
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Access device and check response
///////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool DeviceIdentMgr::checkDeviceTypeMatch(BusElemAddrType address, const DeviceTypeRecord* pDevTypeRec)
{
    // Get the detection records
    std::vector<DeviceTypeRecords::DeviceDetectionRec> detectionRecs;
    deviceTypeRecords.getDetectionRecs(pDevTypeRec, detectionRecs);

    // Check if all values match
    bool detectionValuesMatch = true;
    for (const auto& detectionRec : detectionRecs)
    {

        // Check there is a read data check
        uint32_t readDataCheckBytes = 0;
        if (detectionRec.checkValues.size() == 0)
            continue;
        readDataCheckBytes = detectionRec.checkValues[0].second.size();

        // Create a bus request to read the detection value
        // Create the poll request
        BusRequestInfo reqRec(BUS_REQ_TYPE_FAST_SCAN, 
                address,
                0, 
                detectionRec.writeData.size(), 
                detectionRec.writeData.data(),
                readDataCheckBytes,
                detectionRec.pauseAfterSendMs, 
                nullptr, 
                this);
        std::vector<uint8_t> readData;
        RaftRetCode rslt = _busReqSyncFn != nullptr ? _busReqSyncFn(&reqRec, &readData) : RAFT_BUS_NOT_INIT;

#ifdef DEBUG_DEVICE_IDENT_MGR
        String writeStr;
        Raft::getHexStrFromBytes(detectionRec.writeData.data(), detectionRec.writeData.size(), writeStr);
        String readDataStr;
        Raft::getHexStrFromBytes(readData.data(), readData.size(), readDataStr);
        LOG_I(MODULE_PREFIX, "checkDeviceTypeMatch %s addr %s writeData %s rslt %d readData %s readSize %d pauseAfterMs %d", 
                    rslt == RAFT_OK ? "OK" : "BUS ACCESS FAILED",
                    BusI2CAddrAndSlot::toString(address).c_str(), 
                    writeStr.c_str(), rslt, 
                    readDataStr.c_str(), readData.size(), 
                    detectionRec.pauseAfterSendMs);
#endif

        // Check ok result
        if (rslt != RAFT_OK)
            return false;

        // Iterate through check values to see if one of them matches
        bool checkValueMatch = false;
        for (const auto& checkValue : detectionRec.checkValues)
        {

#ifdef DEBUG_DEVICE_IDENT_MGR
            String readMaskStr;
            Raft::getHexStrFromBytes(checkValue.first.data(), checkValue.first.size(), readMaskStr);
            String readCheckStr;
            Raft::getHexStrFromBytes(checkValue.second.data(), checkValue.second.size(), readCheckStr);
            LOG_I(MODULE_PREFIX, "checkDeviceTypeMatch readDataMask %s readDataCheck %s VS readData %s",
                        readMaskStr.c_str(),
                        readCheckStr.c_str(),
                        readDataStr.c_str());
#endif

            // Check the read data
            bool sizeMatch = readData.size() == checkValue.second.size();
            if (sizeMatch)
            {
                bool checkByteMatch = true;
                for (int i = 0; i < readData.size(); i++)
                {
                    if ((readData[i] & checkValue.first[i]) != checkValue.second[i])
                    {
                        checkByteMatch = false;
                        break;
                    }
                }
                if (checkByteMatch)
                {
                    checkValueMatch = true;
                    break;
                }
            }

#ifdef DEBUG_DEVICE_IDENT_MGR
            LOG_I(MODULE_PREFIX, "checkDeviceTypeMatch readData %s sizeMatch %d checkValueMatch %d", 
                        readDataStr.c_str(), sizeMatch, checkValueMatch);
#endif
        }

#ifdef DEBUG_DEVICE_IDENT_MGR
        LOG_I(MODULE_PREFIX, "checkDeviceTypeMatch address %s %s", 
                    BusI2CAddrAndSlot::toString(address).c_str(),
                    checkValueMatch ? "MATCH" : "NO MATCH");
#endif

        // Check if all values match
        if (!checkValueMatch)
            detectionValuesMatch = false;

        if (detectionRec.pauseAfterSendMs > 0)
            delay(detectionRec.pauseAfterSendMs);
    }

    // Access the device and check the response
    return detectionValuesMatch;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Process initialisation of a device
///////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool DeviceIdentMgr::processDeviceInit(BusElemAddrType address, const DeviceTypeRecord* pDevTypeRec)
{
    // Get initialisation bus requests
    std::vector<BusRequestInfo> initBusRequests;
    deviceTypeRecords.getInitBusRequests(address, pDevTypeRec, initBusRequests);

#ifdef DEBUG_DEVICE_IDENT_MGR
    LOG_I(MODULE_PREFIX, "processDeviceInit address %s numInitBusRequests %d", 
                BusI2CAddrAndSlot::toString(address).c_str(), initBusRequests.size());
#endif

    // Initialise the device
    for (auto& initBusRequest : initBusRequests)
    {
        std::vector<uint8_t> readData;
        BusRequestInfo reqRec(initBusRequest);
        if (_busReqSyncFn != nullptr)
            _busReqSyncFn(&reqRec, &readData);

        // Check for bar-access time after each request
        if (initBusRequest.getBarAccessForMsAfterSend() > 0)
            delay(initBusRequest.getBarAccessForMsAfterSend());
    }

    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Format device poll responses to JSON
///////////////////////////////////////////////////////////////////////////////////////////////////////////////

String DeviceIdentMgr::deviceStatusToJson(BusElemAddrType address, DeviceOnlineState onlineState, uint16_t deviceTypeIndex, 
                const std::vector<uint8_t>& devicePollResponseData, uint32_t responseSize) const
{
    // Get the poll response JSON using DeviceOnlineState directly
    return deviceTypeRecords.deviceStatusToJson(address, onlineState, deviceTypeIndex, devicePollResponseData);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get JSON for device type info
/// @param address Address of element
/// @param includePlugAndPlayInfo true to include plug and play information
/// @param deviceTypeIndex (out) device type index
/// @return JSON string
String DeviceIdentMgr::getDevTypeInfoJsonByAddr(BusElemAddrType address, bool includePlugAndPlayInfo, DeviceTypeIndexType& deviceTypeIndex) const
{
    // Get device type index
    deviceTypeIndex = _busStatusMgr.getDeviceTypeIndexByAddr(address);
    if (deviceTypeIndex == DEVICE_TYPE_INDEX_INVALID)
        return "{}";

    // Get device type info
    return deviceTypeRecords.getDevTypeInfoJsonByTypeIdx(deviceTypeIndex, includePlugAndPlayInfo);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get JSON for device type info
/// @param deviceType Device type
/// @param includePlugAndPlayInfo true to include plug and play information
/// @param deviceTypeIndex (out) device type index
/// @return JSON string
String DeviceIdentMgr::getDevTypeInfoJsonByTypeName(const String& deviceType, bool includePlugAndPlayInfo, DeviceTypeIndexType& deviceTypeIndex) const
{
    // Get device type info
    return deviceTypeRecords.getDevTypeInfoJsonByTypeName(deviceType, includePlugAndPlayInfo, deviceTypeIndex);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get device type info JSON by device type index
/// @param deviceTypeIdx device type index
/// @param includePlugAndPlayInfo include plug and play info
/// @return JSON string
String DeviceIdentMgr::getDevTypeInfoJsonByTypeIdx(DeviceTypeIndexType deviceTypeIdx, bool includePlugAndPlayInfo) const
{
    // Get device type info
    return deviceTypeRecords.getDevTypeInfoJsonByTypeIdx(deviceTypeIdx, includePlugAndPlayInfo);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get queued device data in JSON format
/// @return JSON doc
String DeviceIdentMgr::getQueuedDeviceDataJson()
{
    // Return string
    String jsonStr;

    // Get list of all bus element addresses
    std::vector<BusElemAddrType> addresses;
    _busStatusMgr.getBusElemAddresses(addresses, false);
    for (auto address : addresses)
    {
        // Get bus status for each address
        DeviceOnlineState onlineState = DeviceOnlineState::OFFLINE;
        uint16_t deviceTypeIndex = 0;
        std::vector<uint8_t> devicePollResponseData;
        uint32_t responseSize = 0;
        _busStatusMgr.getBusElemPollResponses(address, onlineState, deviceTypeIndex, devicePollResponseData, responseSize, 0);

        // Skip unidentified devices
        if (deviceTypeIndex == DEVICE_TYPE_INDEX_INVALID)
            continue;

        // Skip unidentified devices
        if (deviceTypeIndex == DEVICE_TYPE_INDEX_INVALID)
            continue;

        // Use device identity manager to convert to JSON
        String jsonData = deviceStatusToJson(address, 
                        onlineState, deviceTypeIndex, devicePollResponseData, responseSize);
        if (jsonData.length() > 0)
        {
            jsonStr += (jsonStr.length() == 0 ? "{" : ",") + jsonData;
        }
    }

    // Add pending deletion notices (devices that have been removed)
    std::vector<BusStatusMgr::DeletionNotice> deletions;
    _busStatusMgr.getPendingDeletions(deletions);
    for (const auto& deletion : deletions)
    {
        // Generate deletion notice with empty data and PENDING_DELETION state
        std::vector<uint8_t> emptyData;
        String jsonData = deviceStatusToJson(deletion.address, 
                        DeviceOnlineState::PENDING_DELETION, deletion.deviceTypeIndex, emptyData, 0);
        if (jsonData.length() > 0)
        {
            jsonStr += (jsonStr.length() == 0 ? "{" : ",") + jsonData;
        }
    }

    return jsonStr.length() == 0 ? "{}" : jsonStr + "}";
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get queued device data in binary format
/// @param connMode connection mode (inc bus number)
/// @return Binary data vector
std::vector<uint8_t> DeviceIdentMgr::getQueuedDeviceDataBinary(uint32_t connMode)
{
    // Return buffer
    std::vector<uint8_t> binData;

    // Get list of all bus element addresses
    std::vector<BusElemAddrType> addresses;
    _busStatusMgr.getBusElemAddresses(addresses, false);
    for (auto address : addresses)
    {
        // Get bus status for each address
        DeviceOnlineState onlineState = DeviceOnlineState::OFFLINE;
        uint16_t deviceTypeIndex = 0;
        std::vector<uint8_t> devicePollResponseData;
        uint32_t responseSize = 0;
        _busStatusMgr.getBusElemPollResponses(address, onlineState, deviceTypeIndex, devicePollResponseData, responseSize, 0);

        // Skip unidentified devices
        if (deviceTypeIndex == DEVICE_TYPE_INDEX_INVALID)
            continue;

        // Skip unidentified devices
        if (deviceTypeIndex == DEVICE_TYPE_INDEX_INVALID)
            continue;

        // Generate binary device message
        RaftDevice::genBinaryDataMsg(binData, connMode, address, deviceTypeIndex, onlineState, devicePollResponseData);
    }

    // Add pending deletion notices (devices that have been removed)
    std::vector<BusStatusMgr::DeletionNotice> deletions;
    _busStatusMgr.getPendingDeletions(deletions);
    for (const auto& deletion : deletions)
    {
        // Generate deletion notice with empty data and PENDING_DELETION state
        std::vector<uint8_t> emptyData;
        RaftDevice::genBinaryDataMsg(binData, connMode, deletion.address, deletion.deviceTypeIndex, 
                        DeviceOnlineState::PENDING_DELETION, emptyData);
    }

    // Return binary data
    return binData;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get decoded poll responses
/// @param address address of device to get data from
/// @param pStructOut pointer to structure (or array of structures) to receive decoded data
/// @param structOutSize size of structure (in bytes) to receive decoded data
/// @param maxRecCount maximum number of records to decode
/// @param decodeState decode state for this device
/// @return number of records decoded
/// @note the pStructOut should generally point to structures of the correct type for the device data and the
///       decodeState should be maintained between calls for the same device
uint32_t DeviceIdentMgr::getDecodedPollResponses(BusElemAddrType address, 
                void* pStructOut, uint32_t structOutSize, 
                uint16_t maxRecCount, RaftBusDeviceDecodeState& decodeState) const
{
    // Get poll result for each address
    DeviceOnlineState onlineState = DeviceOnlineState::OFFLINE;
    uint16_t deviceTypeIndex = 0;
    std::vector<uint8_t> devicePollResponseData;
    uint32_t responseSize = 0;
    _busStatusMgr.getBusElemPollResponses(address, onlineState, deviceTypeIndex, devicePollResponseData, responseSize, 0);

#ifdef DEBUG_GET_DECODED_POLL_RESPONSES
    LOG_I(MODULE_PREFIX, "getDecodedPollResponses address %s onlineState %s deviceTypeIndex %d responseSize %d",
                BusI2CAddrAndSlot::toString(address).c_str(),
                BusAddrStatus::getOnlineStateStr(onlineState), deviceTypeIndex, responseSize);
#endif

    // Decode the poll response
    return decodePollResponses(deviceTypeIndex, devicePollResponseData.data(), responseSize, 
                pStructOut, structOutSize, 
                maxRecCount, decodeState);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get debug JSON
/// @return JSON string
String DeviceIdentMgr::getDebugJSON(bool includeBraces) const
{
    return _busStatusMgr.getDebugJSON(includeBraces);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Decode one or more poll responses for a device
/// @param deviceTypeIndex index of device type
/// @param pPollBuf buffer containing poll responses
/// @param pollBufLen length of poll response buffer
/// @param pStructOut pointer to structure (or array of structures) to receive decoded data
/// @param structOutSize size of structure (in bytes) to receive decoded data (includes timestamp)
/// @param maxRecCount maximum number of records to decode
/// @return number of records decoded
uint32_t DeviceIdentMgr::decodePollResponses(uint16_t deviceTypeIndex, 
            const uint8_t* pPollBuf, uint32_t pollBufLen, 
            void* pStructOut, uint32_t structOutSize, 
            uint16_t maxRecCount, RaftBusDeviceDecodeState& decodeState) const
{
    // Get device type info
    DeviceTypeRecord devTypeRec;
    if (!deviceTypeRecords.getDeviceInfo(deviceTypeIndex, devTypeRec))
        return 0;

    // Check the decode method is present
    if (!devTypeRec.pollResultDecodeFn)
        return 0;

    // Decode the poll response
    return devTypeRec.pollResultDecodeFn(pPollBuf, pollBufLen, pStructOut, structOutSize, maxRecCount, decodeState);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Send command to device on bus
/// @param cmdJSON Command JSON string
/// @param respMsg (out) response message from the device
/// @return Result code
/// @note The JSON string should include:
///       - "hexWr": hex string of data to write to the device
///       - "numToRd": number of bytes to read from the device (optional)
RaftRetCode DeviceIdentMgr::sendCmdToDevice(RaftDeviceID deviceID, const char* cmdJSON, String* respMsg)
{
    // Get command and parameters from JSON
    RaftJson cmdJsonObj(cmdJSON);
    String hexWriteData = cmdJsonObj.getString("hexWr", "");
    int numBytesToRead = cmdJsonObj.getLong("numToRd", 0);

    // Convert hex write data to binary
    uint32_t numBytesToWrite = hexWriteData.length() / 2;
    std::vector<uint8_t> writeVec;
    writeVec.resize(numBytesToWrite);
    uint32_t writeBytesLen = Raft::getBytesFromHexStr(hexWriteData.c_str(), writeVec.data(), numBytesToWrite);
    writeVec.resize(writeBytesLen);

    // Create element request
    static const uint32_t CMDID_CMDRAW = 100;
    HWElemReq hwElemReq = {writeVec, numBytesToRead, CMDID_CMDRAW, "cmdraw", 0};

    // Form bus request
    BusRequestInfo busReqInfo("", deviceID.getAddress());
    busReqInfo.set(BUS_REQ_TYPE_STD, hwElemReq, 0,
            [](void* pCallbackData, BusRequestResult& reqResult)
                {
                    if (pCallbackData)
                        ((DeviceIdentMgr*)pCallbackData)->cmdResultReportCallback(reqResult);
                },
            this);

    // Make the request
    RaftRetCode rslt = RAFT_INVALID_DATA;
    if (_busReqAsyncFn != nullptr)
    {
        rslt = _busReqAsyncFn(&busReqInfo, 0) ? RAFT_OK : RAFT_BUS_NOT_INIT;
        if (respMsg)
            *respMsg = rslt ? "Command sent" : "Failed to send command";
    }
    else
    {
        if (respMsg)
            *respMsg = "Bus not initialised";
    }

#ifdef DEBUG_MAKE_BUS_REQUEST_VERBOSE
    String outStr;
    Raft::getHexStrFromBytes(hwElemReq._writeData.data(),
                hwElemReq._writeData.size() > 16 ? 16 : hwElemReq._writeData.size(),
                outStr);
    LOG_I(MODULE_PREFIX, "sendCmdToDevice resp %s deviceId %s len %d data %s ...",
                    respMsg ? respMsg->c_str() : String(Raft::getRetCodeStr(rslt)).c_str(),
                    deviceID.toString().c_str(),
                    hwElemReq._writeData.size(),
                    outStr.c_str());
#endif

    return rslt;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Callback for command result reports
/// @param reqResult Result of the bus request
void DeviceIdentMgr::cmdResultReportCallback(BusRequestResult &reqResult)
{
#ifdef DEBUG_CMD_RESULT_CALLBACK
    LOG_I(MODULE_PREFIX, "cmdResultReportCallback len %d", reqResult.getReadDataLen());
    Raft::logHexBuf(reqResult.getReadData(), reqResult.getReadDataLen(), MODULE_PREFIX, "cmdResultReportCallback");
#endif
}