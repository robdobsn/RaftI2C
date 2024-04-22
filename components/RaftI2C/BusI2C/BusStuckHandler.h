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
#include "driver/gpio.h"

class BusExtenderMgr;

class BusStuckHandler
{
public:
    // Constructor and destructor
    BusStuckHandler(BusExtenderMgr& BusExtenderMgr);
    virtual ~BusStuckHandler();

    // Setup
    void setup(const RaftJsonIF& config);

    // Service
    void service();

    // Check bus stuck
    bool isStuck();

private:
    // Bus extender manager
    BusExtenderMgr& _BusExtenderMgr;

    // Pins
    gpio_num_t _sdaPin = GPIO_NUM_NC;
    gpio_num_t _sclPin = GPIO_NUM_NC;
};