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
/// @brief I2C address and slot
/// @details The address is the I2C address and the slotNum is a number which is 0 for devices not connected 
///          through a bus extender or from 1 to 64 for devices connected through a bus extender
class BusI2CAddrAndSlot
{
public:
    BusI2CAddrAndSlot()
    {
        clear();
    }
    BusI2CAddrAndSlot(uint32_t i2cAddr, uint32_t slotNum)
    {
        _address = (i2cAddr & 0xFF) | ((slotNum & 0x3F) << 8);
    }
    BusI2CAddrAndSlot(BusElemAddrType compositeAddrAndSlot)
    {
        _address = compositeAddrAndSlot;
    }
    operator BusElemAddrType() const
    {
        return _address;
    }
    uint16_t getI2CAddr() const
    {
        return _address & 0xFF;
    }
    uint16_t getSlotNum() const
    {
        return (_address >> 8) & 0x3F;
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
        _address = 0;
    }
    bool operator==(const BusI2CAddrAndSlot& other) const
    {
        return _address == other._address;
    }
    bool operator==(BusElemAddrType address) const
    {
        return _address == address;
    }
    String toString() const
    {
        return String(_address, 16);
    }
    static String toString(BusElemAddrType address)
    {
        return String(address, 16);
    }
private:
    BusElemAddrType _address;
};
