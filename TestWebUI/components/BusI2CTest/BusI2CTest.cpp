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
    int bus3V3En = config.getInt("bus3V3En", 0);
    if (bus3V3En >= 0)
    {
        // Enable 3.3V bus
        pinMode(bus3V3En, OUTPUT);
        digitalWrite(bus3V3En, HIGH);
    }
    delay(200);
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
