/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// I2C Bus IO Expander
//
// Rob Dobson 2025
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <stdint.h>
#include "RaftUtils.h"
#include "VirtualPinResult.h"
#include "BusRequestInfo.h"

#define DEBUG_IO_EXPANDER_

class BusIOExpander
{
public:
    BusIOExpander(uint32_t addr, uint32_t muxAddr, uint32_t muxChanIdx, int8_t muxResetPin, uint32_t virtualPinBase, uint32_t numPins) : 
        addr(addr), muxAddr(muxAddr), muxChanIdx(muxChanIdx), muxResetPin(muxResetPin), virtualPinBase(virtualPinBase), numPins(numPins)
    {
    }

    /// @brief Set to use async I2C requests
    /// @param useAsync true to use async requests
    void setUseAsyncI2C(bool useAsync)
    {
        useAsyncI2C = useAsync;
    }
    
    /// @brief Get virtual pin base
    /// @return virtual pin base
    uint32_t getVirtualPinBase() const
    {
        return virtualPinBase;
    }

    /// @brief Get number of pins
    /// @return number of pins
    uint32_t getNumPins() const
    {
        return numPins;
    }

    /// @brief Check if this is a match to the IO expander address, and mux address and channel
    /// @param i2cAddr address of IO expander
    /// @param muxAddr address of mux (0 if on main I2C bus)
    /// @param muxChanIdx channel on mux
    /// @return true if this is a match
    bool isMatch(uint32_t i2cAddr, uint32_t muxAddr, uint32_t muxChanIdx)
    {
        return (addr == i2cAddr) && (muxAddr == this->muxAddr) && (muxChanIdx == this->muxChanIdx);
    }

    /// @brief Set virtual pin mode on IO expander
    /// @param pinNum - pin number
    /// @param mode - mode (INPUT or OUTPUT)
    /// @param level - true for high, false for low
    void virtualPinMode(int pinNum, uint8_t mode, bool level);

    /// @brief Set virtual pin level on IO expander
    /// @param pinNum - pin number
    /// @param level - true for on, false for off
    void virtualPinWrite(int pinNum, bool level);

    /// @brief Get virtual pin level on IO expander
    /// @param pinNum - pin number
    /// @param busI2CReqAsyncFn - function to call to perform I2C request
    /// @param vPinCallback - callback for virtual pin changes
    /// @param pCallbackData - callback data
    void virtualPinRead(int pinNum, BusReqAsyncFn busI2CReqAsyncFn, VirtualPinCallbackType vPinCallback, void* pCallbackData);

    /// @brief Update power control registers for all slots
    /// @param force true to force update (even if not dirty)
    /// @param busI2CReqSyncFn function to call to perform I2C request
    void updateSync(bool force, BusReqSyncFn busI2CReqSyncFn);

    /// @brief Update power control registers for all slots
    /// @param force true to force update (even if not dirty)
    /// @param busI2CReqSyncFn function to call to perform I2C request
    /// @param vPinCallback callback for virtual pin changes
    /// @param pCallbackData callback data
    void updateAsync(bool force, BusReqAsyncFn busI2CReqAsyncFn, VirtualPinCallbackType vPinCallback, void *pCallbackData);

    /// @brief Get debug info
    /// @return debug info
    String getDebugStr()
    {
        return Raft::formatString(100, "addr 0x%02x %s vPinBase %d numPins %d ; ", 
            addr, 
            muxAddr != 0 ? Raft::formatString(100, "muxAddr 0x%02x muxChanIdx %d", muxAddr, muxChanIdx).c_str() : "MAIN_BUS",
            virtualPinBase, numPins);
    }

private:

    // PCA9535 registers
    static const uint8_t PCA9535_INPUT_PORT_0 = 0x00;
    static const uint8_t PCA9535_OUTPUT_PORT_0 = 0x02;
    static const uint8_t PCA9535_CONFIG_PORT_0 = 0x06;

    // Power controller address
    uint8_t addr = 0;

    // Muliplexer address (0 if connected directly to main I2C bus)
    uint8_t muxAddr = 0;

    // Multiplexer channel index
    uint8_t muxChanIdx = 0;

    // Multiplexer reset pin
    int8_t muxResetPin = -1;

    // virtual pin (an extension of normal microcontroller pin numbering to include expander
    // pins based at this number for this device) start number
    uint16_t virtualPinBase = 0;

    // Number of pins
    uint16_t numPins = 0;

    // Power controller output register
    uint16_t outputsReg = 0xffff;

    // Power controller config bits record
    uint16_t configReg = 0xffff;

    // Output data is dirty
    bool outputsRegDirty = true;

    // Config register is dirty
    bool configRegDirty = true;

    // Use Async I2C requests
    bool useAsyncI2C = true;

    // Debug
    static constexpr const char* MODULE_PREFIX = "BusIOExpander";
};
