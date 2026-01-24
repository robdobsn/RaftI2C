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
#include "RaftBusConsts.h"
#include "BusI2CConsts.h"

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @class BusI2CAddrAndSlot
/// @brief I2C address and slot (0 when device is not connected through bus extender)
/// @details The address is the I2C address and the slotNum is a number from 1 to 64
///          which is used to identify the device when connected through a bus extender
/// @note The slotNum value of 0 can also be used to address a device which is connected
///       to a bus expander and if more than one device is on the same address, the
///       first device found will be used
class BusI2CAddrAndSlot
{
public:
    BusI2CAddrAndSlot()
    {
        clear();
    }
    BusI2CAddrAndSlot(uint32_t i2cAddr, uint32_t slotNum)
    {
        this->i2cAddr = i2cAddr;
        this->slotNum = slotNum;
    }
    static BusI2CAddrAndSlot fromBusElemAddrType(BusElemAddrType compositeAddrAndSlot)
    {
        BusI2CAddrAndSlot addrAndSlot;
        addrAndSlot.i2cAddr = compositeAddrAndSlot & 0xFF;
        addrAndSlot.slotNum = (compositeAddrAndSlot >> 8) & 0x3F;
        return addrAndSlot;
    }
    inline BusElemAddrType toBusElemAddrType() const
    {
        return (i2cAddr & 0xFF) | ((slotNum & 0x3F) << 8);
    }
    static inline uint16_t getI2CAddr(BusElemAddrType compositeAddrAndSlot)
    {
        return compositeAddrAndSlot & 0xFF;
    }
    static inline uint16_t getSlotNum(BusElemAddrType compositeAddrAndSlot)
    {
        return (compositeAddrAndSlot >> 8) & 0x3F;
    }
    void clear()
    {
        i2cAddr = 0;
        slotNum = 0;
    }
    bool operator==(const BusI2CAddrAndSlot& other) const
    {
        return i2cAddr == other.i2cAddr && slotNum == other.slotNum;
    }
    bool operator==(BusElemAddrType address) const
    {
        return address == toBusElemAddrType();
    }
    String toString() const
    {
        char buf[16];
        snprintf(buf, sizeof(buf), "%s%x@%u", RAFT_BUS_ADDR_PREFIX, i2cAddr, slotNum);
        return String(buf);
    }
    static String toString(BusElemAddrType address)
    {
        char buf[16];
        snprintf(buf, sizeof(buf), "%s%x@%u", RAFT_BUS_ADDR_PREFIX, getI2CAddr(address), getSlotNum(address));
        return String(buf);
    }
    void fromString(const String& str)
    {
        // Split into address and slot
        int atPos = str.indexOf('@');
        if (atPos < 0)
        {
            i2cAddr = strtol(str.c_str(), NULL, 0);
            slotNum = 0;
        }
        else
        {
            i2cAddr = strtol(str.substring(0, atPos).c_str(), NULL, 0);
            slotNum = strtol(str.substring(atPos + 1).c_str(), NULL, 0);
        }
    }
    uint16_t i2cAddr:10;
    uint8_t slotNum:6;
};
