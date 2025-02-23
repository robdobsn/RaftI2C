/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// I2C Bus IO Expanders
//
// Rob Dobson 2025
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <stdint.h>
#include <vector>
#include "RaftJsonIF.h"
#include "BusRequestInfo.h"
#include "VirtualPinResult.h"
#include "BusIOExpander.h"

/// @brief Bus IO Expander
class BusIOExpanders
{
public:
    // Constructor and destructor
    BusIOExpanders();
    ~BusIOExpanders();

    // Setup
    void setup(const RaftJsonIF& config);

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

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get virtual pin level on IO expander
    /// @param pinNum - pin number
    /// @param busI2CReqAsyncFn Function to call to perform I2C request
    /// @param vPinCallback - callback for virtual pin changes
    /// @param pCallbackData - callback data
    /// @return RAFT_OK if successful
    RaftRetCode virtualPinRead(int pinNum, BusReqAsyncFn busI2CReqAsyncFn, 
            VirtualPinReadCallbackType vPinCallback, void* pCallbackData);

    /// @brief Sync state changes in I2C IO expanders
    /// @param force true to force action
    void syncI2CIOStateChanges(bool force, BusReqSyncFn busI2CReqSyncFn);

    /// @brief Check if address is an IO expander
    /// @param i2cAddr address of IO expander
    /// @param muxAddr address of mux (0 if on main I2C bus)
    /// @param muxChanIdx channel on mux
    /// @return true if address is an IO expander
    bool isIOExpander(uint16_t i2cAddr, uint32_t muxAddr = 0, uint32_t muxChanIdx = 0)
    {
        for (BusIOExpander& ioExpander : _ioExpanders)
        {
            if (ioExpander.isMatch(i2cAddr, muxAddr, muxChanIdx))
                return true;
        }
        return false;
    }

private:

    // Max pins on an IO expander
    static const uint32_t IO_EXPANDER_MAX_PINS = 16;

    // IO exander records
    std::vector<BusIOExpander> _ioExpanders;

    /// @brief Find IO expander for a virtual pin
    /// @param vPin virtual pin number
    /// @return IO expander record or nullptr if not found
    BusIOExpander* findIOExpanderFromVPin(uint32_t vPin)
    {
        for (uint32_t i = 0; i < _ioExpanders.size(); i++)
        {
            int virtualPinBase = _ioExpanders[i].getVirtualPinBase();
            if (vPin >= virtualPinBase && 
                vPin < virtualPinBase + _ioExpanders[i].getNumPins())
            {
                return &_ioExpanders[i];
            }
        }
        return nullptr;
    }

    // Debug
    static constexpr const char* MODULE_PREFIX = "RaftI2CIOExps";    
};
