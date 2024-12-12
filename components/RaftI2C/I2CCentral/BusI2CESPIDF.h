/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Original ESP IDF I2C Device Interface
//
// Rob Dobson 2021
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "RaftI2CCentralIF.h"
#include "driver/i2c.h"

class BusI2CESPIDF : public RaftI2CCentralIF
{
public:
    BusI2CESPIDF();
    virtual ~BusI2CESPIDF();

    // Init/de-init
    virtual bool init(uint8_t i2cPort, uint16_t pinSDA, uint16_t pinSCL, uint32_t busFrequency, 
                uint32_t busFilteringLevel = RaftI2CCentralIF::DEFAULT_BUS_FILTER_LEVEL) override final;
    virtual void deinit() override final;

    // Busy
    virtual bool isBusy() override final;

    // Access the bus
    virtual RaftRetCode access(uint32_t address, const uint8_t* pWriteBuf, uint32_t numToWrite,
                    uint8_t* pReadBuf, uint32_t numToRead, uint32_t& numRead) override final;

    // Check if bus operating ok
    virtual bool isOperatingOk() const override final;

private:
    // Bus settings
    int _i2cPort = 0;
    i2c_port_t _i2cNum = I2C_NUM_0;
    static const int I2C_TIMEOUT_IN_I2C_BIT_PERIODS = 20;
    // Detection of bus failure
    uint32_t _busLockDetectCount = 0;
    static const uint32_t MAX_BUS_LOCK_DETECT_COUNT = 3;
    static const uint32_t BUS_LOCK_DETECT_MAX_MS = 500;

    // Debug
    static constexpr const char* MODULE_PREFIX = "RaftI2CBusESPIDF";
};
