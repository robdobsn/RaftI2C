/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// I2C Bus Extender Manager
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

class BusExtenderMgr
{
public:
    // Constructor and destructor
    BusExtenderMgr(BusPowerController& busPowerController, BusStuckHandler& busStuckHandler, 
            BusStatusMgr& busStatusMgr, BusI2CReqSyncFn busI2CReqSyncFn);
    virtual ~BusExtenderMgr();

    // Setup
    void setup(const RaftJsonIF& config);

    // Service
    void loop();

    // Service called from I2C task
    void taskService();
    
    // State change on an element (may or may not be a bus extender)
    void elemStateChange(uint32_t addr, bool elemResponding);

    // Check if address is a bus extender
    bool isBusExtender(uint8_t addr)
    {
        return _isEnabled && (addr >= _minAddr) && (addr <= _maxAddr);
    }

    // Get count of bus extenders
    uint32_t getBusExtenderCount() const
    {
        return _busExtenderCount;
    }

    // Get min address of bus extenders
    uint32_t getMinAddr() const
    {
        return _minAddr;
    }

    // Get max address of bus extenders
    uint32_t getMaxAddr() const
    {
        return _maxAddr;
    }

    // Get list of active extender addresses
    void getActiveExtenderAddrs(std::vector<uint32_t>& activeExtenderAddrs)
    {
        activeExtenderAddrs.clear();
        uint32_t addr = _minAddr;
        for (BusExtender& busExtender : _busExtenderRecs)
        {
            if (busExtender.isOnline)
                activeExtenderAddrs.push_back(addr);
            addr++;
        }
    }

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Enable one slot on bus extender(s)
    /// @param slotPlus1 Slot number (1-based)
    /// @return OK if successful, otherwise error code which may be invalid if the slotPlus1 doesn't exist or one of
    ///         the bus stuck codes if the bus is now stuck
    RaftI2CCentralIF::AccessResultCode enableOneSlot(uint32_t slotPlus1);

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Disable all slots on bus extenders
    /// @param force Force disable even if the status indicates it is not necessary
    void disableAllSlots(bool force);

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get bus extender slot indices
    /// @return Valid indices of bus multiplexer slots
    const std::vector<uint8_t>& getSlotIndices() const
    {
        return _busExtenderSlotIndices;
    }

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get next slot
    /// @param slotPlus1 Slot number (1-based)
    /// @note this is used when scanning to work through all slots and then loop back to 0 (main bus)
    /// @return Next slot number (1-based)
    uint32_t getNextSlotNum(uint32_t slotPlus1);

    // Bus extender slot count
    static const uint32_t I2C_BUS_EXTENDER_SLOT_COUNT = 8;

    // Bus extender channels
    static const uint32_t I2C_BUS_EXTENDER_ALL_CHANS_OFF = 0;
    static const uint32_t I2C_BUS_EXTENDER_ALL_CHANS_ON = 0xff;

    // Number of times to retry bus stuck recovery
    static const uint32_t BUS_CLEAR_ATTEMPT_REPEAT_COUNT = 5;

private:
    // Extender functionality enabled
    bool _isEnabled = true;

    // Bus power controller
    BusPowerController& _busPowerController;

    // Bus stuck handler
    BusStuckHandler& _busStuckHandler;

    // Bus status manager
    BusStatusMgr& _busStatusMgr;

    // Bus access function
    BusI2CReqSyncFn _busI2CReqSyncFn;

    // Bus extender address range
    uint32_t _minAddr = I2C_BUS_EXTENDER_BASE;
    uint32_t _maxAddr = I2C_BUS_EXTENDER_BASE+I2C_BUS_EXTENDERS_MAX-1;

    // Bus extender reset pin(s)
    gpio_num_t _resetPin = GPIO_NUM_NC;
    gpio_num_t _resetPinAlt = GPIO_NUM_NC;

    // Bus extender record
    class BusExtender
    {
    public:
        // Flags
        // isDetected:1 - Extender has been detected
        bool isDetected:1 = false,

        // isOnline:1 - Extender is online
             isOnline:1 = false,

        // maskWrittenOk:1 - Current bit mask has been written to the extender
             maskWrittenOk:1 = false;

        // Current bit mask (each bit enables a slot when 1)
        uint32_t curBitMask = 0;
    };

    // Bus extenders
    std::vector<BusExtender> _busExtenderRecs;

    // Number of bus extenders detected so far
    uint8_t _busExtenderCount = 0;

    // Bus extender slot indices (that have been found on discovered bus multiplexers)
    std::vector<uint8_t> _busExtenderSlotIndices;

    // Init bus extender records
    void initBusExtenderRecs();

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get extender and slot index from slotPlus1
    /// @param slotPlus1 Slot number (1-based)
    /// @param extenderIdx Extender index
    /// @param slotIdx Slot index
    /// @return True if valid
    bool getExtenderAndSlotIdx(uint32_t slotPlus1, uint32_t& extenderIdx, uint32_t& slotIdx);

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Set slot enables on extender
    /// @param extenderIdx Extender index
    /// @param slotMask Slot mask
    /// @param force Force enable/disable (even if status indicates it is not necessary)
    /// @return Result code
    RaftI2CCentralIF::AccessResultCode setSlotEnables(uint32_t extenderIdx, uint32_t slotMask, bool force);

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Attempt to clear bus stuck problem
    /// @param failAfterSlotSet Bus stuck after setting slot (so an individual slot may be stuck)
    /// @param slotPlus1 Slot number (1-based) (valid if after slot set)
    /// @return True if slot setting is still valid
    bool attemptToClearBusStuck(bool failAfterSlotSet, uint32_t slotPlus1);
};
