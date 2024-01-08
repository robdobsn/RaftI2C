/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Original ESP IDF I2C Device Interface
//
// Rob Dobson 2021
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "Logger.h"
#include "RaftUtils.h"
#include "BusI2CESPIDF.h"
#include "RaftArduino.h"

static const char* MODULE_PREFIX = "BusI2CESPIDF";

BusI2CESPIDF::BusI2CESPIDF()
{
    _busLockDetectCount = 0;
}
BusI2CESPIDF::~BusI2CESPIDF()
{
}

// Init/de-init
bool BusI2CESPIDF::init(uint8_t i2cPort, uint16_t pinSDA, uint16_t pinSCL, uint32_t busFrequency, 
            uint32_t busFilteringLevel)
{
    _i2cPort = i2cPort;
    i2c_config_t conf;
    conf.mode = I2C_MODE_MASTER;
    conf.sda_io_num = (gpio_num_t) pinSDA;
    conf.sda_pullup_en = GPIO_PULLUP_DISABLE;
    conf.scl_io_num = (gpio_num_t) pinSCL;
    conf.scl_pullup_en = GPIO_PULLUP_DISABLE;
    conf.master.clk_speed = busFrequency;
    conf.clk_flags = 0;
    _i2cNum = (i2cPort == 0) ? I2C_NUM_0 : 
#if defined(CONFIG_IDF_TARGET_ESP32C3)
    I2C_NUM_0;
#else
    I2C_NUM_1;
#endif    
    esp_err_t err = i2c_param_config(_i2cNum, &conf);
    if (err != ESP_OK)
    {
        LOG_W(MODULE_PREFIX, "param_config param error");
        return false;
    }
    err = i2c_driver_install(_i2cNum, conf.mode, 0, 0, 0);
    if (err != ESP_OK)
    {
        LOG_W(MODULE_PREFIX, "driver_install fail %s", err == ESP_FAIL ? "DRIVER FAIL" : "param error");
    }
#if defined(CONFIG_IDF_TARGET_ESP32C3)
    int timeout = 16;
#else
    int timeout = I2C_TIMEOUT_IN_I2C_BIT_PERIODS * (APB_CLK_FREQ / busFrequency);
#endif
    err = i2c_set_timeout(_i2cNum, timeout);
    if (err != ESP_OK)
    {
        LOG_W(MODULE_PREFIX, "set_timeout fail %s timeout %d err %d", 
                    err == ESP_FAIL ? "DRIVER FAIL" : "param error", timeout, err);
    }
    return true;
}

void BusI2CESPIDF::deinit()
{
    i2c_driver_delete(_i2cNum);
}

// Busy
bool BusI2CESPIDF::isBusy()
{
    return false;
}

// Access the bus
RaftI2CCentralIF::AccessResultCode BusI2CESPIDF::access(uint16_t address, uint8_t* pWriteBuf, uint32_t numToWrite,
                uint8_t* pReadBuf, uint32_t numToRead, uint32_t& numRead)
{
    // Send the command
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    bool addrSent = false;
    numRead = 0;

    // In addition to checking write length and buffer are ok, if both write and read lengths are zero then do a zero length write
    if (((numToWrite > 0) && (pWriteBuf)) ||
            ((numToWrite == 0) && (numToRead == 0)))
    {
        uint8_t dummyData = 0;
        addrSent = true;
        i2c_master_write_byte(cmd, (address << 1) | I2C_MASTER_WRITE, true);
        i2c_master_write(cmd, pWriteBuf ? pWriteBuf : &dummyData, numToWrite, true);
    }
    if (numToRead > 0)
    {
        if (addrSent)
            i2c_master_start(cmd);
        i2c_master_write_byte(cmd, (address << 1) | I2C_MASTER_READ, true);
        esp_err_t readRslt = i2c_master_read(cmd, pReadBuf, numToRead, I2C_MASTER_LAST_NACK);
        if (readRslt == ESP_OK)
            numRead = numToRead;
    }
    i2c_master_stop(cmd);
    uint32_t reqStartMs = millis();
    int ret = i2c_master_cmd_begin(_i2cNum, cmd, pdMS_TO_TICKS(10));

    i2c_cmd_link_delete(cmd);

    // Detect bus action taking too long
    if (Raft::isTimeout(millis(), reqStartMs, BUS_LOCK_DETECT_MAX_MS))
    {
        LOG_W(MODULE_PREFIX, "sendHelper send too slow addr %02x %ldms lockDetectCount %d", 
                address, millis() - reqStartMs, _busLockDetectCount);
        if (_busLockDetectCount < MAX_BUS_LOCK_DETECT_COUNT)
            _busLockDetectCount++;
    }
    else
    {
        _busLockDetectCount = 0;
    }
    return ret == ESP_OK ? RaftI2CCentralIF::ACCESS_RESULT_OK : RaftI2CCentralIF::ACCESS_RESULT_SW_TIME_OUT;
}

// Check if bus operating ok
bool BusI2CESPIDF::isOperatingOk() const
{
    return _busLockDetectCount < MAX_BUS_LOCK_DETECT_COUNT;
}
