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
        LOG_W(MODULE_PREFIX, "setup no controlled3V3Pin specified");
    }
    else
    {
        // Turn on power
        pinMode(controlled3V3Pin, OUTPUT);
        digitalWrite(controlled3V3Pin, HIGH);
        LOG_I(MODULE_PREFIX, "setup controlled3V3Pin %d to HIGH", controlled3V3Pin);
    }
    delay(200);
}

void BusI2CTest::loop()
{
#ifdef TURN_ON_COMPLEX_POWER_INITIALLY
    if (!_busPowerInit)
    {
        String jsonStr;
        for (RaftBus* pBus : raftBusSystem.getBusList())
        {
            if (!pBus)
                continue;
            // // Get device interface
            // RaftBusDevicesIF* pDevicesIF = pBus->getBusDevicesIF();
            // if (!pDevicesIF)
            //     continue; 

            // Send a command to mux to select the device
            setPower3V3OnAllSlots(pBus);
        }

        _busPowerInit = true;
    }
#endif

    // Check for loop rate
    if (Raft::isTimeout(millis(), _lastLoopMs, 1000))
    {
        // Update last loop time
        _lastLoopMs = millis();

        // Put some code here that will be executed once per second
        // ...
    }
}

#ifdef TURN_ON_COMPLEX_POWER_INITIALLY
void BusI2CTest::setPower3V3OnAllSlots(RaftBus* pBus)
{
    const int muxResetPin = 2;
    const int muxAddr = 0x75;
    const int ioExpAddr = 0x25;
    const int ioExpMuxChan = 7;
    const int PCA9535_OUTPUT_PORT_0 = 0x02;
    const int PCA9535_CONFIG_PORT_0 = 0x06;
    const uint32_t outputsReg = 0x0555;
    const uint32_t configReg = ~outputsReg;

    // Check reset pin is output and set to 1
    if (muxResetPin >= 0)
    {
        pinMode(muxResetPin, OUTPUT);
        digitalWrite(muxResetPin, HIGH);
        LOG_I(MODULE_PREFIX, "update muxResetPin %d set to HIGH", muxResetPin);
    }

    // Set the mux channel
    uint8_t muxWriteData[1] = { (uint8_t)(1 << ioExpMuxChan) };
    BusRequestInfo reqRec(BUS_REQ_TYPE_FAST_SCAN,
                muxAddr,
                0, sizeof(muxWriteData),
                muxWriteData,
                0,
                0, 
                nullptr, 
                nullptr);
    pBus->addRequest(reqRec);

    // Set the output register first (to avoid unexpected power changes)
    uint8_t outputPortData[3] = { PCA9535_OUTPUT_PORT_0, 
                uint8_t(outputsReg & 0xff), 
                uint8_t(outputsReg >> 8)};
    reqRec = {BUS_REQ_TYPE_FAST_SCAN,
                ioExpAddr,
                0, sizeof(outputPortData),
                outputPortData,
                0,
                0, 
                nullptr, 
                nullptr};
    pBus->addRequest(reqRec);

    // Write the configuration register
    uint8_t configPortData[3] = { PCA9535_CONFIG_PORT_0, 
                uint8_t(configReg & 0xff), 
                uint8_t(configReg >> 8)};
    reqRec = {BUS_REQ_TYPE_FAST_SCAN,
                ioExpAddr,
                0, sizeof(configPortData),
                configPortData,
                0,
                0, 
                nullptr, 
                nullptr};
    pBus->addRequest(reqRec);

    // Clear the mux channel
    muxWriteData[1] = { 0 };
    reqRec = {BUS_REQ_TYPE_FAST_SCAN,
                muxAddr,
                0, sizeof(muxWriteData),
                muxWriteData,
                0,
                0, 
                nullptr, 
                nullptr};
    pBus->addRequest(reqRec);
}
#endif
