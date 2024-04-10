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
    void identifyDevice(const RaftI2CAddrAndSlot& addrAndSlot, DeviceStatus& deviceStatus);

    // Communicate with device to check identity
    bool checkDeviceTypeMatch(const RaftI2CAddrAndSlot& addrAndSlot, const BusI2CDevTypeRecord* pDevTypeRec);

    // Process device initialisation
    bool processDeviceInit(const RaftI2CAddrAndSlot& addrAndSlot, const BusI2CDevTypeRecord* pDevTypeRec);

    // Format device poll responses to JSON
    String identPollRespToJson(const RaftI2CAddrAndSlot& addrAndSlot, uint16_t deviceTypeIndex, 
                    const std::vector<uint8_t>& devicePollResponseData, uint32_t responseSize);

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
