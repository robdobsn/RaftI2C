/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// I2C Bus Stuck Handler
//
// Rob Dobson 2024
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <stdint.h>
#include "RaftJsonIF.h"
#include "BusRequestInfo.h"
#include "driver/gpio.h"

class BusStuckHandler
{
public:
    // Constructor and destructor
    BusStuckHandler(BusReqSyncFn busReqSyncFn);
    virtual ~BusStuckHandler();

    // Setup
    void setup(const RaftJsonIF& config);

    // Service (from I2C task only)
    void loopSync();

    /// @brief Check bus stuck (must be called from I2C task)
    bool isStuck();

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Clear bus stuck problems by clocking the bus
    void clearStuckByClocking();

    // Address to use when attempting to clear bus-stuck problems
    static const uint32_t I2C_BUS_STUCK_CLEAR_ADDR = 0x77;
    static const uint32_t I2C_BUS_STUCK_REPEAT_COUNT = 3;

private:
    // Pins
    gpio_num_t _sdaPin = GPIO_NUM_NC;
    gpio_num_t _sclPin = GPIO_NUM_NC;

    // Last state
    bool _wasStuck = false;

    // Bus I2C Request Sync function
    BusReqSyncFn _busReqSyncFn = nullptr;

    // Debug
    static constexpr const char* MODULE_PREFIX = "RaftI2CBusStuck";    
};
