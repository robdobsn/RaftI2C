/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// ESPIDF5I2CCentral
// I2C Central using ESP IDF 5.2+
//
// Rob Dobson 2024
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "ESPIDF5I2CCentral.h"
#include "Logger.h"
#include "RaftUtils.h"
#include "RaftArduino.h"
#include "sdkconfig.h"

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Consts
static const char *MODULE_PREFIX = "ESPIDF5I2CCentral";

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Constructor
ESPIDF5I2CCentral::ESPIDF5I2CCentral()
{
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Destructor
ESPIDF5I2CCentral::~ESPIDF5I2CCentral()
{
    // De-init
    deinit();
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Initialisation function
/// @param i2cPort - I2C port number
/// @param pinSDA - SDA pin number
/// @param pinSCL - SCL pin number
/// @param busFrequency - bus frequency
/// @param busFilteringLevel - bus filtering level (see ESP-IDF i2c_config_t)
bool ESPIDF5I2CCentral::init(uint8_t i2cPort, uint16_t pinSDA, uint16_t pinSCL, uint32_t busFrequency,
                          uint32_t busFilteringLevel)
{
    // de-init first in case already initialised
    deinit();

    // Settings
    _i2cPort = i2cPort;
    _pinSDA = pinSDA;
    _pinSCL = pinSCL;
    _busFrequency = busFrequency;
    _busFilteringLevel = busFilteringLevel;

    // I2C mater config
    i2c_master_bus_config_t i2c_mst_config = {
        .i2c_port = i2cPort,
        .sda_io_num = (gpio_num_t)pinSDA,
        .scl_io_num = (gpio_num_t)pinSCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .intr_priority = 0,
        .trans_queue_depth = 0,
        .flags = {
            .enable_internal_pullup = true,
        }
    };

    // Create I2C master bus
    if (i2c_new_master_bus(&i2c_mst_config, &_i2cMasterBusHandle) != ESP_OK)
    {
        LOG_E(MODULE_PREFIX, "Failed to create I2C master bus");
        return false;
    }

    // Set initialisation flag
    _isInitialised = true;

    return true;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief De-initialisation function
void ESPIDF5I2CCentral::deinit()
{
    if (_isInitialised)
    {
        // Delete I2C master bus
        i2c_del_master_bus(_i2cMasterBusHandle);
        _isInitialised = false;
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Check if bus is busy
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool ESPIDF5I2CCentral::isBusy()
{
    // Check if hardware is ready
    if (!_isInitialised)
        return true;

    // TODO - implement
    return false;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Check if bus operating ok
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool ESPIDF5I2CCentral::isOperatingOk() const
{
    return _isInitialised;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Access the I2C bus
// Note:
// - a zero length read and zero length write sends address with R/W flag indicating write to test if a node ACKs
// - a write of non-zero length alone does what it says and can be of arbitrary length
// - a read on non-zero length also can be of arbitrary length
// - a write of non-zero length and read of non-zero length is allowed - write occurs first and can only be of max 14 bytes
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

RaftI2CCentralIF::AccessResultCode ESPIDF5I2CCentral::access(uint32_t address, const uint8_t *pWriteBuf, uint32_t numToWrite,
                                                          uint8_t *pReadBuf, uint32_t numToRead, uint32_t &numRead)
{
    // Check valid
    if (!_isInitialised)
        return ACCESS_RESULT_INVALID;
    if ((numToWrite > 0) && !pWriteBuf)
        return ACCESS_RESULT_INVALID;
    if ((numToRead > 0) && !pReadBuf)
        return ACCESS_RESULT_INVALID;

    // Check for scan (probe) operation
    if ((numToWrite == 0) && (numToRead == 0))
    {
        esp_err_t err = i2c_master_probe(_i2cMasterBusHandle, address, 2);
        switch (err)
        {
            case ESP_OK:
            {
                // LOG_I(MODULE_PREFIX, "access probe address 0x%02x OK", address);
                return ACCESS_RESULT_OK;
            }
            case ESP_ERR_NOT_FOUND:
            {
                // LOG_I(MODULE_PREFIX, "access probe address 0x%02x NOT FOUND", address);
                return ACCESS_RESULT_ACK_ERROR;
            }
            default:
            {
                LOG_I(MODULE_PREFIX, "access probe address 0x%02x OTHER %d", address, err);
                return ACCESS_RESULT_HW_TIME_OUT;
            }
        }
    }

    // Check if device is already added to the bus
    i2c_master_dev_handle_t devHandle = nullptr;
    for (const auto& devAddrHandle : _deviceAddrHandles)
    {
        if (devAddrHandle.address == address)
        {
            devHandle = devAddrHandle.handle;
            break;
        }
    }

    // If not found then add the device
    if (devHandle == nullptr)
    {
        i2c_device_config_t dev_cfg = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = (uint16_t)address,
            .scl_speed_hz = _busFrequency,
        };
        if (i2c_master_bus_add_device(_i2cMasterBusHandle, &dev_cfg, &devHandle) != ESP_OK)
        {
            LOG_E(MODULE_PREFIX, "access failed to create I2C device handle address 0x%02x", address);
            return ACCESS_RESULT_NOT_INIT;
        }
        LOG_I(MODULE_PREFIX, "access adding device address 0x%02x devHandle %p", address, devHandle);
        _deviceAddrHandles.push_back({address, devHandle});
    }

    // LOG_I(MODULE_PREFIX, "access addr 0x%02x numToWrite %d numToRead %d deviceHandle %p", address, numToWrite, numToRead, devHandle);

    // Check for write only
    if (numToWrite > 0 && numToRead == 0)
    {
        // Write
        if (i2c_master_transmit(devHandle, pWriteBuf, numToWrite, 10) != ESP_OK)
        {
            LOG_W(MODULE_PREFIX, "access FAILED to TX data addr 0x%02x numToWrite %d", address, numToWrite);
            return ACCESS_RESULT_ACK_ERROR;
        }
        // LOG_I(MODULE_PREFIX, "access transmit OK addr 0x%02x numToWrite %d", address, numToWrite);
        return ACCESS_RESULT_OK;
    }

    // Check for read only
    if (numToRead > 0 && numToWrite == 0)
    {
        // Write
        if (i2c_master_receive(devHandle, pReadBuf, numToRead, 10) != ESP_OK)
        {
            LOG_W(MODULE_PREFIX, "access failed to FAILED RX data addr 0x%02x numToRead %d", address, numToRead);
            return ACCESS_RESULT_ACK_ERROR;
        }
        numRead = numToRead;
        return ACCESS_RESULT_OK;
    }


    // Write and read
    if (i2c_master_transmit_receive(devHandle, pWriteBuf, numToWrite, pReadBuf, numToRead, 10) != ESP_OK)
    {
        LOG_W(MODULE_PREFIX, "access FAILED TX/RX data addr 0x%02x numToWrite %d numToRead %d", address, numToWrite, numToRead);
        return ACCESS_RESULT_ACK_ERROR;
    }
    numRead = numToRead;
    return ACCESS_RESULT_OK;
}

// /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// // Check that the I2C module is ready and reset it if not
// /////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// bool ESPIDF5I2CCentral::ensureI2CReady()
// {
//     // Check if busy
//     if (isBusy() && Raft::isTimeout(millis(), _lastCheckI2CReadyMs, _lastCheckI2CReadyIntervalMs))
//     {
// #ifdef WARN_ON_BUS_IS_BUSY
//         LOG_W(MODULE_PREFIX, "ensureI2CReady bus is busy ... resetting\n");
// #endif

//         // Other checks on I2C should be delayed more
//         _lastCheckI2CReadyIntervalMs = I2C_READY_CHECK_INTERVAL_OTHER_MS;
//         _lastCheckI2CReadyMs = millis();

//         // TODO - implement

//         // Check if now not busy
//         if (isBusy())
//         {
//             // Something more seriously wrong
// #ifdef WARN_ON_BUS_CANNOT_BE_RESET
//             String busLinesErrorMsg;
//             checkI2CLinesOk(busLinesErrorMsg);
//             LOG_W(MODULE_PREFIX, "ensureI2CReady bus still busy ... %s\n", busLinesErrorMsg.c_str());
// #endif
//             return false;
//         }
//     }
//     return true;
// }

