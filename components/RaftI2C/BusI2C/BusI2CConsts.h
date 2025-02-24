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

// Use replacement I2C library - if not defined use original ESP IDF I2C implementation
#define I2C_USE_RAFT_I2C
// #define I2C_USE_ESP_IDF_5

// I2C addresses
static const uint32_t I2C_BUS_ADDRESS_MIN = 4;
static const uint32_t I2C_BUS_ADDRESS_MAX = 0x77;
static const uint32_t I2C_BUS_MUX_BASE_DEFAULT = 0x70;
static const uint32_t I2C_BUS_MUX_MAX = 8;
