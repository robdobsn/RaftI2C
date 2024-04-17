/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Device Ident Manager
//
// Rob Dobson 2024
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "DeviceIdentMgr.h"
#include "BusRequestInfo.h"
#include "Logger.h"

// #define DEBUG_DEVICE_IDENT_MGR

static const char* MODULE_PREFIX = "DeviceIdentMgr";

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Consructor
///////////////////////////////////////////////////////////////////////////////////////////////////////////////

DeviceIdentMgr::DeviceIdentMgr(BusExtenderMgr& busExtenderMgr, BusI2CReqSyncFn busI2CReqSyncFn) :
    _busExtenderMgr(busExtenderMgr),
    _busI2CReqSyncFn(busI2CReqSyncFn)
{
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Setup
///////////////////////////////////////////////////////////////////////////////////////////////////////////////

void DeviceIdentMgr::setup(const RaftJsonIF& config)
{
    // Enabled
    _isEnabled = config.getBool("identEnable", true);

    // Debug
    LOG_I(MODULE_PREFIX, "DeviceIdentMgr setup %s", _isEnabled ? "enabled" : "disabled");
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Attempt device identification
// Note that this is called from within the scanning code so the device should already be
// selected if it is on a bus extender, etc.
///////////////////////////////////////////////////////////////////////////////////////////////////////////////

void DeviceIdentMgr::identifyDevice(const BusI2CAddrAndSlot& addrAndSlot, DeviceStatus& deviceStatus)
{
    // Clear device status
    deviceStatus.clear();

    // Check if enabled
    if (!_isEnabled)
    {
#ifdef DEBUG_DEVICE_IDENT_MGR
        LOG_I(MODULE_PREFIX, "Device identification disabled");
#endif
        return;
    }

    // Check if this address is in the range of any known device
    std::vector<uint16_t> deviceTypesForAddr = _deviceTypeRecords.getDeviceTypeIdxsForAddr(addrAndSlot);
    for (const auto& deviceTypeIdx : deviceTypesForAddr)
    {
#ifdef DEBUG_DEVICE_IDENT_MGR
        LOG_I(MODULE_PREFIX, "identifyDevice deviceType %s addr@slot+1 %s", 
                    deviceType.c_str(), addrAndSlot.toString().c_str());
#endif

        // Get JSON definition for device
        const BusI2CDevTypeRecord* pDevTypeRec = _deviceTypeRecords.getDeviceInfo(deviceTypeIdx);
        if (!pDevTypeRec)
            continue;

        // Check if the detection value(s) match responses from the device
        // Generate a bus request to read the detection value
        if (checkDeviceTypeMatch(addrAndSlot, pDevTypeRec))
        {
#ifdef DEBUG_DEVICE_IDENT_MGR
            LOG_I(MODULE_PREFIX, "Device ident found %s", deviceInfoJson.c_str());
#endif
            // Initialise the device if required
            processDeviceInit(addrAndSlot, pDevTypeRec);

            // Set device type index
            deviceStatus.deviceTypeIndex = deviceTypeIdx;

            // Get polling info
            _deviceTypeRecords.getPollInfo(addrAndSlot, pDevTypeRec, deviceStatus.deviceIdentPolling);

            // Set polling results size
            deviceStatus.dataAggregator.init(deviceStatus.deviceIdentPolling.numPollResultsToStore, 
                    deviceStatus.deviceIdentPolling.pollResultSizeIncTimestamp);

#ifdef DEBUG_HANDLE_BUS_DEVICE_INFO
            LOG_I(MODULE_PREFIX, "setBusElemDevInfo addr@slot+1 %s numPollReqs %d", 
                    addrAndSlot.toString().c_str(),
                    pollRequests.size());
#endif
        }
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Access device and check response
///////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool DeviceIdentMgr::checkDeviceTypeMatch(const BusI2CAddrAndSlot& addrAndSlot, const BusI2CDevTypeRecord* pDevTypeRec)
{
    // Get the detection records
    std::vector<DeviceTypeRecords::DeviceDetectionRec> detectionRecs;
    _deviceTypeRecords.getDetectionRecs(pDevTypeRec, detectionRecs);

    // Check if all values match
    for (const auto& detectionRec : detectionRecs)
    {

#ifdef DEBUG_DEVICE_IDENT_MGR
        String writeStr;
        Raft::getHexStrFromBytes(detectionRec.writeData.data(), detectionRec.writeData.size(), writeStr);
        String readMaskStr;
        Raft::getHexStrFromBytes(detectionRec.readDataMask.data(), detectionRec.readDataMask.size(), readMaskStr);
        String readCheckStr;
        Raft::getHexStrFromBytes(detectionRec.readDataCheck.data(), detectionRec.readDataCheck.size(), readCheckStr);
        LOG_I(MODULE_PREFIX, "checkDeviceTypeMatch addr@slot+1 %s writeData %s readDataMask %s readDataCheck %s", 
                    addrAndSlot.toString().c_str(), 
                    writeStr.c_str(),
                    readMaskStr.c_str(),
                    readCheckStr.c_str());
#endif

        // Create a bus request to read the detection value
        // Create the poll request
        BusI2CRequestRec reqRec(BUS_REQ_TYPE_FAST_SCAN, 
                addrAndSlot,
                0, 
                detectionRec.writeData.size(), 
                detectionRec.writeData.data(),
                detectionRec.readDataCheck.size(),
                0, 
                nullptr, 
                this);
        std::vector<uint8_t> readData;
        RaftI2CCentralIF::AccessResultCode rslt = _busI2CReqSyncFn(&reqRec, &readData);

#ifdef DEBUG_DEVICE_IDENT_MGR
        String writeHexStr;
        Raft::getHexStrFromBytes(detectionRec.writeData.data(), detectionRec.writeData.size(), writeHexStr);
        String readHexStr;
        Raft::getHexStrFromBytes(readData.data(), readData.size(), readHexStr);
        LOG_I(MODULE_PREFIX, "checkDeviceTypeMatch addr@slot+1 %s writeData %s rslt %d readData %s", 
                    addrAndSlot.toString().c_str(), writeHexStr.c_str(), rslt, readHexStr.c_str());
#endif

        // Check ok result
        if (rslt != RaftI2CCentralIF::ACCESS_RESULT_OK)
            return false;

        // Check the read data
        if (readData.size() != detectionRec.readDataCheck.size())
        {
#ifdef DEBUG_DEVICE_IDENT_MGR
            LOG_I(MODULE_PREFIX, "checkDeviceTypeMatch SIZE MISMATCH addr@slot+1 %s", addrAndSlot.toString().c_str());
#endif
            return false;
        }
        for (int i = 0; i < readData.size(); i++)
        {
            uint8_t readDataMaskedVal = readData[i] & detectionRec.readDataMask[i];
#ifdef DEBUG_DEVICE_IDENT_MGR
            String readMaskStr;
            Raft::getHexStrFromBytes(detectionRec.readDataMask.data(), detectionRec.readDataMask.size(), readMaskStr);
            String readCheckStr;
            Raft::getHexStrFromBytes(detectionRec.readDataCheck.data(), detectionRec.readDataCheck.size(), readCheckStr);
            LOG_I(MODULE_PREFIX, "checkDeviceTypeMatch %s idx %d addr@slot+1 %s readMask 0x%s readCheck 0x%s readData 0x%02x readDataMaskedVal 0x%02x readDataMsg 0x%02x readDataCheck 0x%02x",
                        readDataMaskedVal == detectionRec.readDataCheck[i] ? "OK" : "FAIL", i,
                        addrAndSlot.toString().c_str(), readMaskStr, readCheckStr, readData[i], readDataMaskedVal, detectionRec.readDataMask[i], detectionRec.readDataCheck[i]);
#endif
            if (readDataMaskedVal != detectionRec.readDataCheck[i])
                return false;
        }

#ifdef DEBUG_DEVICE_IDENT_MGR
        LOG_I(MODULE_PREFIX, "checkDeviceTypeMatch addr@slot+1 %s OK", addrAndSlot.toString().c_str());
#endif
    }

    // Access the device and check the response
    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Process initialisation of a device
///////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool DeviceIdentMgr::processDeviceInit(const BusI2CAddrAndSlot& addrAndSlot, const BusI2CDevTypeRecord* pDevTypeRec)
{
    // Get initialisation bus requests
    std::vector<BusI2CRequestRec> initBusRequests;
    _deviceTypeRecords.getInitBusRequests(addrAndSlot, pDevTypeRec, initBusRequests);

#ifdef DEBUG_DEVICE_IDENT_MGR
    LOG_I(MODULE_PREFIX, "processDeviceInit addr@slot+1 %s numInitBusRequests %d", 
                addrAndSlot.toString().c_str(), initBusRequests.size());
#endif

    // Initialise the device
    for (auto& initBusRequest : initBusRequests)
    {
        _busI2CReqSyncFn(&initBusRequest, nullptr);
    }

    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Format device poll responses to JSON
///////////////////////////////////////////////////////////////////////////////////////////////////////////////

String DeviceIdentMgr::deviceStatusToJson(const BusI2CAddrAndSlot& addrAndSlot, bool isOnline, uint16_t deviceTypeIndex, 
                const std::vector<uint8_t>& devicePollResponseData, uint32_t responseSize)
{
    // Get device type info
    const BusI2CDevTypeRecord* pDevTypeRec = _deviceTypeRecords.getDeviceInfo(deviceTypeIndex);
    if (!pDevTypeRec)
        return "";

    // Get polling info
    DevicePollingInfo pollingInfo;
    _deviceTypeRecords.getPollInfo(addrAndSlot, pDevTypeRec, pollingInfo);

    // Get the poll response JSON
    return _deviceTypeRecords.deviceStatusToJson(addrAndSlot, isOnline, pDevTypeRec, devicePollResponseData);
}
