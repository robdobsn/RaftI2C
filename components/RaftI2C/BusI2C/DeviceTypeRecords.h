/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Device Type Records
//
// Rob Dobson 2024
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "RaftUtils.h"
#include "BusI2CConsts.h"
#include "BusI2CDevTypeRecord.h"
#include "DevicePollingInfo.h"
#include "RaftJson.h"

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @class DeviceTypeRecords
/// @brief Device type records
class DeviceTypeRecords
{
public:
    DeviceTypeRecords();

    ///////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get device type for address
    /// @param addrAndSlot i2c address and slot
    /// @returns device type indexes that match the address
    std::vector<uint16_t> getDeviceTypeIdxsForAddr(BusI2CAddrAndSlot addrAndSlot) const;

    ///////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get device type record for a device type index
    /// @param deviceTypeIdx device type index
    /// @return pointer to info record if device type found, nullptr if not
    const BusI2CDevTypeRecord* getDeviceInfo(uint16_t deviceTypeIdx) const;

    //////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get device type record for a device type name
    /// @param deviceType device type name
    /// @return pointer to info record if device type found, nullptr if not
    const BusI2CDevTypeRecord* getDeviceInfo(const String& deviceType) const;

    //////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get device polling info
    /// @param addrAndSlot i2c address and slot
    /// @param pDevTypeRec device type record
    /// @param pollRequests (out) polling info
    void getPollInfo(BusI2CAddrAndSlot addrAndSlot, const BusI2CDevTypeRecord* pDevTypeRec, DevicePollingInfo& pollingInfo) const;

    ///////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get device type info JSON by device type index
    /// @param deviceTypeIdx device type index
    /// @param includePlugAndPlayInfo include plug and play info
    /// @return JSON string
    String getDevTypeInfoJsonByTypeIdx(uint16_t deviceTypeIdx, bool includePlugAndPlayInfo) const;

    ///////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get device type info JSON by device type name
    /// @param deviceType device type name
    /// @param includePlugAndPlayInfo include plug and play info
    /// @return JSON string
    String getDevTypeInfoJsonByTypeName(const String& deviceType, bool includePlugAndPlayInfo) const;

    // Device detection record
    class DeviceDetectionRec
    {
    public:
        std::vector<uint8_t> writeData;
        std::vector<uint8_t> readDataMask;
        std::vector<uint8_t> readDataCheck;
    };

    ///////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get detection records
    /// @param pDevTypeRec device type record
    /// @param detectionRecs (out) detection records
    void getDetectionRecs(const BusI2CDevTypeRecord* pDevTypeRec, std::vector<DeviceDetectionRec>& detectionRecs);

    ///////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get initialisation bus requests
    /// @param addrAndSlot i2c address and slot
    /// @param pDevTypeRec device type record
    /// @param initBusRequests (out) initialisation bus requests
    void getInitBusRequests(BusI2CAddrAndSlot addrAndSlot, const BusI2CDevTypeRecord* pDevTypeRec, std::vector<BusI2CRequestRec>& initBusRequests);

    ///////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Convert poll response to JSON
    /// @param addrAndSlot i2c address and slot
    /// @param isOnline true if device is online
    /// @param pDevTypeRec pointer to device type record
    /// @param devicePollResponseData device poll response data
    String deviceStatusToJson(BusI2CAddrAndSlot addrAndSlot, bool isOnline, const BusI2CDevTypeRecord* pDevTypeRec, 
            const std::vector<uint8_t>& devicePollResponseData) const;

    ///////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get scan priority lists
    /// @param priorityLists (out) priority lists
    static void getScanPriorityLists(std::vector<std::vector<RaftI2CAddrType>>& priorityLists);

private:
    // Helpers
    static bool extractBufferDataFromHexStr(const String& writeStr, std::vector<uint8_t>& writeData);
    static bool extractMaskAndDataFromHexStr(const String& readStr, std::vector<uint8_t>& readDataMask, 
                std::vector<uint8_t>& readDataCheck, bool maskToZeros);
    static uint32_t extractReadDataSize(const String& readStr);                
};
