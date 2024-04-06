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

class DevInfoRecords
{
public:
    DevInfoRecords();

    /// @brief Get device type for address
    /// @param addrAndSlot i2c address and slot
    /// @param devTypesForAddr (out) device types (strings) that match the address
    void getDeviceTypesForAddr(RaftI2CAddrAndSlot addrAndSlot, std::vector<String>& devTypesForAddr);

    /// @brief Get device info for a device type
    /// @param deviceType device type
    /// @param devInfo (out) device info
    bool getDeviceInfo(const String& deviceType, RaftJson& devInfo);

    /// @brief Get device polling info
    /// @param deviceType device type
    /// @param pollRequests (out) polling info
    void getPollInfo(RaftI2CAddrAndSlot addrAndSlot, const RaftJsonIF& deviceInfoJson, DevicePollingInfo& pollingInfo);

    // Device detection record
    class DeviceDetectionRec
    {
    public:
        std::vector<uint8_t> writeData;
        std::vector<uint8_t> readDataMask;
        std::vector<uint8_t> readDataCheck;
    };

    /// @brief Get detection records
    /// @param deviceType device type
    /// @param detectionRecs (out) detection records
    void getDetectionRecs(const RaftJsonIF& deviceInfoJson, std::vector<DeviceDetectionRec>& detectionRecs);

    /// @brief Get initialisation bus requests
    /// @param deviceType device type
    /// @param initBusRequests (out) initialisation bus requests
    void getInitBusRequests(RaftI2CAddrAndSlot addrAndSlot, const RaftJsonIF& deviceInfoJson, std::vector<BusI2CRequestRec>& initBusRequests);

private:

    bool isAddrInRange(const String& devKey, RaftI2CAddrAndSlot addrAndSlot) const;

    static bool extractBufferDataFromHexStr(const String& writeStr, std::vector<uint8_t>& writeData);

    static bool extractMaskAndDataFromHexStr(const String& readStr, std::vector<uint8_t>& readDataMask, 
                std::vector<uint8_t>& readDataCheck, bool maskToZeros);

private:

    void convertAddressRangeToMinMax(const String& addressRange, uint32_t& minAddr, uint32_t& maxAddr) const;

    // JSON for device information records
    RaftJson _baseDevInfo;
};
