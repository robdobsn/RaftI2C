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
#include "BusBase.h"
#include "RaftJson.h"
#include <vector>
#include <list>

class DeviceIdentMgr
{
public:
    DeviceIdentMgr(BusBase& busBase, BusI2CReqSyncFn busI2CReqSyncFn);
    void setup(const RaftJsonIF& config);
  
    // Attempt device identification
    DeviceIdent attemptDeviceIdent(const RaftI2CAddrAndSlot& addrAndSlot);

    // Communicate with device to check identity
    bool accessDeviceAndCheckReponse(const RaftI2CAddrAndSlot& addrAndSlot, const std::vector<RaftJson::NameValuePair>& detectionNameValues);

    // Process device initialisation
    bool processDeviceInit(const RaftI2CAddrAndSlot& addrAndSlot, const DevInfoRec& devInfoRec);

private:
    bool extractWriteData(const String& writeStr, std::vector<uint8_t>& writeData)
    {
        String writeStrLC = writeStr;
        writeStrLC.toLowerCase();
        // Check if writeStr starts with 0x
        if (!writeStrLC.startsWith("0x"))
        {
            return false;
        }
        // Compute length
        uint32_t writeStrLen = (writeStrLC.length() - 2 + 1) / 2;
        // Extract the write data
        writeData.resize(writeStrLen);
        Raft::getBytesFromHexStr(writeStr.c_str()+2, writeData.data(), writeData.size());
        return writeData.size() > 0;
    }

    bool extractReadMaskAndCheck(const String& readStr, std::vector<uint8_t>& readDataMask, std::vector<uint8_t>& readDataCheck)
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
                    readDataMask[byteIdx] = 0xff;
                    readDataCheck[byteIdx] = 0;
                }
                if (readStrLC[i] == 'x')
                {
                    readDataMask[byteIdx] &= ~bitMask;
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

    // Device indentification enabled
    bool _isEnabled = false;

    // Device info records
    static const std::vector<DevInfoRec> _hwDevInfoRecs;

    // Bus base
    BusBase& _busBase;

    // Bus i2c request function
    BusI2CReqSyncFn _busI2CReqSyncFn = nullptr;


//    static const uint32_t HW_DEV_DETECT_NEXT_MS = 100;

//     void deviceOnline(RaftI2CAddrAndSlot addrAndSlot)
//     {
//         // Find the record for this address if it exists
//         for (auto& devProc : _hwDevProcRecs)
//         {
//             if (devProc.addrAndSlot == addrAndSlot)
//             {
//                 return;
//             }
//         }

//         // Create a new record
//         _hwDevProcRecs.push_back(DevProcRec(addrAndSlot));
//     }

//     void service()
//     {
//         for (auto& devProc : _hwDevProcRecs)
//         {
//             // Check if already detected
//             if (devProc.state == DevProcRec::HW_DEV_PROC_STATE_DETECTED)
//             {
//                 continue;
//             }

//             // Check if we need to change state
//             if (devProc.state == DevProcRec::HW_DEV_PROC_STATE_DETECTING)
//             {
//                 // Check if ready for the next state
//                 if (Raft::isTimeout(millis(), devProc.lastStateChangeMs, HW_DEV_DETECT_NEXT_MS))
//                 {
//                     devProc.lastStateChangeMs = millis();

//                     // Check if we have run out of detection values
//                     if (devProc.curDevInfoRecordIdx >= _hwDevInfoRecs.size())
//                     {
//                         devProc.state = DevProcRec::HW_DEV_PROC_STATE_UNKNOWN_DEVICE_TYPE;
//                         continue;
//                     }

//                     // Find a device info record that matches the address
//                     for (; devProc.curDevInfoRecordIdx < _hwDevInfoRecs.size(); devProc.curDevInfoRecordIdx++)
//                     {
//                         if (_hwDevInfoRecs[devProc.curDevInfoRecordIdx].isAddrInRange(devProc.addrAndSlot))
//                         {
//                             // Check if the detection value
//                             for (const auto& detectionValue : _hwDevInfoRecs[devProc.curDevInfoRecordIdx].detectionValues)
//                             {
//                                 // Generate a bus request to read the detection value
//                                 if (accessDeviceAndCheckReponse(devProc, detectionValue))
//                                 {
//                                     devProc.devInfoRec = _hwDevInfoRecs[devProc.curDevInfoRecordIdx];
//                                     devProc.state = DevProcRec::HW_DEV_PROC_STATE_DETECTED;
//                                     break;
//                                 }
//                             }
//                             devProc.curDevInfoRecordIdx++;
//                             break;
//                         }
//                     }
//                 }
//             }
//         }
//     }

// private:
//     bool accessDeviceAndCheckReponse(DevProcRec& devProc, const String& detectionValue)
//     {
//         // TODO
//         // std::vector<uint8_t> writeData;
//         // std::vector<uint8_t> readDataMask;
//         // std::vector<uint8_t> readDataCheck;
//         // extractRegAndValue(detectionValue, writeData, readDataMask, readData);

//         // // Create a bus request to read the detection value
//         // BusI2CRequestRec reqRec(BUS_REQ_TYPE_POLL,
//         //             devProc.addrAndSlot,
//         //             0, writeData.size(), 
//         //             writeData.data(),
//         //             readData.size(), 0, 
//         //             nullptr, 
//         //             this);
//         // std::vector<uint8_t> readData;
//         // RaftI2CCentralIF::AccessResultCode rslt = _busI2CReqSyncFn(&reqRec, readData);

//         // Access the device and check the response
//         return true;
//     }
    // std::list<DevProcRec> _hwDevProcRecs;
};
