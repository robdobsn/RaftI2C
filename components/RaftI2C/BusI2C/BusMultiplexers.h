/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// I2C Bus Multiplexers
//
// Rob Dobson 2024
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "BusI2CRequestRec.h"
#include "BusPowerController.h"
#include "BusStuckHandler.h"
#include "BusStatusMgr.h"
#include "RaftJsonIF.h"
#include "driver/gpio.h"

class BusMultiplexers
{
public:
    // Constructor and destructor
    BusMultiplexers(BusPowerController& busPowerController, BusStuckHandler& busStuckHandler, 
            BusStatusMgr& busStatusMgr, BusI2CReqSyncFn busI2CReqSyncFn);
    virtual ~BusMultiplexers();

    // Setup
    void setup(const RaftJsonIF& config);

    // Service
    void loop();

    // Service called from I2C task
    void taskService();
    
    // State change on an element (may or may not be a bus mux)
    void elemStateChange(uint32_t addr, bool elemResponding);

    // Check if address is a bus mux
    bool isBusMultiplexer(uint8_t addr)
    {
        return _isEnabled && (addr >= _minAddr) && (addr <= _maxAddr);
    }

    // Get min address of bus mux
    uint32_t getMinAddr() const
    {
        return _minAddr;
    }

    // Get max address of bus mux
    uint32_t getMaxAddr() const
    {
        return _maxAddr;
    }

    // Get list of active muliplexer addresses
    void getActiveMuxAddrs(std::vector<uint32_t>& activeMuxAddrs)
    {
        activeMuxAddrs.clear();
        uint32_t addr = _minAddr;
        for (BusMux& busMux : _busMuxRecs)
        {
            if (busMux.isOnline)
                activeMuxAddrs.push_back(addr);
            addr++;
        }
    }

    /// @brief Enable one slot on bus multiplexers(s)
    /// @param slotNum Slot number (1-based)
    /// @return OK if successful, otherwise error code which may be invalid if the slotNum doesn't exist or one of
    ///         the bus stuck codes if the bus is now stuck
    RaftI2CCentralIF::AccessResultCode enableOneSlot(uint32_t slotNum);

    /// @brief Disable all slots on bus multiplexers
    /// @param force Force disable even if the status indicates it is not necessary
    void disableAllSlots(bool force);

    /// @brief Get bus multiplexer slot indices
    /// @return Valid indices of bus multiplexer slots
    const std::vector<uint8_t>& getSlotIndices() const
    {
        return _busMuxSlotIndices;
    }

    /// @brief Get next slot
    /// @param slotNum Slot number (1-based)
    /// @note this is used when scanning to work through all slots and then loop back to 0 (main bus)
    /// @return Next slot number (1-based)
    uint32_t getNextSlotNum(uint32_t slotNum);

    // Bus mux slot count
    static const uint32_t I2C_BUS_MUX_SLOT_COUNT = 8;

    // Bus mux channels
    static const uint32_t I2C_BUS_MUX_ALL_CHANS_OFF = 0;
    static const uint32_t I2C_BUS_MUX_ALL_CHANS_ON = 0xff;

    // Number of times to retry bus stuck recovery
    static const uint32_t BUS_CLEAR_ATTEMPT_REPEAT_COUNT = 5;

private:
    // Multiplexer functionality enabled
    bool _isEnabled = true;

    // Bus power controller
    BusPowerController& _busPowerController;

    // Bus stuck handler
    BusStuckHandler& _busStuckHandler;

    // Bus status manager
    BusStatusMgr& _busStatusMgr;

    // Bus access function
    BusI2CReqSyncFn _busI2CReqSyncFn;

    // Bus mux address range
    uint32_t _minAddr = I2C_BUS_MUX_BASE;
    uint32_t _maxAddr = I2C_BUS_MUX_BASE+I2C_BUS_MUX_MAX-1;

    // Bus mux reset pin(s)
    gpio_num_t _resetPin = GPIO_NUM_NC;
    gpio_num_t _resetPinAlt = GPIO_NUM_NC;

    // Bus multiplexer record
    class BusMux
    {
    public:
        // Flags
        // isDetected:1 - mux has been detected
        bool isDetected:1 = false,

        // isOnline:1 - mux is online
             isOnline:1 = false,

        // maskWrittenOk:1 - Current bit mask has been written to the mux
             maskWrittenOk:1 = false;

        // Current bit mask (each bit enables a slot when 1)
        uint32_t curBitMask = 0;
    };

    // Bus multiplexer records
    std::vector<BusMux> _busMuxRecs;

    // Bus mux slot indices (that have been found on discovered bus multiplexers)
    std::vector<uint8_t> _busMuxSlotIndices;

    // Init bus mux records
    void initBusMuxRecs();

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get muliplexer and slot index from slot number
    /// @param slotNum Slot number (1-based)
    /// @param muxIdx Multiplexer index
    /// @param slotIdx Slot index
    /// @return True if valid
    bool getMuxAndSlotIdx(uint32_t slotNum, uint32_t& muxIdx, uint32_t& slotIdx);

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Set slot enables on mux
    /// @param muxIdx Multiplexer index
    /// @param slotMask Slot mask
    /// @param force Force enable/disable (even if status indicates it is not necessary)
    /// @return Result code
    RaftI2CCentralIF::AccessResultCode setSlotEnables(uint32_t muxIdx, uint32_t slotMask, bool force);

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Attempt to clear bus stuck problem
    /// @param failAfterSlotSet Bus stuck after setting slot (so an individual slot may be stuck)
    /// @param slotNum Slot number (1-based) (valid if after slot set)
    /// @return True if slot setting is still valid
    bool attemptToClearBusStuck(bool failAfterSlotSet, uint32_t slotNum);
};
