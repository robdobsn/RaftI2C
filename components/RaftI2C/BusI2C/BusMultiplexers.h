/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// I2C Bus Multiplexers
//
// Rob Dobson 2024
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "BusPowerController.h"
#include "BusStuckHandler.h"
#include "BusStatusMgr.h"
#include "BusI2CElemTracker.h"
#include "RaftJsonIF.h"
#include "driver/gpio.h"

class BusMultiplexers
{
public:
    // Constructor and destructor
    BusMultiplexers(BusStuckHandler& busStuckHandler, 
        BusStatusMgr& busStatusMgr, 
        BusI2CElemTracker& busElemTracker, 
        BusReqSyncFn busI2CReqSyncFn);
    virtual ~BusMultiplexers();

    // Setup
    void setup(const RaftJsonIF& config);

    // Service
    void loop();

    // Service called from I2C task
    void taskService();
    
    /// @brief Handle state change on an element
    /// @param addr Address of element
    /// @param slotNum Slot number (1-based)
    /// @param elemResponding True if element is responding
    /// @return True if a change was detected (e.g. new mux or change in online/offline status)
    bool elemStateChange(BusElemAddrType addr, uint32_t slotNum, bool elemResponding);

    // Check if address is a bus mux
    bool isBusMultiplexer(BusElemAddrType addr)
    {
        return _isEnabled && (addr >= _minAddr) && (addr <= _maxAddr);
    }

    // Check if the slot is correct for mux address
    bool isSlotCorrect(BusElemAddrType addr, uint32_t slotNum)
    {
        // Check it is a mux
        if ((addr < _minAddr) || (addr > _maxAddr))
            return true;

        // Check that the mux connection slot is the one being scanned
        uint32_t connSlotNum = _busMuxRecs[addr - _minAddr].muxConnSlotNum;
        return slotNum == connSlotNum;
    }

    // Get min address of bus mux
    BusElemAddrType getMinAddr() const
    {
        return _minAddr;
    }

    // Get max address of bus mux
    BusElemAddrType getMaxAddr() const
    {
        return _maxAddr;
    }

    // Get list of active muliplexer addresses
    void getActiveMuxAddrs(std::vector<BusElemAddrType>& activeMuxAddrs)
    {
        activeMuxAddrs.clear();
        BusElemAddrType addr = _minAddr;
        for (BusMux& busMux : _busMuxRecs)
        {
            if (busMux.isOnline)
                activeMuxAddrs.push_back(addr);
            addr++;
        }
    }

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get muliplexer and slot index from slot number
    /// @param slotNum Slot number (1-based)
    /// @param muxIdx Multiplexer index
    /// @param slotIdx Slot index
    /// @return True if valid
    bool getMuxAndSlotIdx(uint32_t slotNum, uint32_t& muxIdx, uint32_t& slotIdx);
    
    /// @brief Get address based on mux record
    /// @param muxIdx Multiplexer index
    /// @return Address
    BusElemAddrType getAddrFromMuxIdx(uint32_t muxIdx)
    {
        if (muxIdx >= _busMuxRecs.size())
            return 0;
        return _minAddr + muxIdx;
    }

    /// @brief Enable one slot on bus multiplexers(s)
    /// @param slotNum Slot number (1-based)
    /// @return OK if successful, otherwise error code which may be invalid if the slotNum doesn't exist or one of
    ///         the bus stuck codes if the bus is now stuck
    RaftRetCode enableOneSlot(uint32_t slotNum);

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

    /// @brief Set handler for bus power management
    /// @param pBusPowerController - bus power controller
    void setBusPowerController(BusPowerController* pBusPowerController)
    {
        _pBusPowerController = pBusPowerController;
    }

private:

    // Bus mux slot count
    static const uint32_t I2C_BUS_MUX_SLOT_COUNT = 8;

    // Bus mux channels
    static const uint32_t I2C_BUS_MUX_ALL_CHANS_OFF = 0;
    static const uint32_t I2C_BUS_MUX_ALL_CHANS_ON = 0xff;

