/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// ESPIDF5I2CCentral.h
// I2C Central using ESP IDF 5.2+
//
// Rob Dobson 2024
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "RaftI2CCentralIF.h"
#include "RaftUtils.h"
#include "sdkconfig.h"
#include "driver/i2c_master.h"

class ESPIDF5I2CCentral : public RaftI2CCentralIF
{
public:
    ESPIDF5I2CCentral();
    virtual ~ESPIDF5I2CCentral();

    // Init/de-init
    virtual bool init(uint8_t i2cPort, uint16_t pinSDA, uint16_t pinSCL, uint32_t busFrequency, 
                uint32_t busFilteringLevel = DEFAULT_BUS_FILTER_LEVEL) override final;
    virtual void deinit() override final;

    // Busy
    virtual bool isBusy() override final;

    // Access the bus
    virtual AccessResultCode access(uint32_t address, const uint8_t* pWriteBuf, uint32_t numToWrite,
                    uint8_t* pReadBuf, uint32_t numToRead, uint32_t& numRead) override final;

    // Check if bus operating ok
    virtual bool isOperatingOk() const override final;
     
private:
    // Settings
    uint8_t _i2cPort = 0;
    int16_t _pinSDA = -1;
    int16_t _pinSCL = -1;
    uint32_t _busFrequency = 100000;
    uint32_t _busFilteringLevel = DEFAULT_BUS_FILTER_LEVEL;

    // Init flag
    bool _isInitialised = false;

    // I2C master bus handle
    i2c_master_bus_handle_t _i2cMasterBusHandle;

    // I2C device handles
    class I2CAddrAndHandle
    {
    public:
        uint32_t address;
        i2c_master_dev_handle_t handle;
    };

    // Device handles
    std::vector<I2CAddrAndHandle> _deviceAddrHandles;
};
