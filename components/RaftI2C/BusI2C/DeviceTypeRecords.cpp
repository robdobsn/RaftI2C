/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Device type records
//
// Rob Dobson 2024
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "DeviceTypeRecords.h"
#include "BusRequestInfo.h"

#define DEBUG_DEVICE_INFO_RECORDS
#define DEBUG_POLL_REQUEST_REQS
#define DEBUG_DEVICE_INFO_PERFORMANCE
#define DEBUG_DEVICE_INIT_REQS

#if defined(DEBUG_DEVICE_INFO_RECORDS) || defined(DEBUG_DEVICE_INFO_PERFORMANCE) 
static const char* MODULE_PREFIX = "DeviceTypeRecords";
#endif

// Global object
DeviceTypeRecords deviceTypeRecords;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Generated device type records
#include "DeviceTypeRecords_generated.h"
static const uint32_t BASE_DEV_TYPE_ARRAY_SIZE = sizeof(baseDevTypeRecords) / sizeof(DeviceTypeRecord);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Constructor
DeviceTypeRecords::DeviceTypeRecords()
{
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief get device types (strings) for an address
/// @param addr address
/// @returns device type indexes (into generated baseDevTypeRecords array) that match the address
std::vector<uint16_t> DeviceTypeRecords::getDeviceTypeIdxsForAddr(BusElemAddrType addr) const
{
#ifdef DEBUG_DEVICE_INFO_PERFORMANCE
    uint64_t startTimeUs = micros();
#endif

    // Check valid
    if ((addr < BASE_DEV_INDEX_BY_ARRAY_MIN_ADDR) || (addr > BASE_DEV_INDEX_BY_ARRAY_MAX_ADDR))
        return std::vector<uint16_t>();
    uint32_t addrIdx = addr - BASE_DEV_INDEX_BY_ARRAY_MIN_ADDR;
    
    // Get number of types for this addr - if none then return
    uint32_t numTypes = baseDevTypeCountByAddr[addrIdx];
    if (numTypes == 0)
        return std::vector<uint16_t>();

    // Iterate the types for this address
    std::vector<uint16_t> devTypeIdxsForAddr;
    for (uint32_t i = 0; i < numTypes; i++)
    {
        devTypeIdxsForAddr.push_back(baseDevTypeIndexByAddr[addrIdx][i]);
    }
    
#ifdef DEBUG_DEVICE_INFO_PERFORMANCE
    uint64_t endTimeUs = micros();
    LOG_I(MODULE_PREFIX, "getDeviceTypeIdxsForAddr %s %d typeIdxs %lld us", BusI2CAddrAndSlot::fromCompositeAddrAndSlot(addr).toString().c_str(), 
                    devTypeIdxsForAddr.size(), endTimeUs - startTimeUs);
#endif

#ifdef DEBUG_DEVICE_INFO_RECORDS
    LOG_I(MODULE_PREFIX, "getDeviceTypeIdxsForAddr %s %d types", BusI2CAddrAndSlot::fromCompositeAddrAndSlot(addr).toString().c_str(), devTypeIdxsForAddr.size());
#endif
    return devTypeIdxsForAddr;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get device type for a device type index
/// @param deviceTypeIdx device type index
/// @return pointer to device type record if device type found, nullptr if not
const DeviceTypeRecord* DeviceTypeRecords::getDeviceInfo(uint16_t deviceTypeIdx) const
{
    // Check if in range
    if (deviceTypeIdx >= BASE_DEV_TYPE_ARRAY_SIZE)
        return nullptr;
    return &baseDevTypeRecords[deviceTypeIdx];
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get device type for a device type name
/// @param deviceTypeName device type name
/// @return pointer to device type record if device type found, nullptr if not
const DeviceTypeRecord* DeviceTypeRecords::getDeviceInfo(const String& deviceTypeName) const
{
    // Iterate the device types
    for (uint16_t i = 0; i < BASE_DEV_TYPE_ARRAY_SIZE; i++)
    {
        if (deviceTypeName == baseDevTypeRecords[i].deviceType)
            return &baseDevTypeRecords[i];
    }
    return nullptr;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get device polling info
/// @param addrAndSlot i2c address and slot
/// @param pDevTypeRec device type record
/// @param pollRequests (out) polling info
void DeviceTypeRecords::getPollInfo(BusElemAddrType addr, const DeviceTypeRecord* pDevTypeRec, DevicePollingInfo& pollingInfo) const
{
    // Clear initially
    pollingInfo.clear();
    if (!pDevTypeRec)
        return;

    // Form JSON from string
    RaftJson pollInfo(pDevTypeRec->pollInfo);

    // Get polling request records
    String pollRequest = pollInfo.getString("c", "");
    if (pollRequest.length() == 0)
        return;

    // Extract polling info
    std::vector<RaftJson::NameValuePair> pollWriteReadPairs;
    RaftJson::extractNameValues(pollRequest, "=", "&", ";", pollWriteReadPairs);

    // Create a polling request for each pair
    uint16_t pollResultDataSize = 0;
    for (const auto& pollWriteReadPair : pollWriteReadPairs)
    {
        std::vector<uint8_t> writeData;
        if (!extractBufferDataFromHexStr(pollWriteReadPair.name, writeData))
        {
#ifdef DEBUG_POLL_REQUEST_REQS
            LOG_I(MODULE_PREFIX, "getPollInfo FAIL extractBufferDataFromHexStr %s (value %s)", 
                        pollWriteReadPair.name.c_str(), pollWriteReadPair.value.c_str());
#endif
            continue;
        }
        std::vector<uint8_t> readDataMask;
        std::vector<uint8_t> readData;
        if (!extractMaskAndDataFromHexStr(pollWriteReadPair.value, readDataMask, readData, false))
        {
#ifdef DEBUG_POLL_REQUEST_REQS
            LOG_I(MODULE_PREFIX, "getPollInfo FAIL extractMaskAndDataFromHexStr %s (name %s)", 
                        pollWriteReadPair.value.c_str(), pollWriteReadPair.name.c_str());
#endif
            continue;
        }

#ifdef DEBUG_POLL_REQUEST_REQS
        String writeDataStr;
        Raft::getHexStrFromBytes(writeData.data(), writeData.size(), writeDataStr);
        String readDataMaskStr;
        Raft::getHexStrFromBytes(readDataMask.data(), readDataMask.size(), readDataMaskStr);
        String readDataStr;
        Raft::getHexStrFromBytes(readData.data(), readData.size(), readDataStr);
        LOG_I(MODULE_PREFIX, "getPollInfo addr@slot+1 %s writeData %s readDataMask %s readData %s", 
                    BusI2CAddrAndSlot::fromCompositeAddrAndSlot(addr).toString().c_str(), writeDataStr.c_str(), readDataMaskStr.c_str(), readDataStr.c_str());
#endif

        // Create the poll request
        BusRequestInfo pollReq(BUS_REQ_TYPE_POLL, 
                addr,
                DevicePollingInfo::DEV_IDENT_POLL_CMD_ID, 
                writeData.size(),
                writeData.data(), 
                readDataMask.size(),
                0, 
                NULL, 
                NULL);
        pollingInfo.pollReqs.push_back(pollReq);

        // Keep track of poll result size
        pollResultDataSize += readData.size();
    }

    // Get number of polling results to store
    pollingInfo.numPollResultsToStore = pollInfo.getLong("s", 0);

    // Get polling interval
    pollingInfo.pollIntervalUs = pollInfo.getLong("i", 0) * 1000;

    // Set the poll result size
    pollingInfo.pollResultSizeIncTimestamp = pollResultDataSize + DevicePollingInfo::POLL_RESULT_TIMESTAMP_SIZE;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get initialisation bus requests
/// @param deviceType device type
/// @param initRequests (out) initialisation requests
void DeviceTypeRecords::getInitBusRequests(BusI2CAddrAndSlot addrAndSlot, const DeviceTypeRecord* pDevTypeRec, std::vector<BusI2CRequestRec>& initRequests)
{
    // Clear initially
    initRequests.clear();
    if (!pDevTypeRec)
        return;
        
    // Get the initialisation values
    String initValues = pDevTypeRec->initValues;

    // Extract name:value pairs
    std::vector<RaftJson::NameValuePair> initWriteReadPairs;
    RaftJson::extractNameValues(initValues, "=", "&", ";", initWriteReadPairs);

    // Form the bus requests
    for (const auto& initNameValue : initWriteReadPairs)
    {
        std::vector<uint8_t> writeData;
        if (!extractBufferDataFromHexStr(initNameValue.name, writeData))
            continue;
        uint32_t numReadDataBytes = extractReadDataSize(initNameValue.value);
        uint32_t barAccessForMs = extractBarAccessMs(initNameValue.value);

        // Create a bus request to write the initialisation value
        BusI2CRequestRec reqRec(BUS_REQ_TYPE_FAST_SCAN,
                    addrAndSlot,
                    0, 
                    writeData.size(), 
                    writeData.data(),
                    numReadDataBytes,
                    barAccessForMs, 
                    nullptr, 
                    this);
        initRequests.push_back(reqRec);

        // Debug
#ifdef DEBUG_DEVICE_INIT_REQS
        String writeDataStr;
        Raft::getHexStrFromBytes(writeData.data(), writeData.size(), writeDataStr);
        LOG_I(MODULE_PREFIX, "getInitBusRequests addr@slot+1 %s devType %s writeData %s readDataSize %d", 
                    addrAndSlot.toString().c_str(), pDevTypeRec->deviceType, 
                    writeDataStr.c_str(), numReadDataBytes);
#endif
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Extract buffer data from hex string
/// @param writeStr hex string
/// @param writeData (out) buffer data
bool DeviceTypeRecords::extractBufferDataFromHexStr(const String& writeStr, std::vector<uint8_t>& writeData)
{        
    const char* pStr = writeStr.c_str();
    uint32_t inStrLen = writeStr.length();
    if (writeStr.startsWith("0x") || writeStr.startsWith("0X"))
    {
        pStr = writeStr.c_str() + 2;
        inStrLen -= 2;
    }
    // Compute length
    uint32_t writeStrLen = (inStrLen + 1) / 2;
    // Extract the write data
    writeData.resize(writeStrLen);
    Raft::getBytesFromHexStr(pStr, writeData.data(), writeData.size());
    return writeData.size() > 0 || (writeStr.length() == 0);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Extract mask and data from hex string
/// @param readStr hex string
/// @param readDataMask (out) mask data
/// @param readDataCheck (out) check data
/// @param maskToZeros true if mask should be set to zeros
bool DeviceTypeRecords::extractMaskAndDataFromHexStr(const String& readStr, std::vector<uint8_t>& readDataMask, 
            std::vector<uint8_t>& readDataCheck, bool maskToZeros)
{
    readDataCheck.clear();
    readDataMask.clear();
    String readStrLC = readStr;
    readStrLC.toLowerCase();
    // Check for readStr starts with rNNNN (for read NNNN bytes)
    if (readStrLC.startsWith("r"))
    {
        // Compute length
        uint32_t lenBytes = strtol(readStrLC.c_str() + 1, NULL, 10);
        // Extract the read data
        readDataMask.resize(lenBytes);
        readDataCheck.resize(lenBytes);
        for (int i = 1; i < readStrLC.length(); i++)
        {
            readDataMask[i - 1] = maskToZeros ? 0xff : 0;
            readDataCheck[i - 1] = 0;
        }
        return true;
    }

    // Check if readStr starts with 0x
    else if (readStrLC.startsWith("0x"))
    {
        // Compute length
        uint32_t lenBytes = (readStrLC.length() - 2 + 1) / 2;
        // Extract the read data
        readDataMask.resize(lenBytes);
        readDataCheck.resize(lenBytes);
        Raft::getBytesFromHexStr(readStrLC.c_str() + 2, readDataCheck.data(), readDataCheck.size());
        for (int i = 0; i < readDataMask.size(); i++)
        {
            readDataMask[i] = maskToZeros ? 0xff : 0;
        }
        return true;
    }

    // Check if readStr starts with 0b 
    else if (readStrLC.startsWith("0b"))
    {
        // Compute length
        uint32_t lenBits = readStrLC.length() - 2;
        uint32_t lenBytes = (lenBits + 7) / 8;
        // Extract the read data
        readDataMask.resize(lenBytes);
        readDataCheck.resize(lenBytes);
        uint32_t bitMask = 0x80;
        uint32_t byteIdx = 0;
        for (int i = 2; i < readStrLC.length(); i++)
        {
            if (bitMask == 0x80)
            {
                readDataMask[byteIdx] = maskToZeros ? 0xff : 0;
                readDataCheck[byteIdx] = 0;
            }
            if (readStrLC[i] == 'x')
            {
                if (maskToZeros)
                    readDataMask[byteIdx] &= ~bitMask;
                else
                    readDataMask[byteIdx] |= bitMask;
            }
            else if (readStrLC[i] == '1')
            {
                readDataCheck[byteIdx] |= bitMask;
            }
            bitMask >>= 1;
            if (bitMask == 0)
            {
                bitMask = 0x80;
                byteIdx++;
            }
        }
        return true;
    }
    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Extract read data size
/// @param readStr hex string
/// @return number of bytes to read
uint32_t DeviceTypeRecords::extractReadDataSize(const String& readStr)
{
    String readStrLC = readStr;
    readStrLC.toLowerCase();
    // Check for readStr starts with rNNNN (for read NNNN bytes)
    if (readStrLC.startsWith("r"))
    {
        return strtol(readStrLC.c_str() + 1, NULL, 10);
    }
    // Check if readStr starts with 0b 
    if (readStrLC.startsWith("0b"))
    {
        return (readStrLC.length() - 2 + 7) / 8;
    }
    return 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Extract bar access time
/// @param readStr hex string
/// @return bar access time in ms
uint32_t DeviceTypeRecords::extractBarAccessMs(const String& readStr)
{
    String readStrLC = readStr;
    readStrLC.toLowerCase();
    // Check for readStr having pNN in it
    int pauseIdx = readStrLC.indexOf("p");
    if (pauseIdx < 0)
    {
        return 0;
    }
    return strtol(readStrLC.c_str() + pauseIdx + 1, NULL, 10);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get detection records
/// @param deviceType device type
/// @param detectionRecs (out) detection records
void DeviceTypeRecords::getDetectionRecs(const DeviceTypeRecord* pDevTypeRec, std::vector<DeviceDetectionRec>& detectionRecs)
{
    // Clear initially
    detectionRecs.clear();
    if (!pDevTypeRec)
        return;

    // Get the detection values
    String detectionValues = pDevTypeRec->detectionValues;

    // Extract name:value pairs
    std::vector<RaftJson::NameValuePair> detectionWriteReadPairs;
    RaftJson::extractNameValues(detectionValues, "=", "&", ";", detectionWriteReadPairs);

    // Convert to detection records
    for (const auto& detectionNameValue : detectionWriteReadPairs)
    {
        DeviceDetectionRec detectionRec;
        if (!extractBufferDataFromHexStr(detectionNameValue.name, detectionRec.writeData))
            continue;
        if (!extractMaskAndDataFromHexStr(detectionNameValue.value, detectionRec.readDataMask, detectionRec.readDataCheck, true))
            continue;
        detectionRec.pauseAfterSendMs = extractBarAccessMs(detectionNameValue.value);
        detectionRecs.push_back(detectionRec);
    }

#ifdef DEBUG_DEVICE_INFO_RECORDS
    LOG_I(MODULE_PREFIX, "getDetectionRecs %d recs detectionStr %s", detectionRecs.size(), detectionValues.c_str());
#endif
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Convert poll response to JSON
/// @param addrAndSlot i2c address and slot
/// @param isOnline true if device is online
/// @param pDevTypeRec pointer to device type record
/// @param devicePollResponseData device poll response data
String DeviceTypeRecords::deviceStatusToJson(BusI2CAddrAndSlot addrAndSlot, bool isOnline, const DeviceTypeRecord* pDevTypeRec, 
        const std::vector<uint8_t>& devicePollResponseData) const
{
    // Device type name
    String devTypeName = pDevTypeRec ? pDevTypeRec->deviceType : "";
    // Form a hex buffer
    String hexOut;
    Raft::getHexStrFromBytes(devicePollResponseData.data(), devicePollResponseData.size(), hexOut);
    return "\"" + addrAndSlot.toString() + "\":{\"x\":\"" + hexOut + "\",\"_o\":" + String(isOnline ? "1" : "0") + ",\"_t\":\"" + devTypeName + "\"}";
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get device type info JSON by device type index
/// @param deviceTypeIdx device type index
/// @param includePlugAndPlayInfo include plug and play info
/// @return JSON string
String DeviceTypeRecords::getDevTypeInfoJsonByTypeIdx(uint16_t deviceTypeIdx, bool includePlugAndPlayInfo) const
{
    // Check if in range
    if (deviceTypeIdx >= BASE_DEV_TYPE_ARRAY_SIZE)
        return "{}";

    // Get the device type record
    const DeviceTypeRecord* pDevTypeRec = &baseDevTypeRecords[deviceTypeIdx];
    if (!pDevTypeRec)
        return "{}";

    // Get JSON for device type
    return pDevTypeRec->getJson(includePlugAndPlayInfo);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get device type info JSON by device type name
/// @param deviceTypeName device type name
/// @param includePlugAndPlayInfo include plug and play info
/// @return JSON string
String DeviceTypeRecords::getDevTypeInfoJsonByTypeName(const String& deviceTypeName, bool includePlugAndPlayInfo) const
{
    // Get the device type info
    const DeviceTypeRecord* pDevTypeRec = getDeviceInfo(deviceTypeName);

    // Get JSON for device type
    return pDevTypeRec ? pDevTypeRec->getJson(includePlugAndPlayInfo) : "{}";
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get scan priority lists
/// @param priorityLists (out) priority lists
void DeviceTypeRecords::getScanPriorityLists(std::vector<std::vector<RaftI2CAddrType>>& priorityLists)
{
    // Clear initially
    priorityLists.clear();

    // Resize list
    priorityLists.resize(numScanPriorityLists);
    for (int i = 0; i < numScanPriorityLists; i++)
    {
        for (int j = 0; j < scanPriorityListLengths[i]; j++)
        {
            priorityLists[i].push_back(scanPriorityLists[i][j]);
        }
    }
}
