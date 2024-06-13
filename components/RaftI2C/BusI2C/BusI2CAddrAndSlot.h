/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// I2C Bus Address and Slot
//
// Rob Dobson 2024
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <stdint.h>
#include "RaftArduino.h"
#include "BusI2CConsts.h"

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @class BusI2CAddrAndSlot
/// @brief I2C address and slot (0 when device is not connected through bus extender)
/// @details The address is the I2C address and the slotPlus1 is a number from 1 to 64
///          which is used to identify the device when connected through a bus extender
/// @note The slotPlus1 value of 0 can also be used to address a device which is connected
///       to a bus expander and if more than one device is on the same address, the
///       first device found will be used
class BusI2CAddrAndSlot
{
public:
    BusI2CAddrAndSlot()
    {
        clear();
    }
    BusI2CAddrAndSlot(uint32_t addr, uint32_t slotPlus1)
    {
        this->addr = addr;
        this->slotPlus1 = slotPlus1;
    }
    static BusI2CAddrAndSlot fromCompositeAddrAndSlot(uint32_t compositeAddrAndSlot)
    {
        BusI2CAddrAndSlot addrAndSlot;
        addrAndSlot.addr = compositeAddrAndSlot & 0xFF;
        addrAndSlot.slotPlus1 = (compositeAddrAndSlot >> 8) & 0x3F;
        return addrAndSlot;
    }
    uint32_t toCompositeAddrAndSlot() const
    {
        return (addr & 0xFF) | ((slotPlus1 & 0x3F) << 8);
    }
    void clear()
    {
        addr = 0;
        slotPlus1 = 0;
    }
    bool operator==(const BusI2CAddrAndSlot& other) const
    {
        return addr == other.addr && slotPlus1 == other.slotPlus1;
    }
    String toString() const
    {
        return "0x" + String(addr, 16) + "@" + String(slotPlus1);
    }
    void fromString(const String& str)
    {
        // Split into address and slot
        int atPos = str.indexOf('@');
        if (atPos < 0)
        {
            addr = strtol(str.c_str(), NULL, 0);
            slotPlus1 = 0;
        }
        else
        {
            addr = strtol(str.substring(0, atPos).c_str(), NULL, 0);
            slotPlus1 = strtol(str.substring(atPos + 1).c_str(), NULL, 0);
        }
    }
    uint16_t addr:10;
    uint8_t slotPlus1:6;
};
