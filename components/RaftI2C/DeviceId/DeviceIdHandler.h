

#pragma once

#include "DevInfoRec.h"
#include "DevProcRec.h"
#include "BusI2CRequestRec.h"
#include "BusBase.h"
#include <vector>
#include <list>

class DeviceIdHandler
{
public:
    static const uint32_t HW_DEV_DETECT_NEXT_MS = 100;

    DeviceIdHandler(BusBase& busBase, BusI2CReqSyncFn busI2CReqSyncFn) :
        _busBase(busBase),
        _busI2CReqSyncFn(busI2CReqSyncFn)
    {
        _hwDevInfoRecs.push_back(DevInfoRec({"VCNL4040", "Vishay", "VCNL4040", "0x60", {"0x0c==0b100001100000XXXX"}}));
    }

    void deviceOnline(RaftI2CAddrAndSlot addrAndSlot)
    {
        // Find the record for this address if it exists
        for (auto& devProc : _hwDevProcRecs)
        {
            if (devProc.addrAndSlot == addrAndSlot)
            {
                return;
            }
        }

        // Create a new record
        _hwDevProcRecs.push_back(DevProcRec(addrAndSlot));
    }

    void service()
    {
        for (auto& devProc : _hwDevProcRecs)
        {
            // Check if already detected
            if (devProc.state == DevProcRec::HW_DEV_PROC_STATE_DETECTED)
            {
                continue;
            }

            // Check if we need to change state
            if (devProc.state == DevProcRec::HW_DEV_PROC_STATE_DETECTING)
            {
                // Check if ready for the next state
                if (Raft::isTimeout(millis(), devProc.lastStateChangeMs, HW_DEV_DETECT_NEXT_MS))
                {
                    devProc.lastStateChangeMs = millis();

                    // Check if we have run out of detection values
                    if (devProc.curDevInfoRecordIdx >= _hwDevInfoRecs.size())
                    {
                        devProc.state = DevProcRec::HW_DEV_PROC_STATE_UNKNOWN_DEVICE_TYPE;
                        continue;
                    }

                    // Find a device info record that matches the address
                    for (; devProc.curDevInfoRecordIdx < _hwDevInfoRecs.size(); devProc.curDevInfoRecordIdx++)
                    {
                        if (_hwDevInfoRecs[devProc.curDevInfoRecordIdx].isAddrInRange(devProc.addrAndSlot))
                        {
                            // Check if the detection value
                            for (const auto& detectionValue : _hwDevInfoRecs[devProc.curDevInfoRecordIdx].detectionValues)
                            {
                                // Generate a bus request to read the detection value
                                if (accessDeviceAndCheckReponse(devProc, detectionValue))
                                {
                                    devProc.devInfoRec = _hwDevInfoRecs[devProc.curDevInfoRecordIdx];
                                    devProc.state = DevProcRec::HW_DEV_PROC_STATE_DETECTED;
                                    break;
                                }
                            }
                            devProc.curDevInfoRecordIdx++;
                            break;
                        }
                    }
                }
            }
        }
    }

private:
    bool accessDeviceAndCheckReponse(DevProcRec& devProc, const String& detectionValue)
    {
        std::vector<uint8_t> writeData;
        std::vector<uint8_t> readDataMask;
        std::vector<uint8_t> readDataCheck;
        extractRegAndValue(detectionValue, writeData, readDataMask, readData);

        // Create a bus request to read the detection value
        BusI2CRequestRec reqRec(BUS_REQ_TYPE_POLL,
                    devProc.addrAndSlot,
                    0, writeData.size(), 
                    writeData.data(),
                    readData.size(), 0, 
                    nullptr, 
                    this);
        std::vector<uint8_t> readData;
        RaftI2CCentralIF::AccessResultCode rslt = _busI2CReqSyncFn(&reqRec, readData);

        // Access the device and check the response
        return true;
    }

    std::vector<DevInfoRec> _hwDevInfoRecs;
    std::list<DevProcRec> _hwDevProcRecs;

    // Bus base
    BusBase& _busBase;

    // Bus i2c request function
    BusI2CReqSyncFn _busI2CReqSyncFn = nullptr;

};
