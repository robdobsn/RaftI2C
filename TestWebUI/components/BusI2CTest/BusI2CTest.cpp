////////////////////////////////////////////////////////////////////////////////
//
// BusI2CTest.h
//
////////////////////////////////////////////////////////////////////////////////

#include "BusI2CTest.h"
#include "RaftUtils.h"

static const char *MODULE_PREFIX = "BusI2CTest";

BusI2CTest::BusI2CTest(const char *pModuleName, RaftJsonIF& sysConfig)
    : RaftSysMod(pModuleName, sysConfig)
{
    // This code is executed when the system module is created
    // ...
}

BusI2CTest::~BusI2CTest()
{
    // This code is executed when the system module is destroyed
    // ...
}

void BusI2CTest::setup()
{
    // The following code is an example of how to use the config object to
    // get a parameter from SysType (JSON) file for this system module
    // Replace this with your own setup code
    String configValue = config.getString("exampleGroup/exampleKey", "This Should Not Happen!");
    LOG_I(MODULE_PREFIX, "%s", configValue.c_str());
}

void BusI2CTest::loop()
{
    // Check for loop rate
    if (Raft::isTimeout(millis(), _lastLoopMs, 1000))
    {
        // Update last loop time
        _lastLoopMs = millis();

        // Put some code here that will be executed once per second
        // ...
    }
}

