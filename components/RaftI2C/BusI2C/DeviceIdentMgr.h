/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Device Ident Manager
//
// Rob Dobson 2024
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "DevInfoRec.h"
#include "DevProcRec.h"
#include "BusI2CRequestRec.h"
#include "DeviceIdent.h"
#include "BusExtenderMgr.h"
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
  
    // Attempt device identification
    const DevInfoRec* attemptDeviceIdent(const RaftI2CAddrAndSlot& addrAndSlot);

    // Communicate with device to check identity
    bool accessDeviceAndCheckReponse(const RaftI2CAddrAndSlot& addrAndSlot, const DevInfoRec& devInfoRec);

    // Process device initialisation
    bool processDeviceInit(const RaftI2CAddrAndSlot& addrAndSlot, const DevInfoRec& devInfoRec);

private:
    // Device indentification enabled
    bool _isEnabled = false;

    // Device info records
    // TODO - remove
    static const std::vector<DevInfoRec> _hwDevInfoRecs;

    // Bus base
    BusExtenderMgr& _busExtenderMgr;

    // Bus i2c request function
    BusI2CReqSyncFn _busI2CReqSyncFn = nullptr;
};
