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
#include "RaftThreading.h"

class BusIOExpander
{
public:
    BusIOExpander(uint32_t addr, uint32_t muxAddr, uint32_t muxChanIdx, int8_t muxResetPin, uint32_t virtualPinBase, uint32_t numPins) : 
        _addr(addr), _muxAddr(muxAddr), _muxChanIdx(muxChanIdx), _muxResetPin(muxResetPin), _virtualPinBase(virtualPinBase), _numVirtualPins(numPins)
    {
        RaftMutex_init(_regMutex);
    }
    
    /// @brief Get virtual pin base
    /// @return virtual pin base
    uint32_t getVirtualPinBase() const
    {
        return _virtualPinBase;
    }

    /// @brief Get number of pins
    /// @return number of pins
    uint32_t getNumPins() const
    {
        return _numVirtualPins;
    }

    /// @brief Check if this is a match to the IO expander address, and mux address and channel
    /// @param i2cAddr address of IO expander
    /// @param muxAddr address of mux (0 if on main I2C bus)
    /// @param muxChanIdx channel on mux
    /// @return true if this is a match
    bool isMatch(uint32_t i2cAddr, uint32_t muxAddr, uint32_t muxChanIdx)
    {
        return (_addr == i2cAddr) && (muxAddr == this->_muxAddr) && (muxChanIdx == this->_muxChanIdx);
    }

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Set virtual pin levels on IO expander (pins must be on the same expander or on GPIO)
    /// @param numPins - number of pins to set
    /// @param pPinNums - array of pin numbers
    /// @param pLevels - array of levels (0 for low)
    /// @param pResultCallback - callback for result when complete/failed
    /// @param pCallbackData - callback data
    /// @return RAFT_OK if successful
    RaftRetCode virtualPinsSet(uint32_t numPins, const int* pPinNums, const uint8_t* pLevels, 
                VirtualPinSetCallbackType pResultCallback, void* pCallbackData);

    /// @brief Get virtual pin level on IO expander
    /// @param pinNum - pin number
    /// @param busI2CReqAsyncFn - function to call to perform I2C request
    /// @param vPinCallback - callback for virtual pin changes
    /// @param pCallbackData - callback data
    /// @return RAFT_OK if successful
    RaftRetCode virtualPinRead(int pinNum, BusReqAsyncFn busI2CReqAsyncFn, 
                VirtualPinReadCallbackType vPinCallback, void* pCallbackData);

    /// @brief Update power control registers for all slots
    /// @param force true to force update (even if not dirty)
    /// @param busI2CReqSyncFn function to call to perform I2C request
    void updateSync(bool force, BusReqSyncFn busI2CReqSyncFn);

    /// @brief Get debug info
    /// @return debug info
    String getDebugStr()
    {
        return Raft::formatString(100, "addr 0x%02x %s vPinBase %d numPins %d ; ", 
            _addr, 
            _muxAddr != 0 ? Raft::formatString(100, "muxAddr 0x%02x muxChanIdx %d", _muxAddr, _muxChanIdx).c_str() : "MAIN_BUS",
            _virtualPinBase, _numVirtualPins);
    }

private:

    // PCA9535 registers
    static const uint8_t PCA9535_INPUT_PORT_0 = 0x00;
    static const uint8_t PCA9535_OUTPUT_PORT_0 = 0x02;
    static const uint8_t PCA9535_CONFIG_PORT_0 = 0x06;

    // Power controller address
    uint8_t _addr = 0;

    // Muliplexer address (0 if connected directly to main I2C bus)
    uint8_t _muxAddr = 0;

    // Multiplexer channel index
    uint8_t _muxChanIdx = 0;

    // Multiplexer reset pin
    int8_t _muxResetPin = -1;

    // virtual pin (an extension of normal microcontroller pin numbering to include expander
    // pins based at this number for this device) start number
    uint16_t _virtualPinBase = 0;

    // Number of pins
    uint16_t _numVirtualPins = 0;

    // Power controller output register
    uint16_t _outputsReg = 0xffff;

    // Power controller config bits record
    uint16_t _configReg = 0xffff;

    // Output data is dirty
    bool _outputsRegDirty = true;

    // Config register is dirty
    bool _configRegDirty = true;

    // Semaphore controlling access to register values
    RaftMutex _regMutex;

    // Max wait for mutex
    static const uint32_t REG_MUTEX_MAX_WAIT_UPDATE_MS = 10;
    static const uint32_t REG_MUTEX_MAX_WAIT_VPIN_MS = 10;

    // Callbacks for set operations
    class VirtualPinSetCallbackInfo
    {
    public:
        VirtualPinSetCallbackType pResultCallback = nullptr;
        void* pCallbackData = nullptr;
    };
    std::vector<VirtualPinSetCallbackInfo> _virtualPinSetCallbacks;

    // Debug
    static constexpr const char* MODULE_PREFIX = "BusIOExpander";
};
