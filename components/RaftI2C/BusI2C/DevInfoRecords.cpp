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
#define DEBUG_DEVICE_INFO_PERFORMANCE

#if defined(DEBUG_DEVICE_INFO_RECORDS) || defined(DEBUG_DEVICE_INFO_PERFORMANCE) 
static const char* MODULE_PREFIX = "DevInfoRecords";
#endif

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Base device information records JSON
///////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "DevTypeRecords_generated.h"

static const uint32_t BASE_DEV_TYPE_ARRAY_SIZE = sizeof(baseDevTypeRecords) / sizeof(BusI2CDevTypeRecord);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Constructor
///////////////////////////////////////////////////////////////////////////////////////////////////////////////

DevInfoRecords::DevInfoRecords()
{
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief get device types (strings) for an address
/// @param addrAndSlot i2c address and slot
/// @returns device type indexes (into generated baseDevTypeRecords array) that match the address
std::vector<uint16_t> DevInfoRecords::getDeviceTypeIdxsForAddr(RaftI2CAddrAndSlot addrAndSlot)
{
#ifdef DEBUG_DEVICE_INFO_PERFORMANCE
    uint64_t startTimeUs = micros();
#endif

    // Check valid
    if ((addrAndSlot.addr < BASE_DEV_INDEX_BY_ARRAY_MIN_ADDR) || (addrAndSlot.addr > BASE_DEV_INDEX_BY_ARRAY_MAX_ADDR))
        return std::vector<uint16_t>();
    uint32_t addrIdx = addrAndSlot.addr - BASE_DEV_INDEX_BY_ARRAY_MIN_ADDR;
    
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
    LOG_I(MODULE_PREFIX, "getDeviceTypeIdxsForAddr %s %d typeIdxs %lld us", addrAndSlot.toString().c_str(), 
                    devTypeIdxsForAddr.size(), endTimeUs - startTimeUs);
#endif

#ifdef DEBUG_DEVICE_INFO_RECORDS
    LOG_I(MODULE_PREFIX, "getDeviceTypeIdxsForAddr %s %d types", addrAndSlot.toString().c_str(), devTypeIdxsForAddr.size());
#endif
    return devTypeIdxsForAddr;
}

// TODO - remove
// ///////////////////////////////////////////////////////////////////////////////////////////////////////////////
// /// @brief Get device info for a device type
// /// @param deviceType device type
// /// @param devInfo (out) device info
// /// @return true if device info found
// bool DevInfoRecords::getDeviceInfo(const String& deviceType, RaftJson& devInfo)
// {
//     // Get the device info
//     String devInfoStr = _baseDevInfo.getString(("devTypes/" + deviceType).c_str(), "");
//     if (devInfoStr.length() == 0)
//         return false;
//     devInfo = devInfoStr;
//     return true;
// }

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get device info for a device type index
/// @param deviceTypeIdx device type index
/// @return pointer to info record if device info found, nullptr if not
const BusI2CDevTypeRecord* DevInfoRecords::getDeviceInfo(uint16_t deviceTypeIdx)
{
    // Check if in range
    if (deviceTypeIdx >= BASE_DEV_TYPE_ARRAY_SIZE)
        return nullptr;
    return &baseDevTypeRecords[deviceTypeIdx];
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get device polling info
/// @param addrAndSlot i2c address and slot
/// @param pDevTypeRec device type record
/// @param pollRequests (out) polling info
void DevInfoRecords::getPollInfo(RaftI2CAddrAndSlot addrAndSlot, const BusI2CDevTypeRecord* pDevTypeRec, DevicePollingInfo& pollingInfo)
{
    // Clear initially
    pollingInfo.clear();
    if (!pDevTypeRec)
        return;

    // Form JSON from string
    RaftJson pollInfo(pDevTypeRec->pollingConfigJson);

    // Get polling request records
    String pollRequest = pollInfo.getString("c", "");
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
    pollingInfo.numPollResultsToStore = pollInfo.getLong("s", 0);

    // Get polling interval
    pollingInfo.pollIntervalMs = pollInfo.getLong("i", 0);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get initialisation bus requests
/// @param deviceType device type
/// @param initRequests (out) initialisation requests
void DevInfoRecords::getInitBusRequests(RaftI2CAddrAndSlot addrAndSlot, const BusI2CDevTypeRecord* pDevTypeRec, std::vector<BusI2CRequestRec>& initRequests)
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

// TODO - remove

// ///////////////////////////////////////////////////////////////////////////////////////////////////////////////
// /// @brief Check if device address is in range
// /// @param devType device type
// /// @param addrAndSlot i2c address and slot
// /// @return true if address in range
// bool DevInfoRecords::isAddrInRange(const String& devType, RaftI2CAddrAndSlot addrAndSlot) const
// {
//     // Extract address range
//     String addressRange = _baseDevInfo.getString(("devTypes/" + devType + "/ad").c_str(), "");
//     if (addressRange.length() == 0)
//     {
// #ifdef DEBUG_DEVICE_INFO_RECORDS
//         LOG_I(MODULE_PREFIX, "isAddrInRange %s no address range", devType.c_str());
// #endif
//         return false;
//     }
//     // Convert address range to min and max addresses
//     uint32_t minAddr = 0;
//     uint32_t maxAddr = 0;
//     convertAddressRangeToMinMax(addressRange, minAddr, maxAddr);

// #ifdef DEBUG_DEVICE_INFO_RECORDS
//     LOG_I(MODULE_PREFIX, "isAddrInRange %s 0x%02x-0x%02x 0x%02x", devType.c_str(), minAddr, maxAddr, addrAndSlot.addr);
// #endif

//     // Check if address in range
//     if (minAddr == 0 && maxAddr == 0)
//     {
//         return false;
//     }
//     if (addrAndSlot.addr < minAddr || addrAndSlot.addr > maxAddr)
//     {
//         return false;
//     }
//     return true;
// }

// ///////////////////////////////////////////////////////////////////////////////////////////////////////////////
// /// @brief Convert address range to min and max addresses
// /// @param addressRange a string of the form 0xXX or 0xXX-0xYY
// /// @param minAddr lower end of the address range
// /// @param maxAddr upper end of the address range (which may be the same as the lower address)
// void DevInfoRecords::convertAddressRangeToMinMax(const String& addressRange, uint32_t& minAddr, uint32_t& maxAddr) const
// {
//     // Check if the address range is a single address
//     if (addressRange.length() == 4)
//     {
//         minAddr = maxAddr = strtoul(addressRange.c_str(), NULL, 16);
//         return;
//     }

//     // Check if the address range is a range
//     if (addressRange.length() == 9)
//     {
//         minAddr = strtoul(addressRange.c_str(), NULL, 16);
//         maxAddr = strtoul(addressRange.c_str() + 5, NULL, 16);
//         return;
//     }

//     // Invalid address range
//     minAddr = 0;
//     maxAddr = 0;
// }

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Extract buffer data from hex string
/// @param writeStr hex string
/// @param writeData (out) buffer data
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
/// @brief Extract mask and data from hex string
/// @param readStr hex string
/// @param readDataMask (out) mask data
/// @param readDataCheck (out) check data
/// @param maskToZeros true if mask should be set to zeros
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
void DevInfoRecords::getDetectionRecs(const BusI2CDevTypeRecord* pDevTypeRec, std::vector<DeviceDetectionRec>& detectionRecs)
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
        detectionRecs.push_back(detectionRec);
    }

#ifdef DEBUG_DEVICE_INFO_RECORDS
    LOG_I(MODULE_PREFIX, "getDetectionRecs %d recs detectionStr %s", detectionRecs.size(), detectionValues.c_str());
#endif
}