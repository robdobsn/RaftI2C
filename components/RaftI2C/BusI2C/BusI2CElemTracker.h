/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Main Bus Elem Tracker
//
// Rob Dobson 2024
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <stdint.h>
#include "RaftBus.h"
#include "RaftUtils.h"
#include "BusI2CConsts.h"

class BusI2CElemTracker {

public:
    // Constructor and destructor
    BusI2CElemTracker()
    {
        // Clear found on main bus bits
        for (int i = 0; i < TRACKER_BITS_ARRAY_SIZE; i++)
        {
            _mainBusAddrBits[i] = 0;
            _muxBusAddrBits[i] = 0;
        }
    }

    ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Is address found on main bus
    /// @param addr address
    /// @return true if address found on main bus
    bool isAddrFoundOnMainBus(uint32_t addr) const
    {
        if (addr > I2C_BUS_ADDRESS_MAX)
            return false;
        return (_mainBusAddrBits[addr/32] & (1 << (addr % 32))) != 0;
    }

    ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Is address found on muliplexer
    /// @param addr address
    /// @return true if address found on multiplexer
    bool isAddrFoundOnMux(uint32_t addr) const
    {
        if (addr > I2C_BUS_ADDRESS_MAX)
            return false;
        return (_muxBusAddrBits[addr/32] & (1 << (addr % 32))) != 0;
    }

    ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Set element found (on slot 0 - main bus or on an extender)
    /// @param addr address
    /// @param slot slot number
    void setElemFound(uint32_t addr, uint16_t slot)
    {
        if (addr > I2C_BUS_ADDRESS_MAX)
            return;
        if (slot == 0)
            _mainBusAddrBits[addr/32] |= (1 << (addr % 32));
        else
            _muxBusAddrBits[addr/32] |= (1 << (addr % 32));
    }

    ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get all addresses found on mux bus
    /// @param slot slot number
    /// @param addrList List of addresses found on mux bus
    void getAddrList(uint32_t slot, std::vector<uint32_t>& addrList) const
    {
        for (uint32_t addr = 0; addr <= I2C_BUS_ADDRESS_MAX; addr++)
        {
            if ((slot == 0) && isAddrFoundOnMainBus(addr))
                addrList.push_back(addr);
            else if ((slot != 0) && isAddrFoundOnMux(addr))
                addrList.push_back(addr);
        }
    }

private:
    // Addresses found online on main bus / mux bus at any time
    static const uint32_t TRACKER_BITS_ARRAY_SIZE = (I2C_BUS_ADDRESS_MAX+31)/32;
    uint32_t _mainBusAddrBits[TRACKER_BITS_ARRAY_SIZE] = {0};
    uint32_t _muxBusAddrBits[TRACKER_BITS_ARRAY_SIZE] = {0};

    // Debug
    static constexpr const char* MODULE_PREFIX = "BusI2CElemTracker";
};
