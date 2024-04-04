/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Device Ident Manager
//
// Rob Dobson 2024
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "DeviceIdentMgr.h"
#include "Logger.h"

// #define DEBUG_DEVICE_IDENT_MGR

static const char* MODULE_PREFIX = "DeviceIdentMgr";

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Consts
///////////////////////////////////////////////////////////////////////////////////////////////////////////////

const std::vector<DevInfoRec> DeviceIdentMgr::_hwDevInfoRecs = {
    {"VCNL4040", "VCNL4040", "Vishay", "VCNL4040", "0x60", "0x0c=0b100001100000XXXX", "0x041007=&0x030e08=&0x000000", 1000, "0x08=0bXXXXXXXXXXXXXXXX&0x09=0bXXXXXXXXXXXXXXXX&0x0a=0bXXXXXXXXXXXXXXXX"}
};

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

const DevInfoRec* DeviceIdentMgr::attemptDeviceIdent(const RaftI2CAddrAndSlot& addrAndSlot)
{
    // Check if enabled
    if (!_isEnabled)
    {
#ifdef DEBUG_DEVICE_IDENT_MGR
        LOG_I(MODULE_PREFIX, "Device ident not enabled");
#endif
        return nullptr;
    }

    // Check if this address is in the range of any known device
    for (const auto& devInfoRec : _hwDevInfoRecs)
    {
        if (devInfoRec.isAddrInRange(addrAndSlot))
        {
#ifdef DEBUG_DEVICE_IDENT_MGR
            LOG_I(MODULE_PREFIX, "attemptDeviceIdent dev %s addr@slot+1 %s addrRange %s", 
                        devInfoRec.typeKey.c_str(), addrAndSlot.toString().c_str(), devInfoRec.addressRange.c_str());
#endif
            // Check if the detection value(s) match responses from the device
            // Generate a bus request to read the detection value
            if (accessDeviceAndCheckReponse(addrAndSlot, devInfoRec))
            {
#ifdef DEBUG_DEVICE_IDENT_MGR
                LOG_I(MODULE_PREFIX, "Device ident found %s", devInfoRec.typeKey.c_str());
#endif
                // Initialise the device if required
                processDeviceInit(addrAndSlot, devInfoRec);

                // Device detected
                return &devInfoRec;
            }
        }
    } 

    return nullptr;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Access device and check response
///////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool DeviceIdentMgr::accessDeviceAndCheckReponse(const RaftI2CAddrAndSlot& addrAndSlot, const DevInfoRec& devInfoRec)
{
    // Get the detection values
    std::vector<RaftJson::NameValuePair> detectionWriteReadPairs;
    devInfoRec.getDetectionWriteReadPairs(detectionWriteReadPairs);

    // Check if all values match
    for (const auto& detectionNameValue : detectionWriteReadPairs)
    {
        std::vector<uint8_t> writeData;
        std::vector<uint8_t> readDataMask;
        std::vector<uint8_t> readDataCheck;
        if (!DevInfoRec::extractBufferDataFromHexStr(detectionNameValue.name, writeData))
            continue;
        if (!DevInfoRec::extractMaskAndDataFromHexStr(detectionNameValue.value, readDataMask, readDataCheck, true))
            continue;

#ifdef DEBUG_DEVICE_IDENT_MGR
        LOG_I(MODULE_PREFIX, "accessDeviceAndCheckReponse addr@slot+1 %s reg %s value %s", 
                    addrAndSlot.toString().c_str(), detectionNameValue.name.c_str(), detectionNameValue.value.c_str());
#endif

        // Create a bus request to read the detection value
        BusI2CRequestRec reqRec(BUS_REQ_TYPE_STD,
                    addrAndSlot,
                    0, writeData.size(), 
                    writeData.data(),
                    readDataCheck.size(),
                    nullptr, 
                    0, 
                    nullptr, 
                    this);
        std::vector<uint8_t> readData;
        RaftI2CCentralIF::AccessResultCode rslt = _busI2CReqSyncFn(&reqRec, &readData);

#ifdef DEBUG_DEVICE_IDENT_MGR
        String writeHexStr;
        Raft::getHexStrFromBytes(writeData.data(), writeData.size(), writeHexStr);
        String readHexStr;
        Raft::getHexStrFromBytes(readData.data(), readData.size(), readHexStr);
        LOG_I(MODULE_PREFIX, "accessDeviceAndCheckReponse addr@slot+1 %s writeData %s rslt %d readData %s", 
                    addrAndSlot.toString().c_str(), writeHexStr.c_str(), rslt, readHexStr.c_str());
#endif

        // Check ok result
        if (rslt != RaftI2CCentralIF::ACCESS_RESULT_OK)
            return false;

        // Check the read data
        if (readData.size() != readDataCheck.size())
        {
#ifdef DEBUG_DEVICE_IDENT_MGR
            LOG_I(MODULE_PREFIX, "accessDeviceAndCheckReponse addr@slot+1 %s reg %s value %s readData size mismatch", 
                        addrAndSlot.toString().c_str(), detectionNameValue.name.c_str(), detectionNameValue.value.c_str());
#endif
            return false;
        }
        for (int i = 0; i < readData.size(); i++)
        {
            uint8_t readDataMaskedVal = readData[i] & readDataMask[i];
#ifdef DEBUG_DEVICE_IDENT_MGR
            String readMaskStr;
            Raft::getHexStrFromBytes(readDataMask.data(), readDataMask.size(), readMaskStr);
            String readCheckStr;
            Raft::getHexStrFromBytes(readDataCheck.data(), readDataCheck.size(), readCheckStr);
            LOG_I(MODULE_PREFIX, "accessDeviceAndCheckReponse %s idx %d addr@slot+1 %s readMask 0x%s readCheck 0x%s readData 0x%02x readDataMaskedVal 0x%02x readDataMsg 0x%02x readDataCheck 0x%02x", 
                        readDataMaskedVal == readDataCheck[i] ? "OK" : "FAIL", i,
                        addrAndSlot.toString().c_str(), readMaskStr, readCheckStr, readData[i], readDataMaskedVal, readDataMask[i], readDataCheck[i]);
#endif
            if (readDataMaskedVal != readDataCheck[i])
                return false;
        }

#ifdef DEBUG_DEVICE_IDENT_MGR
        LOG_I(MODULE_PREFIX, "accessDeviceAndCheckReponse addr@slot+1 %s reg %s value %s OK", 
                    addrAndSlot.toString().c_str(), detectionNameValue.name.c_str(), detectionNameValue.value.c_str());
#endif
    }

    // Access the device and check the response
    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Process initialisation of a device
///////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool DeviceIdentMgr::processDeviceInit(const RaftI2CAddrAndSlot& addrAndSlot, const DevInfoRec& devInfoRec)
{
    // Extract initialisation records
    std::vector<RaftJson::NameValuePair> initWriteReadPairs;
    devInfoRec.getInitWriteReadPairs(initWriteReadPairs);

    // Initialise the device
    for (const auto& initNameValue : initWriteReadPairs)
    {
        std::vector<uint8_t> writeData;
        if (!DevInfoRec::extractBufferDataFromHexStr(initNameValue.name, writeData))
            continue;

        // Create a bus request to write the initialisation value
        BusI2CRequestRec reqRec(BUS_REQ_TYPE_STD,
                    addrAndSlot,
                    0, writeData.size(), 
                    writeData.data(),
                    0,
                    nullptr,
                    0, 
                    nullptr, 
                    this);
        _busI2CReqSyncFn(&reqRec, nullptr);
    }

    return true;
}

