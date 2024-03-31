////////////////////////////////////////////////////////////////////////////////
//
// BusI2CTest.h
//
////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "RaftArduino.h"
#include "RaftSysMod.h"

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
};
