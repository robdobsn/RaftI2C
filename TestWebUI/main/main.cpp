/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Main entry point
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "RaftCoreApp.h"
#include "RegisterSysMods.h"
#include "RegisterWebServer.h"
#include "BusI2CTest.h"
#include "BusI2C.h"
#include "HWDevMan.h"

// Entry point
extern "C" void app_main(void)
{
    RaftCoreApp raftCoreApp;
    
    // Register SysMods from RaftSysMods library
    RegisterSysMods::registerSysMods(raftCoreApp.getSysManager());

    // Register WebServer from RaftWebServer library
    RegisterSysMods::registerWebServer(raftCoreApp.getSysManager());

    // Register BusI2C
    raftBusSystem.registerBus("I2C", BusI2C::createFn);

    // Register sysmod
    raftCoreApp.registerSysMod("BusI2CTest", BusI2CTest::create, true);

    // Device manager
    raftCoreApp.registerSysMod("HWDevMan", HWDevMan::create, true);

    // Loop forever
    while (1)
    {
        // Yield for 1 tick
        vTaskDelay(1);

        // Loop the app
        raftCoreApp.loop();
    }
}
