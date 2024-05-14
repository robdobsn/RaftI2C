/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Device Ident Manager
//
// Rob Dobson 2024
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "DeviceIdentMgr.h"
#include "BusRequestInfo.h"
#include "Logger.h"

// #define DEBUG_DEVICE_IDENT_MGR
// #define DEBUG_DEVICE_IDENT_MGR_DETAIL
// #define DEBUG_HANDLE_BUS_DEVICE_INFO

static const char* MODULE_PREFIX = "DeviceIdentMgr";

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Consructor
///////////////////////////////////////////////////////////////////////////////////////////////////////////////

DeviceIdentMgr::DeviceIdentMgr(BusStatusMgr& BusStatusMgr, BusExtenderMgr& busExtenderMgr, BusI2CReqSyncFn busI2CReqSyncFn) :
    _busStatusMgr(BusStatusMgr),
    _busExtenderMgr(busExtenderMgr),
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

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Attempt device identification
// Note that this is called from within the scanning code so the device should already be
// selected if it is on a bus extender, etc.
///////////////////////////////////////////////////////////////////////////////////////////////////////////////

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
    std::vector<uint16_t> deviceTypesForAddr = _deviceTypeRecords.getDeviceTypeIdxsForAddr(addrAndSlot);
    for (const auto& deviceTypeIdx : deviceTypesForAddr)
    {
        // Get JSON definition for device
        const BusI2CDevTypeRecord* pDevTypeRec = _deviceTypeRecords.getDeviceInfo(deviceTypeIdx);
        if (!pDevTypeRec)
            continue;

#ifdef DEBUG_DEVICE_IDENT_MGR
        LOG_I(MODULE_PREFIX, "identifyDevice potential deviceType %s addr@slot+1 %s", 
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
            _deviceTypeRecords.getPollInfo(addrAndSlot, pDevTypeRec, deviceStatus.deviceIdentPolling);

            // Set polling results size
            deviceStatus.dataAggregator.init(deviceStatus.deviceIdentPolling.numPollResultsToStore, 
                    deviceStatus.deviceIdentPolling.pollResultSizeIncTimestamp);

#ifdef DEBUG_HANDLE_BUS_DEVICE_INFO
            LOG_I(MODULE_PREFIX, "setBusElemDevInfo addr@slot+1 %s numPollResToStore %d pollResSizeIncTimestamp %d", 
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

bool DeviceIdentMgr::checkDeviceTypeMatch(const BusI2CAddrAndSlot& addrAndSlot, const BusI2CDevTypeRecord* pDevTypeRec)
{
    // Get the detection records
    std::vector<DeviceTypeRecords::DeviceDetectionRec> detectionRecs;
    _deviceTypeRecords.getDetectionRecs(pDevTypeRec, detectionRecs);

    // Check if all values match
    for (const auto& detectionRec : detectionRecs)
    {

#ifdef DEBUG_DEVICE_IDENT_MGR
        String writeStr;
        Raft::getHexStrFromBytes(detectionRec.writeData.data(), detectionRec.writeData.size(), writeStr);
        String readMaskStr;
        Raft::getHexStrFromBytes(detectionRec.readDataMask.data(), detectionRec.readDataMask.size(), readMaskStr);
        String readCheckStr;
        Raft::getHexStrFromBytes(detectionRec.readDataCheck.data(), detectionRec.readDataCheck.size(), readCheckStr);
        LOG_I(MODULE_PREFIX, "checkDeviceTypeMatch addr@slot+1 %s writeData %s readDataMask %s readDataCheck %s readSize %d", 
                    addrAndSlot.toString().c_str(), 
                    writeStr.c_str(),
                    readMaskStr.c_str(),
                    readCheckStr.c_str(),
                    detectionRec.readDataCheck.size());
#endif

        // Create a bus request to read the detection value
        // Create the poll request
        BusI2CRequestRec reqRec(BUS_REQ_TYPE_FAST_SCAN, 
                addrAndSlot,
                0, 
                detectionRec.writeData.size(), 
                detectionRec.writeData.data(),
                detectionRec.readDataCheck.size(),
                0, 
                nullptr, 
                this);
        std::vector<uint8_t> readData;
        RaftI2CCentralIF::AccessResultCode rslt = _busI2CReqSyncFn(&reqRec, &readData);

#ifdef DEBUG_DEVICE_IDENT_MGR
        String writeHexStr;
        Raft::getHexStrFromBytes(detectionRec.writeData.data(), detectionRec.writeData.size(), writeHexStr);
        String readHexStr;
        Raft::getHexStrFromBytes(readData.data(), readData.size(), readHexStr);
        LOG_I(MODULE_PREFIX, "checkDeviceTypeMatch addr@slot+1 %s writeData %s rslt %d readData %s", 
                    addrAndSlot.toString().c_str(), writeHexStr.c_str(), rslt, readHexStr.c_str());
#endif

        // Check ok result
        if (rslt != RaftI2CCentralIF::ACCESS_RESULT_OK)
            return false;

        // Check the read data
        if (readData.size() != detectionRec.readDataCheck.size())
        {
#ifdef DEBUG_DEVICE_IDENT_MGR
            LOG_I(MODULE_PREFIX, "checkDeviceTypeMatch SIZE MISMATCH addr@slot+1 %s", addrAndSlot.toString().c_str());
#endif
            return false;
        }
        for (int i = 0; i < readData.size(); i++)
        {
            uint8_t readDataMaskedVal = readData[i] & detectionRec.readDataMask[i];
#ifdef DEBUG_DEVICE_IDENT_MGR
            String readMaskStr;
            Raft::getHexStrFromBytes(detectionRec.readDataMask.data(), detectionRec.readDataMask.size(), readMaskStr);
            String readCheckStr;
            Raft::getHexStrFromBytes(detectionRec.readDataCheck.data(), detectionRec.readDataCheck.size(), readCheckStr);
            LOG_I(MODULE_PREFIX, "checkDeviceTypeMatch %s idx %d addr@slot+1 %s readMask 0x%s readCheck 0x%s readData 0x%02x readDataMaskedVal 0x%02x readDataMsg 0x%02x readDataCheck 0x%02x",
                        readDataMaskedVal == detectionRec.readDataCheck[i] ? "OK" : "FAIL", i,
                        addrAndSlot.toString().c_str(), readMaskStr, readCheckStr, readData[i], readDataMaskedVal, detectionRec.readDataMask[i], detectionRec.readDataCheck[i]);
#endif
            if (readDataMaskedVal != detectionRec.readDataCheck[i])
                return false;
        }

#ifdef DEBUG_DEVICE_IDENT_MGR
        LOG_I(MODULE_PREFIX, "checkDeviceTypeMatch addr@slot+1 %s OK", addrAndSlot.toString().c_str());
#endif
    }

    // Access the device and check the response
    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Process initialisation of a device
///////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool DeviceIdentMgr::processDeviceInit(const BusI2CAddrAndSlot& addrAndSlot, const BusI2CDevTypeRecord* pDevTypeRec)
{
    // Get initialisation bus requests
    std::vector<BusI2CRequestRec> initBusRequests;
    _deviceTypeRecords.getInitBusRequests(addrAndSlot, pDevTypeRec, initBusRequests);

#ifdef DEBUG_DEVICE_IDENT_MGR
    LOG_I(MODULE_PREFIX, "processDeviceInit addr@slot+1 %s numInitBusRequests %d", 
                addrAndSlot.toString().c_str(), initBusRequests.size());
#endif

    // Initialise the device
    for (auto& initBusRequest : initBusRequests)
    {
        _busI2CReqSyncFn(&initBusRequest, nullptr);
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
    const BusI2CDevTypeRecord* pDevTypeRec = _deviceTypeRecords.getDeviceInfo(deviceTypeIndex);
    if (!pDevTypeRec)
        return "";

    // Get polling info
    DevicePollingInfo pollingInfo;
    _deviceTypeRecords.getPollInfo(addrAndSlot, pDevTypeRec, pollingInfo);

    // Get the poll response JSON
    return _deviceTypeRecords.deviceStatusToJson(addrAndSlot, isOnline, pDevTypeRec, devicePollResponseData);
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
    return _deviceTypeRecords.getDevTypeInfoJsonByTypeIdx(deviceTypeIdx, includePlugAndPlayInfo);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get JSON for device type info
/// @param deviceType Device type
/// @return JSON string
String DeviceIdentMgr::getDevTypeInfoJsonByTypeName(const String& deviceType, bool includePlugAndPlayInfo) const
{
    // Get device type info
    return _deviceTypeRecords.getDevTypeInfoJsonByTypeName(deviceType, includePlugAndPlayInfo);
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
            uint16_t maxRecCount, BusDeviceDecodeState& decodeState)
{
    // Get device type info
    const BusI2CDevTypeRecord* pDevTypeRec = _deviceTypeRecords.getDeviceInfo(deviceTypeIndex);
    if (!pDevTypeRec)
        return 0;

    // Check the decode method is present
    if (!pDevTypeRec->pollResultDecodeFn)
        return 0;

    // Decode the poll response
    return pDevTypeRec->pollResultDecodeFn(pPollBuf, pollBufLen, pStructOut, structOutSize, maxRecCount, decodeState);
}
