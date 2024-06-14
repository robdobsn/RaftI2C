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
#include "Logger.h"

// #define DEBUG_DEVICE_IDENT_MGR
// #define DEBUG_DEVICE_IDENT_MGR_DETAIL
// #define DEBUG_HANDLE_BUS_DEVICE_INFO

static const char* MODULE_PREFIX = "DeviceIdentMgr";

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Consructor
///////////////////////////////////////////////////////////////////////////////////////////////////////////////

DeviceIdentMgr::DeviceIdentMgr(BusStatusMgr& BusStatusMgr, BusMultiplexers& busMultiplexers, BusI2CReqSyncFn busI2CReqSyncFn) :
    _busStatusMgr(BusStatusMgr),
    _busMultiplexers(busMultiplexers),
    _busI2CReqSyncFn(busI2CReqSyncFn)
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
    LOG_I(MODULE_PREFIX, "DeviceIdentMgr setup %s", _isEnabled ? "enabled" : "disabled");
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get list of device addresses attached to the bus
/// @param pAddrList pointer to array to receive addresses
/// @param onlyAddressesWithIdentPollResponses true to only return addresses with ident poll responses    
void DeviceIdentMgr::getDeviceAddresses(std::vector<uint32_t>& addresses, bool onlyAddressesWithIdentPollResponses) const
{
    // Get list of all bus element addresses
    _busStatusMgr.getBusElemAddresses(addresses, onlyAddressesWithIdentPollResponses);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Identify device
/// @param addrAndSlot address and slot
/// @param deviceStatus (out) device status
/// @note This is called from within the scanning code so the device should already be selected if it is on a bus extender, etc.
void DeviceIdentMgr::identifyDevice(const BusI2CAddrAndSlot& addrAndSlot, DeviceStatus& deviceStatus)
{
    // Clear device status
    deviceStatus.clear();

    // Check if enabled
    if (!_isEnabled)
    {
#ifdef DEBUG_DEVICE_IDENT_MGR
        LOG_I(MODULE_PREFIX, "Device identification disabled");
#endif
        return;
    }

    // Check if this address is in the range of any known device
    std::vector<uint16_t> deviceTypesForAddr = deviceTypeRecords.getDeviceTypeIdxsForAddr(addrAndSlot.addr);
    for (const auto& deviceTypeIdx : deviceTypesForAddr)
    {
        // Get JSON definition for device
        const DeviceTypeRecord* pDevTypeRec = deviceTypeRecords.getDeviceInfo(deviceTypeIdx);
        if (!pDevTypeRec)
            continue;

#ifdef DEBUG_DEVICE_IDENT_MGR
        LOG_I(MODULE_PREFIX, "identifyDevice potential deviceType %s addr@slotNum %s", 
                    pDevTypeRec->deviceType, addrAndSlot.toString().c_str());
#endif

        // Check if the detection value(s) match responses from the device
        // Generate a bus request to read the detection value
        if (checkDeviceTypeMatch(addrAndSlot, pDevTypeRec))
        {
#ifdef DEBUG_DEVICE_IDENT_MGR_DETAIL
            LOG_I(MODULE_PREFIX, "identifyDevice FOUND %s", pDevTypeRec->devInfoJson);
#endif
            // Initialise the device if required
            processDeviceInit(addrAndSlot, pDevTypeRec);

            // Set device type index
            deviceStatus.deviceTypeIndex = deviceTypeIdx;

            // Get polling info
            deviceTypeRecords.getPollInfo(addrAndSlot.toCompositeAddrAndSlot(), pDevTypeRec, deviceStatus.deviceIdentPolling);

            // Set polling results size
            deviceStatus.dataAggregator.init(deviceStatus.deviceIdentPolling.numPollResultsToStore, 
                    deviceStatus.deviceIdentPolling.pollResultSizeIncTimestamp);

#ifdef DEBUG_HANDLE_BUS_DEVICE_INFO
            LOG_I(MODULE_PREFIX, "setBusElemDevInfo addr@slotNum %s numPollResToStore %d pollResSizeIncTimestamp %d", 
                    addrAndSlot.toString().c_str(),
                    deviceStatus.deviceIdentPolling.numPollResultsToStore,
                    deviceStatus.deviceIdentPolling.pollResultSizeIncTimestamp);
#endif
        }
        else
        {
#ifdef DEBUG_DEVICE_IDENT_MGR_DETAIL
            LOG_I(MODULE_PREFIX, "identifyDevice CHECK FAILED %s", pDevTypeRec->devInfoJson);
#endif
        }
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Access device and check response
///////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool DeviceIdentMgr::checkDeviceTypeMatch(const BusI2CAddrAndSlot& addrAndSlot, const DeviceTypeRecord* pDevTypeRec)
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
        BusI2CRequestRec reqRec(BUS_REQ_TYPE_FAST_SCAN, 
                addrAndSlot,
                0, 
                detectionRec.writeData.size(), 
                detectionRec.writeData.data(),
                readDataCheckBytes,
                detectionRec.pauseAfterSendMs, 
                nullptr, 
                this);
        std::vector<uint8_t> readData;
        RaftI2CCentralIF::AccessResultCode rslt = _busI2CReqSyncFn(&reqRec, &readData);

#ifdef DEBUG_DEVICE_IDENT_MGR
        String writeStr;
        Raft::getHexStrFromBytes(detectionRec.writeData.data(), detectionRec.writeData.size(), writeStr);
        String readDataStr;
        Raft::getHexStrFromBytes(readData.data(), readData.size(), readDataStr);
        LOG_I(MODULE_PREFIX, "checkDeviceTypeMatch %s addr@slotNum %s writeData %s rslt %d readData %s readSize %d pauseAfterMs %d", 
                    rslt == RaftI2CCentralIF::ACCESS_RESULT_OK ? "OK" : "BUS ACCESS FAILED",
                    addrAndSlot.toString().c_str(), writeStr.c_str(), rslt, readDataStr.c_str(), readData.size(), detectionRec.pauseAfterSendMs);
#endif

        // Check ok result
        if (rslt != RaftI2CCentralIF::ACCESS_RESULT_OK)
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
        LOG_I(MODULE_PREFIX, "checkDeviceTypeMatch addr@slotNum %s %s", 
                    addrAndSlot.toString().c_str(),
                    checkValueMatch ? "MATCH" : "NO MATCH");
#endif

        // Check if all values match
        if (!checkValueMatch)
            detectionValuesMatch = false;
    }

    // Access the device and check the response
    return detectionValuesMatch;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Process initialisation of a device
///////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool DeviceIdentMgr::processDeviceInit(const BusI2CAddrAndSlot& addrAndSlot, const DeviceTypeRecord* pDevTypeRec)
{
    // Get initialisation bus requests
    std::vector<BusRequestInfo> initBusRequests;
    deviceTypeRecords.getInitBusRequests(addrAndSlot.toCompositeAddrAndSlot(), pDevTypeRec, initBusRequests);

#ifdef DEBUG_DEVICE_IDENT_MGR
    LOG_I(MODULE_PREFIX, "processDeviceInit addr@slotNum %s numInitBusRequests %d", 
                addrAndSlot.toString().c_str(), initBusRequests.size());
#endif

    // Initialise the device
    for (auto& initBusRequest : initBusRequests)
    {
        std::vector<uint8_t> readData;
        BusI2CRequestRec reqRec(initBusRequest);
        _busI2CReqSyncFn(&reqRec, &readData);

        // Check for bar-access time after each request
        if (initBusRequest.getBarAccessForMsAfterSend() > 0)
            delay(initBusRequest.getBarAccessForMsAfterSend());
    }

    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Format device poll responses to JSON
///////////////////////////////////////////////////////////////////////////////////////////////////////////////

String DeviceIdentMgr::deviceStatusToJson(const BusI2CAddrAndSlot& addrAndSlot, bool isOnline, uint16_t deviceTypeIndex, 
                const std::vector<uint8_t>& devicePollResponseData, uint32_t responseSize) const
{
    // Get device type info
    const DeviceTypeRecord* pDevTypeRec = deviceTypeRecords.getDeviceInfo(deviceTypeIndex);
    if (!pDevTypeRec)
        return "";

    // Get the poll response JSON
    return deviceTypeRecords.deviceStatusToJson(addrAndSlot.toCompositeAddrAndSlot(), isOnline, pDevTypeRec, devicePollResponseData);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get JSON for device type info
/// @param address Address of element
/// @return JSON string
String DeviceIdentMgr::getDevTypeInfoJsonByAddr(uint32_t address, bool includePlugAndPlayInfo) const
{
    // Get device type index
    uint16_t deviceTypeIdx = _busStatusMgr.getDeviceTypeIndexByAddr(BusI2CAddrAndSlot::fromCompositeAddrAndSlot(address));
    if (deviceTypeIdx == DeviceStatus::DEVICE_TYPE_INDEX_INVALID)
        return "{}";

    // Get device type info
    return deviceTypeRecords.getDevTypeInfoJsonByTypeIdx(deviceTypeIdx, includePlugAndPlayInfo);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get JSON for device type info
/// @param deviceType Device type
/// @return JSON string
String DeviceIdentMgr::getDevTypeInfoJsonByTypeName(const String& deviceType, bool includePlugAndPlayInfo) const
{
    // Get device type info
    return deviceTypeRecords.getDevTypeInfoJsonByTypeName(deviceType, includePlugAndPlayInfo);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get poll responses json
/// @return bus poll responses json
String DeviceIdentMgr::getPollResponsesJson() const
{
    // Return string
    String jsonStr;

    // Get list of all bus element addresses
    std::vector<uint32_t> addresses;
    _busStatusMgr.getBusElemAddresses(addresses, false);
    for (uint32_t address : addresses)
    {
        // Get bus status for each address
        bool isOnline = false;
        uint16_t deviceTypeIndex = 0;
        std::vector<uint8_t> devicePollResponseData;
        uint32_t responseSize = 0;
        _busStatusMgr.getBusElemPollResponses(address, isOnline, deviceTypeIndex, devicePollResponseData, responseSize, 0);

        // Use device identity manager to convert to JSON
        String jsonData = deviceStatusToJson(BusI2CAddrAndSlot::fromCompositeAddrAndSlot(address), 
                        isOnline, deviceTypeIndex, devicePollResponseData, responseSize);
        if (jsonData.length() > 0)
        {
            jsonStr += (jsonStr.length() == 0 ? "{" : ",") + jsonData;
        }
    }
    return jsonStr.length() == 0 ? "{}" : jsonStr + "}";
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
uint32_t DeviceIdentMgr::getDecodedPollResponses(uint32_t address, 
                void* pStructOut, uint32_t structOutSize, 
                uint16_t maxRecCount, RaftBusDeviceDecodeState& decodeState) const
{
    // Get poll result for each address
    bool isOnline = false;
    uint16_t deviceTypeIndex = 0;
    std::vector<uint8_t> devicePollResponseData;
    uint32_t responseSize = 0;
    _busStatusMgr.getBusElemPollResponses(address, isOnline, deviceTypeIndex, devicePollResponseData, responseSize, 0);

    // Decode the poll response
    return decodePollResponses(deviceTypeIndex, devicePollResponseData.data(), responseSize, 
                pStructOut, structOutSize, 
                maxRecCount, decodeState);
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
    const DeviceTypeRecord* pDevTypeRec = deviceTypeRecords.getDeviceInfo(deviceTypeIndex);
    if (!pDevTypeRec)
        return 0;

    // Check the decode method is present
    if (!pDevTypeRec->pollResultDecodeFn)
        return 0;

    // Decode the poll response
    return pDevTypeRec->pollResultDecodeFn(pPollBuf, pollBufLen, pStructOut, structOutSize, maxRecCount, decodeState);
}