    // Number of times to retry bus stuck recovery
    static const uint32_t BUS_CLEAR_ATTEMPT_REPEAT_COUNT = 5;

    // Max number of recurse levels mux connected to mux connected to mux etc
    static const uint32_t MAX_RECURSE_LEVEL_MUX_CONNECTIONS = 5;

    // Multiplexer functionality enabled
    bool _isEnabled = true;

    // Bus power controller
    BusPowerController* _pBusPowerController = nullptr;

    // Bus stuck handler
    BusStuckHandler& _busStuckHandler;

    // Bus status manager
    BusStatusMgr& _busStatusMgr;

    // Bus element tracker
    BusI2CElemTracker& _busElemTracker;

    // Bus access function
    BusReqSyncFn _busReqSyncFn;

    // Bus mux address range
    BusElemAddrType _minAddr = I2C_BUS_MUX_BASE_DEFAULT;
    BusElemAddrType _maxAddr = I2C_BUS_MUX_BASE_DEFAULT+I2C_BUS_MUX_MAX_DEFAULT-1;

    // Bus mux reset pin(s)
    std::vector<int8_t> _resetPins;

    // Clear cascade mux
    // When true, attempt to clear cascaded mux channels when any mux is discovered
    bool _clearCascadeMux = false;

    // Bus multiplexer record
    class BusMux
    {
    public:
        // Flags

        // isOnline:1 - mux is online
        bool isOnline:1 = false;

        // maskWrittenOk:1 - Current bit mask has been written to the mux
        bool maskWrittenOk:1 = false;

        // Detection count (to avoid false positives due to noise)
        uint8_t detectionCount = 0;
        static const uint8_t DETECTION_COUNT_THRESHOLD = 2;

        // Mux connection slot number (0 for main bus, 1-N for accessed via another mux)
        uint8_t muxConnSlotNum = 0;

        // Current bit mask (each bit enables a slot when 1)
        uint32_t curBitMask = 0;
    };

    // Bus multiplexer records
    std::vector<BusMux> _busMuxRecs;

    // Bus mux slot indices (that have been found on discovered bus multiplexers)
    std::vector<uint8_t> _busMuxSlotIndices;

    // Flag indicating at least one second-level mux is detected
    // A second-level mux is where one mux is connected via another mux
    bool _secondLevelMuxDetected = false;

    // Init bus mux records
    void initBusMuxRecs();

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Set slot enables on mux
    /// @param muxIdx Multiplexer index
    /// @param slotMask Slot mask
    /// @param force Force enable/disable (even if status indicates it is not necessary)
    /// @param recurseLevel Recursion level (mux connected to mux connected to mux etc.)
    /// @return Result code
    RaftRetCode setSlotEnables(uint32_t muxIdx, uint32_t slotMask, 
                bool force, uint32_t recurseLevel = 0);

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Write slot mask to multiplexer
    /// @param muxIdx Multiplexer index
    /// @param slotMask Slot mask
    /// @param force Force enable/disable (even if status indicates it is not necessary)
    /// @param recurseLevel Recursion level (mux connected to mux connected to mux etc.)
    /// @return Result code
    RaftRetCode writeSlotMaskToMux(uint32_t muxIdx, 
                uint32_t slotMask, bool force, uint32_t recurseLevel);

    /// @brief Disable all slots on cascaded bus multiplexers
    void clearCascadedMuxes(uint32_t muxIdx);

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Attempt to clear bus stuck problem
    /// @param failAfterSlotSet Bus stuck after setting slot (so an individual slot may be stuck)
    /// @param slotNum Slot number (1-based) (valid if after slot set)
    /// @return True if succeeded in clearing the bus stuck problem
    bool attemptToClearBusStuck(bool failAfterSlotSet, uint32_t slotNum);

    // Debug
    static constexpr const char* MODULE_PREFIX = "RaftI2CBusMux";    
};
