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

class DevInfoRec
{
public:
    // Device info
    String friendlyName;
    String manufacturer;
    String model;

    // This MUST either be 0xXX or 0xXX-0xXX
    String addressRange;

    std::vector<String> detectionValues;

    bool isAddrInRange(RaftI2CAddrAndSlot addrAndSlot)
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

private:
    void convertAddressRangeToMinMax(uint32_t& minAddr, uint32_t& maxAddr)
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
