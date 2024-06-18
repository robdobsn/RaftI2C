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
}

BusI2CTest::~BusI2CTest()
{
}

void BusI2CTest::setup()
{
    // Get controlled power pin
    int controlled3V3Pin = config.getInt("controlled3V3Pin", -1);
    if (controlled3V3Pin < 0)
    {
        LOG_W(MODULE_PREFIX, "No controlled3V3Pin specified");
    }
    else
    {
        // Turn on power
        pinMode(controlled3V3Pin, OUTPUT);
        digitalWrite(controlled3V3Pin, HIGH);
    }

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

