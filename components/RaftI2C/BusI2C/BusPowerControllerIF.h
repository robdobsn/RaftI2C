/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// I2C Bus Power Controller Interface
//
// Rob Dobson 2025
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <stdint.h>
#include <vector>
#include "RaftJsonIF.h"
#include "BusRequestInfo.h"

/// @brief Bus power controllers handle power to either the whole bus OR on a per slot basis
class BusPowerControllerIF
{
public:
    // Constructor and destructor
    BusPowerControllerIF();
    virtual ~BusPowerControllerIF();

    // Setup
    virtual void setup(const RaftJsonIF& config);
    virtual bool postSetup();

    // Service
    virtual void loop();

    // Service called from I2C task
    virtual void taskService(uint64_t timeNowUs);

    // Check if slot has stable power
    virtual bool isSlotPowerStable(uint32_t slotNum);

    /// @brief Power cycle slot
    /// @param slotNum slot number (1 based) (0 to power cycle bus)
    virtual void powerCycleSlot(uint32_t slotNum);

    /// @brief Check if slot power is controlled
    /// @param slotNum slot number (1 based)
    /// @return true if slot power is controlled
    virtual bool isSlotPowerControlled(uint32_t slotNum);

    /// @brief Check if address is a bus power controller
    /// @param i2cAddr address of bus power controller
    /// @param muxAddr address of mux (0 if on main I2C bus)
    /// @param muxChannel channel on mux
    /// @return true if address is a bus power controller
    virtual bool isBusPowerController(uint16_t i2cAddr, uint16_t muxAddr, uint16_t muxChannel);

    /// @brief Set synchronous call function for bus requests
    /// @param busReqSyncFn function to call to perform bus request
    virtual void setBusReqSyncFn(BusReqSyncFn busReqSyncFn)
    {
        _busReqSyncFn = busReqSyncFn;
    }

private:
    // Bus access function
    BusReqSyncFn _busReqSyncFn = nullptr;
};
