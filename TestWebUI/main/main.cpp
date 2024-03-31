/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Main entry point
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "RaftCoreApp.h"
#include "RegisterSysMods.h"
#include "RegisterWebServer.h"
#include "BusI2CTest.h"
#include "DevMan.h"

// Entry point
extern "C" void app_main(void)
{
    RaftCoreApp raftCoreApp;
    
    // Register SysMods from RaftSysMods library
    RegisterSysMods::registerSysMods(raftCoreApp.getSysManager());

    // Register WebServer from RaftWebServer library
    RegisterSysMods::registerWebServer(raftCoreApp.getSysManager());

    // Register sysmod
    raftCoreApp.registerSysMod("BusI2CTest", BusI2CTest::create, true);

    // TODO - fix this as it should be somewhere else
    raftCoreApp.registerSysMod("DevMan", DevMan::create, true);

    // Loop forever
    while (1)
    {
        // Yield for 1 tick
        vTaskDelay(1);

        // Loop the app
        raftCoreApp.loop();
    }
}
