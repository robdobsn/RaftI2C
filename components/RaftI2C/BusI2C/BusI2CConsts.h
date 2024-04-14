/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// I2C Bus Constants
//
// Rob Dobson 2024
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <stdint.h>
#include "RaftArduino.h"

// Use replacement I2C library - if not defined use original ESP IDF I2C implementation
#define I2C_USE_RAFT_I2C

// I2C addresses
static const uint32_t I2C_BUS_ADDRESS_MIN = 4;
static const uint32_t I2C_BUS_ADDRESS_MAX = 0x77;
static const uint32_t I2C_BUS_EXTENDER_BASE = 0x70;
static const uint32_t I2C_BUS_EXTENDERS_MAX = 8;

// I2C address and slot
// The slotPlus1 value is 0 if the device is not connected to an I2C bus expander
// The slotPlus1 value is 1-63 to specify the slot on the I2C bus expander with
// 1 indicating the first slot on the first expander
// The slotPlus1 value of 0 can also be used to address a device which is connected
// to a bus expander and if more than one device is on the same address, the
// first device found will be used
class RaftI2CAddrAndSlot
{
public:
    RaftI2CAddrAndSlot()
    {
        clear();
    }
    RaftI2CAddrAndSlot(uint32_t addr, uint32_t slotPlus1)
    {
        this->addr = addr;
        this->slotPlus1 = slotPlus1;
    }
    static RaftI2CAddrAndSlot fromCompositeAddrAndSlot(uint32_t compositeAddrAndSlot)
    {
        RaftI2CAddrAndSlot addrAndSlot;
        addrAndSlot.addr = compositeAddrAndSlot & 0x3FF;
        addrAndSlot.slotPlus1 = (compositeAddrAndSlot >> 10) & 0x3F;
        return addrAndSlot;
    }
    uint32_t toCompositeAddrAndSlot()
    {
        return (addr & 0x3FF) | ((slotPlus1 & 0x3F) << 10);
    }
    void clear()
    {
        addr = 0;
        slotPlus1 = 0;
    }
    bool operator==(const RaftI2CAddrAndSlot& other) const
    {
        return addr == other.addr && slotPlus1 == other.slotPlus1;
    }
    String toString() const
    {
        return "0x" + String(addr, 16) + "@" + String(slotPlus1);
    }
    uint16_t addr:10;
    uint8_t slotPlus1:6;
};
