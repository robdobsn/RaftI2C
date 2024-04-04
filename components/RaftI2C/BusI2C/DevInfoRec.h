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
#include "DeviceIdent.h"

class DevInfoRec
{
public:
    // Device info
    String typeKey;
    String friendlyName;
    String manufacturer;
    String model;

    // This MUST either be 0xXX or 0xXX-0xXX
    String addressRange;

    // Detection values
    String detectionValues;

    // Initilisation values
    String initValues;

    // Poll interval (ms)
    uint32_t pollIntervalMs = 0;

    // Poll request
    String pollRequest;

    // Get device ident
    DeviceIdent getDeviceIdent() const
    {
        return DeviceIdent(typeKey);
    }

    bool isAddrInRange(RaftI2CAddrAndSlot addrAndSlot) const
    {
        // Convert address range to min and max addresses
        uint32_t minAddr = 0;
        uint32_t maxAddr = 0;
        convertAddressRangeToMinMax(minAddr, maxAddr);
        if (minAddr == 0 && maxAddr == 0)
        {
            return false;
        }
        if (addrAndSlot.addr < minAddr || addrAndSlot.addr > maxAddr)
        {
            return false;
        }
        return true;
    }

    void getDevicePollReqs(RaftI2CAddrAndSlot addrAndSlot, std::vector<BusI2CRequestRec>& pollRequests) const
    {
        // Extract name-value pairs polling string
        std::vector<RaftJson::NameValuePair> pollWriteReadPairs;
        RaftJson::extractNameValues(pollRequest, "=", "&", ";", pollWriteReadPairs);

        // Create a polling request for each pair
        pollRequests.clear();
        for (const auto& pollWriteReadPair : pollWriteReadPairs)
        {
            std::vector<uint8_t> writeData;
            if (!extractBufferDataFromHexStr(pollWriteReadPair.name, writeData))
                continue;
            std::vector<uint8_t> readDataMask;
            std::vector<uint8_t> readData;
            if (!extractMaskAndDataFromHexStr(pollWriteReadPair.value, readDataMask, readData, false))
                continue;

            // Create the poll request
            BusI2CRequestRec pollReq(BUS_REQ_TYPE_POLL, 
                    addrAndSlot,
                    DevicePollingInfo::DEV_IDENT_POLL_CMD_ID, 
                    writeData.size(),
                    writeData.data(), 
                    readDataMask.size(),
                    readDataMask.data(), 
                    0, 
                    NULL, 
                    NULL);
            pollRequests.push_back(pollReq);
        }
    }

    void getDetectionWriteReadPairs(std::vector<RaftJson::NameValuePair>& detectionWriteReadPairs) const
    {
        // Extract name-value pairs from the detection string
        RaftJson::extractNameValues(detectionValues, "=", "&", ";", detectionWriteReadPairs);
    }

    void getInitWriteReadPairs(std::vector<RaftJson::NameValuePair>& initWriteReadPairs) const
    {
        // Extract name-value pairs from the init string
        RaftJson::extractNameValues(initValues, "=", "&", ";", initWriteReadPairs);
    }

    static bool extractBufferDataFromHexStr(const String& writeStr, std::vector<uint8_t>& writeData)
    {        
        const char* pStr = writeStr.c_str();
        uint32_t inStrLen = writeStr.length();
        if (writeStr.startsWith("0x") || writeStr.startsWith("0X"))
        {
            pStr = writeStr.c_str() + 2;
            inStrLen -= 2;
        }
        // Compute length
        uint32_t writeStrLen = (inStrLen + 1) / 2;
        // Extract the write data
        writeData.resize(writeStrLen);
        Raft::getBytesFromHexStr(pStr, writeData.data(), writeData.size());
        return writeData.size() > 0;
    }

    static bool extractMaskAndDataFromHexStr(const String& readStr, std::vector<uint8_t>& readDataMask, 
                std::vector<uint8_t>& readDataCheck, bool maskToZeros)
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
                    readDataMask[byteIdx] = maskToZeros ? 0xff : 0;
                    readDataCheck[byteIdx] = 0;
                }
                if (readStrLC[i] == 'x')
                {
                    if (maskToZeros)
                        readDataMask[byteIdx] &= ~bitMask;
                    else
                        readDataMask[byteIdx] |= bitMask;
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

private:

    void convertAddressRangeToMinMax(uint32_t& minAddr, uint32_t& maxAddr) const
    {
        // Check if the address range is a single address
        if (addressRange.length() == 4)
        {
            minAddr = maxAddr = strtoul(addressRange.c_str(), NULL, 16);
            return;
        }

        // Check if the address range is a range
        if (addressRange.length() == 9)
        {
            minAddr = strtoul(addressRange.c_str(), NULL, 16);
            maxAddr = strtoul(addressRange.c_str() + 5, NULL, 16);
            return;
        }

        // Invalid address range
        minAddr = 0;
        maxAddr = 0;
    }
};
