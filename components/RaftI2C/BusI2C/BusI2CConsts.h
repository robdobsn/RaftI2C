/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// I2C Bus Constants
//
// Rob Dobson 2024
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <stdint.h>
#include "RaftArduino.h"

// I2C implementation selection:
// I2C_USE_RAFT_I2C - Use custom RAFT I2C implementation (default for ESP32, S3, C3)
// I2C_USE_ESP_IDF_5 - Use ESP-IDF 5.2+ I2C master driver (default for C6)
// These can be overridden by defining them in the build system

// I2C addresses
static const uint32_t I2C_BUS_ADDRESS_MIN = 4;
static const uint32_t I2C_BUS_ADDRESS_MAX = 0x77;
static const uint32_t I2C_BUS_MUX_BASE_DEFAULT = 0x70;
static const uint32_t I2C_BUS_MUX_MAX = 8;
