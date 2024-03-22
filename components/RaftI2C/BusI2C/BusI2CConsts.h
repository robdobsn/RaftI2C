# pragma once

#include <stdint.h>

// Use replacement I2C library - if not defined use original ESP IDF I2C implementation
#define I2C_USE_RAFT_I2C

// I2C addresses
static const uint32_t I2C_BUS_ADDRESS_MIN = 4;
static const uint32_t I2C_BUS_ADDRESS_MAX = 0x77;

