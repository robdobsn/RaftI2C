/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Device Info Rec
//
// Rob Dobson 2024
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "RaftUtils.h"
#include "BusI2CConsts.h"
#include "DevicePollingInfo.h"
#include "RaftJson.h"

struct BusI2CDevTypeRecord
{
    const char* deviceType;
    const char* addressRange;
    const char* detectionValues;
    const char* initValues;
    const char* pollingConfigJson;
    const char* devInfoJson;
};

class DevInfoRecords
{
public:
    DevInfoRecords();

    /// @brief Get device type for address
    /// @param addrAndSlot i2c address and slot
    /// @returns device type indexes (into generated baseDevTypeRecords array) that match the address
    std::vector<uint16_t> getDeviceTypeIdxsForAddr(RaftI2CAddrAndSlot addrAndSlot);

    // TODO - remove
    // /// @brief Get device info for a device type
    // /// @param deviceType device type
    // /// @param devInfo (out) device info
    // bool getDeviceInfo(const String& deviceType, RaftJson& devInfo);

    ///////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get device info for a device type index
    /// @param deviceTypeIdx device type index
    /// @return pointer to info record if device info found, nullptr if not
    const BusI2CDevTypeRecord* getDeviceInfo(uint16_t deviceTypeIdx);

    /// @brief Get device polling info
    /// @param addrAndSlot i2c address and slot
    /// @param pDevTypeRec device type record
    /// @param pollRequests (out) polling info
    void getPollInfo(RaftI2CAddrAndSlot addrAndSlot, const BusI2CDevTypeRecord* pDevTypeRec, DevicePollingInfo& pollingInfo);

    // Device detection record
    class DeviceDetectionRec
    {
    public:
        std::vector<uint8_t> writeData;
        std::vector<uint8_t> readDataMask;
        std::vector<uint8_t> readDataCheck;
    };

    /// @brief Get detection records
    /// @param pDevTypeRec device type record
    /// @param detectionRecs (out) detection records
    void getDetectionRecs(const BusI2CDevTypeRecord* pDevTypeRec, std::vector<DeviceDetectionRec>& detectionRecs);

    /// @brief Get initialisation bus requests
    /// @param addrAndSlot i2c address and slot
    /// @param pDevTypeRec device type record
    /// @param initBusRequests (out) initialisation bus requests
    void getInitBusRequests(RaftI2CAddrAndSlot addrAndSlot, const BusI2CDevTypeRecord* pDevTypeRec, std::vector<BusI2CRequestRec>& initBusRequests);

private:

    static bool extractBufferDataFromHexStr(const String& writeStr, std::vector<uint8_t>& writeData);

    static bool extractMaskAndDataFromHexStr(const String& readStr, std::vector<uint8_t>& readDataMask, 
                std::vector<uint8_t>& readDataCheck, bool maskToZeros);

    // TODO - remove
    // bool isAddrInRange(const String& devKey, RaftI2CAddrAndSlot addrAndSlot) const;
    // void convertAddressRangeToMinMax(const String& addressRange, uint32_t& minAddr, uint32_t& maxAddr) const;
};
