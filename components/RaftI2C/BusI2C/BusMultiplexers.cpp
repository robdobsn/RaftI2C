/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// I2C Bus Multiplexers
//
// Rob Dobson 2024
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "BusMultiplexers.h"
#include "RaftJsonPrefixed.h"
#include "RaftJson.h"
#include "RaftUtils.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define DEBUG_BUS_MUX_SETUP
#define DEBUG_BUS_STUCK_WITH_GPIO_NUM 18
#define DEBUG_BUS_STUCK
#define DEBUG_BUS_MUX_ELEM_STATE_CHANGE
#define DEBUG_SLOT_INDEX_INVALID
#define DEBUG_POWER_STABILITY
#define DEBUG_SET_SLOT_ENABLES
#define DEBUG_MULTI_LEVEL_MUX_CONNECTIONS

static const char* MODULE_PREFIX = "BusMux";

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Constructor
BusMultiplexers::BusMultiplexers(BusPowerController& busPowerController, BusStuckHandler& busStuckHandler, 
        BusStatusMgr& BusStatusMgr, BusI2CReqSyncFn busI2CReqSyncFn) :
    _busPowerController(busPowerController), _busStuckHandler(busStuckHandler),
    _busStatusMgr(BusStatusMgr), _busI2CReqSyncFn(busI2CReqSyncFn)
{
    // Init bus multiplexer records
    initBusMuxRecs();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Destructor
BusMultiplexers::~BusMultiplexers()
{
    // Reset pins
    for (auto resetPin : _resetPins)
        gpio_reset_pin((gpio_num_t) resetPin);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Setup
/// @param config Configuration
void BusMultiplexers::setup(const RaftJsonIF& config)
{
    // Check if multiplexer functionality is enabled
    _isEnabled = config.getBool("enable", true);

    // Get the bus multiplexer address range
    _minAddr = config.getLong("minAddr", I2C_BUS_MUX_BASE);
    _maxAddr = config.getLong("maxAddr", I2C_BUS_MUX_BASE+I2C_BUS_MUX_MAX-1);

    // Check if enabled
    if (_isEnabled)
    {
        // Multiplexer reset pin(s)
        _resetPins.clear();
        std::vector<String> resetPinStrs;
        config.getArrayElems("rstPins", resetPinStrs);
        for (auto& resetPinStr : resetPinStrs)
            if (resetPinStr.toInt() >= 0)
                _resetPins.push_back(resetPinStr.toInt());
        
        // Add single reset pin if specified that way
        int resetPin = config.getInt("rstPin", -1);
        if (resetPin >= 0)
            _resetPins.push_back(resetPin);

        // Setup reset pins
        for (auto resetPin : _resetPins)
        {
            gpio_config_t io_conf = {};
            io_conf.intr_type = GPIO_INTR_DISABLE;
            io_conf.mode = GPIO_MODE_OUTPUT;
            io_conf.pin_bit_mask = 1ULL << resetPin;
            io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
            io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
            gpio_config(&io_conf);
            gpio_set_level((gpio_num_t)resetPin, 1);
        }
    }

    // Set the flag to indicate that the bus multiplexers need to be initialized
    for (auto &busMux : _busMuxRecs)
        busMux.maskWrittenOk = false;

    // Debug
    String resetPinStr;
    for (auto resetPin : _resetPins)
        resetPinStr += String(resetPin) + " ";
    LOG_I(MODULE_PREFIX, "setup %s minAddr 0x%02x maxAddr 0x%02x numRecs %d resetPin(s) %s", 
            _isEnabled ? "ENABLED" : "DISABLED", _minAddr, _maxAddr, _busMuxRecs.size(), 
            resetPinStr.c_str());
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Service
void BusMultiplexers::loop()
{
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Service called from I2C task
void BusMultiplexers::taskService()
{
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Handle state change on an element
/// @param addr Address of element
/// @param slotNum Slot number (1-based)
/// @param elemResponding True if element is responding
void BusMultiplexers::elemStateChange(uint32_t addr, uint32_t slotNum, bool elemResponding)
{
    // Check if this is a bus multiplexer
    if (!isBusMultiplexer(addr))
        return;

    // Update the bus multiplexer record
    bool changeDetected = false;
    uint32_t elemIdx = addr-_minAddr;
    if (elemResponding)
    {
        if (!_busMuxRecs[elemIdx].isDetected)
        {
            changeDetected = true;
            _busMuxRecs[elemIdx].maskWrittenOk = false;
            _busMuxRecs[elemIdx].muxConnSlotNum = slotNum;

            // Debug
#ifdef DEBUG_BUS_MUX_ELEM_STATE_CHANGE
            LOG_I(MODULE_PREFIX, "elemStateChange new mux addr 0x%02x slotNum %d", addr, slotNum);
#endif
        }
        _busMuxRecs[elemIdx].isDetected = true;
    }
    else if (_busMuxRecs[elemIdx].isDetected)
    {
        changeDetected = true;

        // Debug
#ifdef DEBUG_BUS_MUX_ELEM_STATE_CHANGE
        LOG_I(MODULE_PREFIX, "elemStateChange mux now offline so re-init 0x%02x slotNum %d", addr, slotNum);
#endif

    }

    // Update the slot indices
    if (changeDetected)
    {
        // Update the slot indices
        _busMuxSlotIndices.clear();
        for (uint32_t i = 0; i < _busMuxRecs.size(); i++)
        {
            if (_busMuxRecs[i].isDetected)
            {
                for (uint32_t slot = 0; slot < I2C_BUS_MUX_SLOT_COUNT; slot++)
                    _busMuxSlotIndices.push_back(i * I2C_BUS_MUX_SLOT_COUNT + slot);
            }
        }
    }

    // Set the online status
    _busMuxRecs[elemIdx].isOnline = elemResponding;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Set slot enables on multiplexer
/// @param muxIdx Multiplexer index
/// @param slotMask Slot mask
/// @param force Force enable/disable (even if status indicates it is not necessary)
/// @param recurseLevel Recursion level (mux connected to mux connected to mux etc.)
/// @return Result code
RaftI2CCentralIF::AccessResultCode BusMultiplexers::setSlotEnables(uint32_t muxIdx, 
            uint32_t slotMask, bool force, uint32_t recurseLevel)
{
    // Check valid
    if (muxIdx >= _busMuxRecs.size())
        return RaftI2CCentralIF::ACCESS_RESULT_INVALID;
    // Check if this slot relies on another slot
    if (recurseLevel > MAX_RECURSE_LEVEL_MUX_CONNECTIONS)
        return RaftI2CCentralIF::ACCESS_RESULT_INVALID;
    if (_busMuxRecs[muxIdx].muxConnSlotNum > 0)
    {
#ifdef DEBUG_MULTI_LEVEL_MUX_CONNECTIONS
        LOG_I(MODULE_PREFIX, "setSlotEnables muxIdx %d slotMask 0x%02x force %d recurseLevel %d", 
                muxIdx, slotMask, force, recurseLevel);
#endif
        // Get the mux index and slot index
        uint32_t muxConnSlotNum = _busMuxRecs[muxIdx].muxConnSlotNum;
        uint32_t muxConnMuxIdx = 0;
        uint32_t muxConnSlotIdx = 0;
        if (!getMuxAndSlotIdx(muxConnSlotNum, muxConnMuxIdx, muxConnSlotIdx))
            return RaftI2CCentralIF::ACCESS_RESULT_INVALID;
        // Check if the mux is online
        if (!_busMuxRecs[muxConnMuxIdx].isOnline)
            return RaftI2CCentralIF::ACCESS_RESULT_INVALID;
        // Check if the slot is enabled
        if ((_busMuxRecs[muxConnMuxIdx].curBitMask & (1 << muxConnSlotIdx)) == 0)
            return RaftI2CCentralIF::ACCESS_RESULT_INVALID;
        // Recursively set the slot enables
        return setSlotEnables(muxConnMuxIdx, slotMask, force, recurseLevel+1);
    }
    // Check if slot initialized
    if (!_busMuxRecs[muxIdx].maskWrittenOk)
    {
        force = true;
        _busMuxRecs[muxIdx].maskWrittenOk = true;
    }
    // Check if status indicates that the mask is already correct
    if (!force)
    {
        BusMux& busMux = _busMuxRecs[muxIdx];
        if (busMux.curBitMask == slotMask)
            return RaftI2CCentralIF::ACCESS_RESULT_OK;
    }
    // Calculate the address
    uint32_t addr = _minAddr + muxIdx;
    // Initialise bus multiplexer
    BusI2CAddrAndSlot addrAndSlot(addr, 0);
    uint8_t writeData[1] = { uint8_t(slotMask) };
    BusI2CRequestRec reqRec(BUS_REQ_TYPE_FAST_SCAN, 
                addrAndSlot,
                0, sizeof(writeData),
                writeData,
                0,
                0, 
                nullptr, 
                this);
    auto rslt = _busI2CReqSyncFn(&reqRec, nullptr);
    // Store the resulting mask information if the operation was successful
    if (rslt == RaftI2CCentralIF::ACCESS_RESULT_OK)
    {
        _busMuxRecs[muxIdx].curBitMask = slotMask;
    }
    else
    {
        _busMuxRecs[muxIdx].maskWrittenOk = false;
    }
#ifdef DEBUG_SET_SLOT_ENABLES
        LOG_I(MODULE_PREFIX, "setSlotEnables rslt %s(%d) muxIdx %d slotMask 0x%02x force %d recurseLevel %d", 
                RaftI2CCentralIF::getAccessResultStr(rslt), rslt, muxIdx, slotMask, force, recurseLevel);
#endif
    return rslt;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Enable a single slot on bus multiplexer(s)
/// @param slotNum Slot number (1-based)
/// @return OK if successful, otherwise error code which may be invalid if the slotNum doesn't exist or 
///         bus stuck codes if the bus is now stuck or power unstable if a device is powering up
RaftI2CCentralIF::AccessResultCode BusMultiplexers::enableOneSlot(uint32_t slotNum)
{
    // If the bus is stuck at this point it implies that a main bus issue has occurred or there is an issue with the last slot
    // that was enabled (if it was definitely a single-slot issue it would have been detected after the slot was first enabled).
    // In this case we need to try to clear the bus-stuck problem in any way possible including clocking the bus,
    // potentially power cycling individual slots and/or power cycling the entire bus (if hardware exists to do this)
    bool busIsStuck = _busStuckHandler.isStuck();
    if (busIsStuck)
    {
        // Repeat here seveal times to try to clear the bus-stuck problem
        for (uint32_t busClearAttemptIdx = 0; busClearAttemptIdx < BUS_CLEAR_ATTEMPT_REPEAT_COUNT; busClearAttemptIdx++)
        {
            // Attempt to clear bus-stuck (returns true if it resolved the issue)
            if (attemptToClearBusStuck(false, slotNum))
                break;
        }

        // Check if still stuck
        if (_busStuckHandler.isStuck())
            return RaftI2CCentralIF::ACCESS_RESULT_BUS_STUCK;
    }

    // Check if main bus is specified
    if (slotNum == 0)
    {
        disableAllSlots(false);
        return RaftI2CCentralIF::ACCESS_RESULT_OK;
    }
    // Get the bus multiplexer index and slot index
    uint32_t slotIdx = 0;
    uint32_t muxIdx = 0;
    if (!getMuxAndSlotIdx(slotNum, muxIdx, slotIdx))
    {
#ifdef DEBUG_SLOT_INDEX_INVALID
        LOG_I(MODULE_PREFIX, "enableOneSlot slotNum %d invalid", slotNum);
#endif
        return RaftI2CCentralIF::ACCESS_RESULT_INVALID;
    }

    // Check if the slot has stable power
    if (!_busPowerController.isSlotPowerStable(slotNum))
    {
#ifdef DEBUG_POWER_STABILITY
        LOG_I(MODULE_PREFIX, "enableOneSlot slotNum %d power not stable", slotNum);
#endif
        return RaftI2CCentralIF::ACCESS_RESULT_SLOT_POWER_UNSTABLE;
    }

    // Enable the slot
    uint32_t mask = 1 << slotIdx;
    bool slotSetOk = setSlotEnables(muxIdx, mask, false) == RaftI2CCentralIF::ACCESS_RESULT_OK;

    // Check if bus is now stuck - if we have an issue at this point it is probably due to a single slot because
    // the earlier bus stuck check would have been true if it was a wider issue. So initially try to clear the
    // single-slot issue if possible
    busIsStuck = _busStuckHandler.isStuck();
    if (busIsStuck)
    {
        // Repeat here seveal times to try to clear the bus-stuck problem
        for (uint32_t busClearAttemptIdx = 0; busClearAttemptIdx < BUS_CLEAR_ATTEMPT_REPEAT_COUNT; busClearAttemptIdx++)
        {
            // If the bus is stuck at this point then it is not possible to enable a slot
            // so try to clear the bus-stuck problem in any way possible
            // (returns true if it resolved the issue)
            if (attemptToClearBusStuck(false, slotNum))
                break;
        }
    }

    // Check for bus still stuck
    if (_busStuckHandler.isStuck())
        return RaftI2CCentralIF::ACCESS_RESULT_BUS_STUCK;

    // Return result
    return slotSetOk ? RaftI2CCentralIF::ACCESS_RESULT_OK : RaftI2CCentralIF::ACCESS_RESULT_ACK_ERROR;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Disable all slots on bus multiplexers
void BusMultiplexers::disableAllSlots(bool force)
{
    // Check if reset is available
    if (_resetPins.size() == 0)
    {
        // Set all bus multiplexer channels off
        for (uint32_t muxIdx = 0; muxIdx < _busMuxRecs.size(); muxIdx++)
        {
            BusMux& busMux = _busMuxRecs[muxIdx];
            if ((busMux.isDetected) && (busMux.isOnline))
                setSlotEnables(muxIdx, 0, force);
        }
    }
    else
    {
        // Reset bus multiplexers
        for (auto resetPin : _resetPins)
        {
            gpio_set_level((gpio_num_t)resetPin, 0);
            delayMicroseconds(1);
            gpio_set_level((gpio_num_t)resetPin, 1);
        }

        // Clear mask values
        for (uint32_t muxIdx = 0; muxIdx < _busMuxRecs.size(); muxIdx++)
        {
            _busMuxRecs[muxIdx].curBitMask = 0;
        }
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Initialise bus multiplexer records
void BusMultiplexers::initBusMuxRecs()
{
    // Clear existing records
    _busMuxRecs.clear();
    _busMuxRecs.resize(_maxAddr-_minAddr+1);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get multiplexer and slot index from slotNum
/// @param slotNum Slot number (1-based)
/// @param muxIdx Multiplexer index
/// @param slotIdx Slot index
/// @return True if valid
/// @note this is used when scanning to work through all slots and then loop back to 0 (main bus)
bool BusMultiplexers::getMuxAndSlotIdx(uint32_t slotNum, uint32_t& muxIdx, uint32_t& slotIdx)
{
    // Check valid slot
    if ((slotNum == 0) || (slotNum > I2C_BUS_MUX_SLOT_COUNT * _busMuxRecs.size()))
        return false;

    // Calculate the bus multiplexer index and slot index
    muxIdx = (slotNum-1) / I2C_BUS_MUX_SLOT_COUNT;
    slotIdx = (slotNum-1) % I2C_BUS_MUX_SLOT_COUNT;
    return true;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get next slot
/// @param slotNum Slot number (1-based)
/// @note this is used when scanning to work through all slots and then loop back to 0 (main bus)
/// @return Next slot number (1-based)
uint32_t BusMultiplexers::getNextSlotNum(uint32_t slotNum)
{
    // Check if no slots available
    if (_busMuxSlotIndices.size() == 0)
        return 0;
    // Find first slot which is >= slotNum
    for (uint32_t slotIdx : _busMuxSlotIndices)
    {
        if (slotIdx >= slotNum)
            return slotIdx+1;
    }
    // Return to main bus
    return 0;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Attempt to clear bus stuck problem
/// @param failAfterSlotSet Bus stuck after setting slot (so an individual slot maybe at fault)
/// @param slotNum Slot number (1-based) (valid if after slot set)
/// @return True if succeeded in clearing the bus stuck problem
bool BusMultiplexers::attemptToClearBusStuck(bool failAfterSlotSet, uint32_t slotNum)
{
#ifdef DEBUG_BUS_STUCK
    LOG_I(MODULE_PREFIX, "attemptToClearBusStuck %s slotNum %d", 
            failAfterSlotSet ? "FAIL_AFTER_SLOT_SET" : "FAIL_BEFORE_SLOT_SET",
            slotNum);
#endif

#ifdef DEBUG_BUS_STUCK_WITH_GPIO_NUM
    pinMode(DEBUG_BUS_STUCK_WITH_GPIO_NUM, OUTPUT);
    for (int i = 0; i < (failAfterSlotSet ? 25 : 5); i++)
    {
        digitalWrite(DEBUG_BUS_STUCK_WITH_GPIO_NUM, HIGH);
        delayMicroseconds(1);
        digitalWrite(DEBUG_BUS_STUCK_WITH_GPIO_NUM, LOW);
        delayMicroseconds(1);
    }
#endif

    // Attempt to clear the bus-stuck issue by clocking the bus
    _busStuckHandler.clearStuckByClocking();

#ifdef DEBUG_BUS_STUCK_WITH_GPIO_NUM
    delayMicroseconds(10);
    for (int i = 0; i < 3; i++)
    {
        digitalWrite(DEBUG_BUS_STUCK_WITH_GPIO_NUM, HIGH);
        delayMicroseconds(1);
        digitalWrite(DEBUG_BUS_STUCK_WITH_GPIO_NUM, LOW);
        delayMicroseconds(1);
    }
    delayMicroseconds(10);
#endif

    // Check if still stuck
    bool busIsStuck = _busStuckHandler.isStuck();
    if (busIsStuck)
    {
        // Clear the stuck bus problem by initially disabling all slots 
        disableAllSlots(true);
        
        // Check if failure occurred after the slot was set
        if (failAfterSlotSet && _busPowerController.isSlotPowerControlled(slotNum))
        {
            // Inform the bus status manager that a slot is powering down
            _busStatusMgr.slotPoweringDown(slotNum);

            // Start power cycling the slot
            _busPowerController.powerCycleSlot(slotNum);
        }
        else
        {
            // Inform the bus status manager that the bus is powering down
            _busStatusMgr.slotPoweringDown(0);

            // Clear the stuck bus problem by power cycling the entire bus
            _busPowerController.powerCycleSlot(0);
        }
    }

#ifdef DEBUG_BUS_STUCK_WITH_GPIO_NUM
    for (int i = 0; i < 5; i++)
    {
        digitalWrite(DEBUG_BUS_STUCK_WITH_GPIO_NUM, HIGH);
        delayMicroseconds(1);
        digitalWrite(DEBUG_BUS_STUCK_WITH_GPIO_NUM, LOW);
        delayMicroseconds(1);
    }
#endif

    return !_busStuckHandler.isStuck();
}
