/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Device Ident Manager
//
// Rob Dobson 2024
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "RaftBusDevicesIF.h"
#include "DeviceTypeRecord.h"
#include "BusStatusMgr.h"
#include "BusMultiplexers.h"
#include "DeviceStatus.h"
#include "RaftJson.h"
#include <vector>
#include <list>

class DeviceIdentMgr : public RaftBusDevicesIF
{
public:
    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Constructor
    /// @param busStatusMgr bus status manager
    /// @param busMultiplexers bus mulitplexers
    /// @param busI2CReqSyncFn bus i2c synchronous access request function
    DeviceIdentMgr(BusStatusMgr& busStatusMgr, BusMultiplexers& busMultiplexers, BusI2CReqSyncFn busI2CReqSyncFn);

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Setup
    /// @param config configuration
    void setup(const RaftJsonIF& config);

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get list of device addresses attached to the bus
    /// @param pAddrList pointer to array to receive addresses
    /// @param onlyAddressesWithIdentPollResponses true to only return addresses with ident poll responses    
    virtual void getDeviceAddresses(std::vector<uint32_t>& addresses, bool onlyAddressesWithIdentPollResponses) const override final;

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get device type information by address
    /// @param address address of device to get information for
    /// @param includePlugAndPlayInfo true to include plug and play information
    /// @return JSON string
    virtual String getDevTypeInfoJsonByAddr(uint32_t address, bool includePlugAndPlayInfo) const override final;

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get device type information by device type name
    /// @param deviceType device type name
    /// @param includePlugAndPlayInfo true to include plug and play information
    /// @return JSON string
    virtual String getDevTypeInfoJsonByTypeName(const String& deviceType, bool includePlugAndPlayInfo) const override final;

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get poll responses json
    /// @return JSON string
    virtual String getPollResponsesJson() const override final;

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
    virtual uint32_t getDecodedPollResponses(uint32_t address, 
                    void* pStructOut, uint32_t structOutSize, 
                    uint16_t maxRecCount, RaftBusDeviceDecodeState& decodeState) const override final;

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get debug JSON
    /// @return JSON string
    virtual String getDebugJSON(bool includeBraces) const override final;

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Register for device data notifications
    /// @param addrAndSlot address
    /// @param dataChangeCB Callback for data change
    /// @param minTimeBetweenReportsMs Minimum time between reports (ms)
    /// @param pCallbackInfo Callback info (passed to the callback)
    virtual void registerForDeviceData(BusElemAddrType address, RaftDeviceDataChangeCB dataChangeCB, 
                uint32_t minTimeBetweenReportsMs, const void* pCallbackInfo) override final
    {
        _busStatusMgr.registerForDeviceData(address, dataChangeCB, minTimeBetweenReportsMs, pCallbackInfo);
    }

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Identify device
    /// @param addrAndSlot address and slot
    /// @param deviceStatus (out) device status
    void identifyDevice(const BusI2CAddrAndSlot& addrAndSlot, DeviceStatus& deviceStatus);

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Check device type match (communicates with the device to check its type)
    /// @param addrAndSlot address and slot
    /// @param pDevTypeRec device type record
    /// @return true if device type matches
    bool checkDeviceTypeMatch(const BusI2CAddrAndSlot& addrAndSlot, const DeviceTypeRecord* pDevTypeRec);

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Process device initialisation
    /// @param addrAndSlot address and slot
    /// @param pDevTypeRec device type record
    /// @return true if device initialisation was successful
    bool processDeviceInit(const BusI2CAddrAndSlot& addrAndSlot, const DeviceTypeRecord* pDevTypeRec);

private:
    // Device indentification enabled
    bool _isEnabled = false;

    // Bus status
    BusStatusMgr& _busStatusMgr;

    // Bus multiplexers
    BusMultiplexers& _busMultiplexers;

    // Bus i2c request function
    BusI2CReqSyncFn _busI2CReqSyncFn = nullptr;

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Format device status to JSON
    /// @param addrAndSlot address and slot
    /// @param isOnline true if device is online
    /// @param deviceTypeIndex index of device type
    /// @param devicePollResponseData poll response data
    /// @param responseSize size of poll response data
    /// @return JSON string
    String deviceStatusToJson(const BusI2CAddrAndSlot& addrAndSlot, bool isOnline, uint16_t deviceTypeIndex, 
                    const std::vector<uint8_t>& devicePollResponseData, uint32_t responseSize) const;

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Decode one or more poll responses for a device
    /// @param deviceTypeIndex index of device type
    /// @param pPollBuf buffer containing poll responses
    /// @param pollBufLen length of poll response buffer
    /// @param pStructOut pointer to structure (or array of structures) to receive decoded data
    /// @param structOutSize size of structure (in bytes) to receive decoded data
    /// @param maxRecCount maximum number of records to decode
    /// @return number of records decoded
    uint32_t decodePollResponses(uint16_t deviceTypeIndex, 
                    const uint8_t* pPollBuf, uint32_t pollBufLen, 
                    void* pStructOut, uint32_t structOutSize, 
                    uint16_t maxRecCount, RaftBusDeviceDecodeState& decodeState) const;

    // Debug
    static constexpr const char* MODULE_PREFIX = "RaftI2CDevIdentMgr";
};
