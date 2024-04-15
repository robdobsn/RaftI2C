/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Device Ident Manager
//
// Rob Dobson 2024
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "DeviceTypeRecords.h"
#include "BusExtenderMgr.h"
#include "DeviceStatus.h"
#include "RaftJson.h"
#include <vector>
#include <list>

class DeviceIdentMgr
{
public:
    // Constructor
    DeviceIdentMgr(BusExtenderMgr& busExtenderMgr, BusI2CReqSyncFn busI2CReqSyncFn);

    // Setup
    void setup(const RaftJsonIF& config);
  
    // Identify device
    void identifyDevice(const BusI2CAddrAndSlot& addrAndSlot, DeviceStatus& deviceStatus);

    // Communicate with device to check identity
    bool checkDeviceTypeMatch(const BusI2CAddrAndSlot& addrAndSlot, const BusI2CDevTypeRecord* pDevTypeRec);

    // Process device initialisation
    bool processDeviceInit(const BusI2CAddrAndSlot& addrAndSlot, const BusI2CDevTypeRecord* pDevTypeRec);

    // Format device poll responses to JSON
    String identPollRespToJson(const BusI2CAddrAndSlot& addrAndSlot, uint16_t deviceTypeIndex, 
                    const std::vector<uint8_t>& devicePollResponseData, uint32_t responseSize);

    // Get device type info JSON by device type index
    const String getDevTypeInfoJsonByTypeIdx(uint16_t deviceTypeIdx, bool includePlugAndPlayInfo) const
    {
        return _deviceTypeRecords.getDevTypeInfoJsonByTypeIdx(deviceTypeIdx, includePlugAndPlayInfo);
    }

    // Get device type info JSON by device type name
    const String getDevTypeInfoJsonByTypeName(const String& deviceTypeName, bool includePlugAndPlayInfo) const
    {
        return _deviceTypeRecords.getDevTypeInfoJsonByTypeName(deviceTypeName, includePlugAndPlayInfo);
    }

private:
    // Device indentification enabled
    bool _isEnabled = false;

    // Bus base
    BusExtenderMgr& _busExtenderMgr;

    // Bus i2c request function
    BusI2CReqSyncFn _busI2CReqSyncFn = nullptr;

    // Device type records
    DeviceTypeRecords _deviceTypeRecords;

};
