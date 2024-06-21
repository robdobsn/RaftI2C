////////////////////////////////////////////////////////////////////////////////
//
// BusI2CTest.h
//
////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "RaftArduino.h"
#include "RaftSysMod.h"

// #define TURN_ON_COMPLEX_POWER_INITIALLY

#ifdef TURN_ON_COMPLEX_POWER_INITIALLY
#include "RaftBusSystem.h"
#include "BusI2C.h"
#endif

class BusI2CTest : public RaftSysMod
{
public:
    BusI2CTest(const char *pModuleName, RaftJsonIF& sysConfig);
    virtual ~BusI2CTest();

    // Create function (for use by SysManager factory)
    static RaftSysMod* create(const char* pModuleName, RaftJsonIF& sysConfig)
    {
        return new BusI2CTest(pModuleName, sysConfig);
    }

protected:

    // Setup
    virtual void setup() override final;

    // Loop (called frequently)
    virtual void loop() override final;

private:
    // Example of how to control loop rate
    uint32_t _lastLoopMs = 0;

#ifdef TURN_ON_COMPLEX_POWER_INITIALLY
    // Setup flag for power control on bus
    bool _busPowerInit = false;

    // Helper
    void setPower3V3OnAllSlots(RaftBus* pBus);
#endif
};
