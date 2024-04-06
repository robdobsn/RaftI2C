/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Device info records
//
// Rob Dobson 2024
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "DevInfoRecords.h"
#include "BusRequestInfo.h"

// #define DEBUG_DEVICE_INFO_RECORDS
// #define DEBUG_DEVICE_INFO_PERFORMANCE

#if defined(DEBUG_DEVICE_INFO_RECORDS) || defined(DEBUG_DEVICE_INFO_PERFORMANCE) 
static const char* MODULE_PREFIX = "DevInfoRecords";
#endif

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Base device information records JSON
///////////////////////////////////////////////////////////////////////////////////////////////////////////////

static const char* baseDevTypeRecordsJson = {
#include "DevIdentJson_generated.h"
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Constructor
///////////////////////////////////////////////////////////////////////////////////////////////////////////////

DevInfoRecords::DevInfoRecords() :
    _baseDevInfo(baseDevTypeRecordsJson, false)
{
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief get device types (strings) for an address
/// @param addrAndSlot i2c address and slot
/// @param devTypesForAddr (out) device types (strings) that match the address
void DevInfoRecords::getDeviceTypesForAddr(RaftI2CAddrAndSlot addrAndSlot, std::vector<String>& devTypesForAddr)
{
#ifdef DEBUG_DEVICE_INFO_PERFORMANCE
    uint64_t startTimeUs = micros();
#endif

    // Clear return array initially
    devTypesForAddr.clear();

    // Get all keys of devTypes
    std::vector<String> devKeys;
    _baseDevInfo.getKeys("devTypes", devKeys);

#ifdef DEBUG_DEVICE_INFO_RECORDS
    LOG_I(MODULE_PREFIX, "getDeviceTypesForAddr %d keys", devKeys.size());
#endif

    // Iterate through all keys and check if address is in range
    for (const auto& devKey : devKeys)
    {
        if (isAddrInRange(devKey, addrAndSlot))
        {
            devTypesForAddr.push_back(devKey);
        }
    }

#ifdef DEBUG_DEVICE_INFO_PERFORMANCE
    uint64_t endTimeUs = micros();
    LOG_I(MODULE_PREFIX, "getDeviceTypesForAddr %s %d keys %d types %lld us", addrAndSlot.toString().c_str(), 
                    devKeys.size(), devTypesForAddr.size(), endTimeUs - startTimeUs);
#endif

#ifdef DEBUG_DEVICE_INFO_RECORDS
    LOG_I(MODULE_PREFIX, "getDeviceTypesForAddr %s %d types", addrAndSlot.toString().c_str(), devTypesForAddr.size());
#endif
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get device info for a device type
/// @param deviceType device type
/// @param devInfo (out) device info
/// @return true if device info found
bool DevInfoRecords::getDeviceInfo(const String& deviceType, RaftJson& devInfo)
{
    // Get the device info
    String devInfoStr = _baseDevInfo.getString(("devTypes/" + deviceType).c_str(), "");
    if (devInfoStr.length() == 0)
        return false;
    devInfo = devInfoStr;
    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get device polling info
/// @param deviceType device type
/// @param pollRequests (out) polling info
void DevInfoRecords::getPollInfo(RaftI2CAddrAndSlot addrAndSlot, const RaftJsonIF& deviceInfoJson, DevicePollingInfo& pollingInfo)
{
    // Clear initially
    pollingInfo.clear();

    // Get polling request records
    String pollRequest = deviceInfoJson.getString("pol/dat", "");
    if (pollRequest.length() == 0)
        return;

    // Extract polling info
    std::vector<RaftJson::NameValuePair> pollWriteReadPairs;
    RaftJson::extractNameValues(pollRequest, "=", "&", ";", pollWriteReadPairs);

    // Create a polling request for each pair
    for (const auto& pollWriteReadPair : pollWriteReadPairs)
    {
        std::vector<uint8_t> writeData;
        if (!extractBufferDataFromHexStr(pollWriteReadPair.name, writeData))
            continue;
        std::vector<uint8_t> readDataMask;
        std::vector<uint8_t> readData;
        if (!extractMaskAndDataFromHexStr(pollWriteReadPair.value, readDataMask, readData, false))
            continue;

        // Create the poll request
        BusI2CRequestRec pollReq(BUS_REQ_TYPE_POLL, 
                addrAndSlot,
                DevicePollingInfo::DEV_IDENT_POLL_CMD_ID, 
                writeData.size(),
                writeData.data(), 
                readDataMask.size(),
                readDataMask.data(), 
                0, 
                NULL, 
                NULL);
        pollingInfo.pollReqs.push_back(pollReq);
    }

    // Get number of polling results to store
    pollingInfo.numPollResultsToStore = deviceInfoJson.getLong("pol/sto", 0);

    // Get polling interval
    pollingInfo.pollIntervalMs = deviceInfoJson.getLong("pol/ms", 0);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get initialisation bus requests
/// @param deviceType device type
/// @param initRequests (out) initialisation requests
void DevInfoRecords::getInitBusRequests(RaftI2CAddrAndSlot addrAndSlot, const RaftJsonIF& deviceInfoJson, std::vector<BusI2CRequestRec>& initRequests)
{
    // Clear initially
    initRequests.clear();

    // Get the initialisation values
    String initValues = deviceInfoJson.getString("ini", "");

    // Extract name:value pairs
    std::vector<RaftJson::NameValuePair> initWriteReadPairs;
    RaftJson::extractNameValues(initValues, "=", "&", ";", initWriteReadPairs);

    // Form the bus requests
    for (const auto& initNameValue : initWriteReadPairs)
    {
        std::vector<uint8_t> writeData;
        if (!extractBufferDataFromHexStr(initNameValue.name, writeData))
            continue;

        // Create a bus request to write the initialisation value
        BusI2CRequestRec reqRec(BUS_REQ_TYPE_FAST_SCAN,
                    addrAndSlot,
                    0, 
                    writeData.size(), 
                    writeData.data(),
                    0,
                    nullptr,
                    0, 
                    nullptr, 
                    this);
        initRequests.push_back(reqRec);
    }
}
                        
///////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Check if device address is in range
/// @param devType device type
/// @param addrAndSlot i2c address and slot
/// @return true if address in range
bool DevInfoRecords::isAddrInRange(const String& devType, RaftI2CAddrAndSlot addrAndSlot) const
{
    // Extract address range
    String addressRange = _baseDevInfo.getString(("devTypes/" + devType + "/ad").c_str(), "");
    if (addressRange.length() == 0)
    {
#ifdef DEBUG_DEVICE_INFO_RECORDS
        LOG_I(MODULE_PREFIX, "isAddrInRange %s no address range", devType.c_str());
#endif
        return false;
    }
    // Convert address range to min and max addresses
    uint32_t minAddr = 0;
    uint32_t maxAddr = 0;
    convertAddressRangeToMinMax(addressRange, minAddr, maxAddr);

#ifdef DEBUG_DEVICE_INFO_RECORDS
    LOG_I(MODULE_PREFIX, "isAddrInRange %s 0x%02x-0x%02x 0x%02x", devType.c_str(), minAddr, maxAddr, addrAndSlot.addr);
#endif

    // Check if address in range
    if (minAddr == 0 && maxAddr == 0)
    {
        return false;
    }
    if (addrAndSlot.addr < minAddr || addrAndSlot.addr > maxAddr)
    {
        return false;
    }
    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Convert address range to min and max addresses
/// @param addressRange a string of the form 0xXX or 0xXX-0xYY
/// @param minAddr lower end of the address range
/// @param maxAddr upper end of the address range (which may be the same as the lower address)
void DevInfoRecords::convertAddressRangeToMinMax(const String& addressRange, uint32_t& minAddr, uint32_t& maxAddr) const
{
    // Check if the address range is a single address
    if (addressRange.length() == 4)
    {
        minAddr = maxAddr = strtoul(addressRange.c_str(), NULL, 16);
        return;
    }

    // Check if the address range is a range
    if (addressRange.length() == 9)
    {
        minAddr = strtoul(addressRange.c_str(), NULL, 16);
        maxAddr = strtoul(addressRange.c_str() + 5, NULL, 16);
        return;
    }

    // Invalid address range
    minAddr = 0;
    maxAddr = 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool DevInfoRecords::extractBufferDataFromHexStr(const String& writeStr, std::vector<uint8_t>& writeData)
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
    return writeData.size() > 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
bool DevInfoRecords::extractMaskAndDataFromHexStr(const String& readStr, std::vector<uint8_t>& readDataMask, 
            std::vector<uint8_t>& readDataCheck, bool maskToZeros)
{
    String readStrLC = readStr;
    readStrLC.toLowerCase();
    // Check if readStr starts with 0b
    if (readStrLC.startsWith("0b"))
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
    return false;
}

/// @brief Get detection records
/// @param deviceType device type
/// @param detectionRecs (out) detection records
void DevInfoRecords::getDetectionRecs(const RaftJsonIF& deviceInfoJson, std::vector<DeviceDetectionRec>& detectionRecs)
{
    // Clear initially
    detectionRecs.clear();

#ifdef DEBUG_DEVICE_INFO_RECORDS
    LOG_I(MODULE_PREFIX, "getDetectionRecs from %s", deviceInfoJson.getJsonDoc());
#endif

    // Get the detection values
    String detectionValues = deviceInfoJson.getString("det", "");

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
        detectionRecs.push_back(detectionRec);
    }

#ifdef DEBUG_DEVICE_INFO_RECORDS
    LOG_I(MODULE_PREFIX, "getDetectionRecs %d recs detectionStr %s", detectionRecs.size(), detectionValues.c_str());
#endif
}