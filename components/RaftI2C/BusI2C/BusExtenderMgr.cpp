/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// I2C Bus Extender Manager
//
// Rob Dobson 2024
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "BusExtenderMgr.h"
#include "RaftJsonPrefixed.h"
#include "RaftJson.h"
#include "RaftUtils.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// #define DEBUG_BUS_EXTENDER_SETUP
// #define DEBUG_BUS_STUCK_WITH_GPIO_NUM 18
// #define DEBUG_BUS_STUCK
// #define DEBUG_BUS_EXTENDERS
// #define DEBUG_BUS_EXTENDER_ELEM_STATE_CHANGE
// #define DEBUG_SLOT_INDEX_INVALID
// #define DEBUG_POWER_STABILITY

static const char* MODULE_PREFIX = "BusExtenderMgr";

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Constructor
BusExtenderMgr::BusExtenderMgr(BusPowerController& busPowerController, BusStuckHandler& busStuckHandler, 
        BusStatusMgr& BusStatusMgr, BusI2CReqSyncFn busI2CReqSyncFn) :
    _busPowerController(busPowerController), _busStuckHandler(busStuckHandler),
    _busStatusMgr(BusStatusMgr), _busI2CReqSyncFn(busI2CReqSyncFn)
{
    // Init bus extender records
    initBusExtenderRecs();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Destructor
BusExtenderMgr::~BusExtenderMgr()
{
    // Reset pins
    if (_resetPin >= 0)
        gpio_reset_pin(_resetPin);
    if (_resetPinAlt >= 0)
        gpio_reset_pin(_resetPinAlt);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Setup
/// @param config Configuration
void BusExtenderMgr::setup(const RaftJsonIF& config)
{
    // Check if extender functionality is enabled
    _isEnabled = config.getBool("enable", true);

    // Get the bus extender address range
    _minAddr = config.getLong("minAddr", I2C_BUS_EXTENDER_BASE);
    _maxAddr = config.getLong("maxAddr", I2C_BUS_EXTENDER_BASE+I2C_BUS_EXTENDERS_MAX-1);

    // Check if enabled
    if (_isEnabled)
    {
        // Extender reset pin(s)
        _resetPin = (gpio_num_t) config.getLong("rstPin", -1);
        _resetPinAlt = (gpio_num_t) config.getLong("rstPinAlt", -1);

        // Setup reset pins
        gpio_config_t io_conf = {};
        if (_resetPin >= 0)
        {
            io_conf.intr_type = GPIO_INTR_DISABLE;
            io_conf.mode = GPIO_MODE_OUTPUT;
            io_conf.pin_bit_mask = 1ULL << _resetPin;
            io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
            io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
            gpio_config(&io_conf);
            gpio_set_level(_resetPin, 1);
        }
        if (_resetPinAlt >= 0)
        {
            io_conf.intr_type = GPIO_INTR_DISABLE;
            io_conf.mode = GPIO_MODE_OUTPUT;
            io_conf.pin_bit_mask = 1ULL << _resetPinAlt;
            io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
            io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
            gpio_config(&io_conf);
            gpio_set_level(_resetPinAlt, 1);
        }
    }

    // Set the flag to indicate that the bus extenders need to be initialized
    for (auto &busExtender : _busExtenderRecs)
        busExtender.maskWrittenOk = false;

    // Debug
    LOG_I(MODULE_PREFIX, "setup %s minAddr 0x%02x maxAddr 0x%02x numRecs %d rstPin %d rstPinAlt %d", 
            _isEnabled ? "ENABLED" : "DISABLED", _minAddr, _maxAddr, _busExtenderRecs.size(), _resetPin, _resetPinAlt);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Service
void BusExtenderMgr::loop()
{
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Service called from I2C task
void BusExtenderMgr::taskService()
{
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Handle state change on an element
/// @param addr Address of element
/// @param elemResponding True if element is responding
void BusExtenderMgr::elemStateChange(uint32_t addr, bool elemResponding)
{
    // Check if this is a bus extender
    if (!isBusExtender(addr))
        return;

    // Update the bus extender record
    bool changeDetected = false;
    uint32_t elemIdx = addr-_minAddr;
    if (elemResponding)
    {
        if (!_busExtenderRecs[elemIdx].isDetected)
        {
            _busExtenderCount++;
            changeDetected = true;
            _busExtenderRecs[elemIdx].maskWrittenOk = false;

            // Debug
#ifdef DEBUG_BUS_EXTENDER_ELEM_STATE_CHANGE
            LOG_I(MODULE_PREFIX, "elemStateChange new bus extender 0x%02x numDetectedExtenders %d", addr, _busExtenderCount);
#endif
        }
        _busExtenderRecs[elemIdx].isDetected = true;
    }
    else if (_busExtenderRecs[elemIdx].isDetected)
    {
        changeDetected = true;

        // Debug
#ifdef DEBUG_BUS_EXTENDER_ELEM_STATE_CHANGE
        LOG_I(MODULE_PREFIX, "elemStateChange bus extender now offline so re-init 0x%02x numDetectedExtenders %d", addr, _busExtenderCount);
#endif

    }

    // Update the slot indices
    if (changeDetected)
    {
        // Update the slot indices
        _busExtenderSlotIndices.clear();
        for (uint32_t i = 0; i < _busExtenderRecs.size(); i++)
        {
            if (_busExtenderRecs[i].isDetected)
            {
                for (uint32_t slot = 0; slot < I2C_BUS_EXTENDER_SLOT_COUNT; slot++)
                    _busExtenderSlotIndices.push_back(i * I2C_BUS_EXTENDER_SLOT_COUNT + slot);
            }
        }
    }

    // Set the online status
    _busExtenderRecs[elemIdx].isOnline = elemResponding;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Set slot enables on extender
/// @param extenderIdx Extender index
/// @param slotMask Slot mask
/// @param force Force enable/disable (even if status indicates it is not necessary)
/// @return Result code
RaftI2CCentralIF::AccessResultCode BusExtenderMgr::setSlotEnables(uint32_t extenderIdx, uint32_t slotMask, bool force)
{
    // Check valid
    if (extenderIdx >= _busExtenderRecs.size())
        return RaftI2CCentralIF::ACCESS_RESULT_INVALID;
    // Check if slot initialized
    if (!_busExtenderRecs[extenderIdx].maskWrittenOk)
    {
        force = true;
        _busExtenderRecs[extenderIdx].maskWrittenOk = true;
    }
    // Check if status indicates that the mask is already correct
    if (!force)
    {
        BusExtender& busExtender = _busExtenderRecs[extenderIdx];
        if (busExtender.curBitMask == slotMask)
            return RaftI2CCentralIF::ACCESS_RESULT_OK;
    }
    // Calculate the address
    uint32_t addr = _minAddr + extenderIdx;
    // Initialise bus extender
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
        _busExtenderRecs[extenderIdx].curBitMask = slotMask;
    }
    else
    {
        _busExtenderRecs[extenderIdx].maskWrittenOk = false;
    }
    return rslt;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Enable a single slot on bus extender(s)
/// @param slotNum Slot number (1-based)
/// @return OK if successful, otherwise error code which may be invalid if the slotNum doesn't exist or 
///         bus stuck codes if the bus is now stuck or power unstable if a device is powering up
RaftI2CCentralIF::AccessResultCode BusExtenderMgr::enableOneSlot(uint32_t slotNum)
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
            // Attempt to clear bus-stuck
            attemptToClearBusStuck(false, slotNum);

            // Check again
            busIsStuck = _busStuckHandler.isStuck();
            if (!busIsStuck)
                break;
        }

        // Check if still stuck
        if (busIsStuck)
            return RaftI2CCentralIF::ACCESS_RESULT_BUS_STUCK;
    }

    // Check if main bus is specified
    if (slotNum == 0)
    {
        disableAllSlots(false);
        return RaftI2CCentralIF::ACCESS_RESULT_OK;
    }
    // Get the bus extender index and slot index
    uint32_t slotIdx = 0;
    uint32_t extenderIdx = 0;
    if (!getExtenderAndSlotIdx(slotNum, extenderIdx, slotIdx))
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
    bool slotSetOk = setSlotEnables(extenderIdx, mask, false) == RaftI2CCentralIF::ACCESS_RESULT_OK;

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
            attemptToClearBusStuck(true, slotNum);

            // Check if still stuck
            busIsStuck = _busStuckHandler.isStuck();
            if (!busIsStuck)
                break;
        }
    }

    // Check for bus still stuck
    if (busIsStuck)
        return RaftI2CCentralIF::ACCESS_RESULT_BUS_STUCK;

    // Return result
    return slotSetOk ? RaftI2CCentralIF::ACCESS_RESULT_OK : RaftI2CCentralIF::ACCESS_RESULT_ACK_ERROR;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Disable all slots on bus extenders
void BusExtenderMgr::disableAllSlots(bool force)
{
    // Check if reset is available
    if (_resetPin < 0)
    {
        // Set all bus extender channels off
        for (uint32_t extenderIdx = 0; extenderIdx < _busExtenderRecs.size(); extenderIdx++)
        {
            BusExtender& busExtender = _busExtenderRecs[extenderIdx];
            if ((busExtender.isDetected) && (busExtender.isOnline))
                setSlotEnables(extenderIdx, 0, force);
        }
    }
    else
    {
        // Reset bus extenders
        if (_resetPin >= 0)
            gpio_set_level(_resetPin, 0);
        if (_resetPinAlt >= 0)
            gpio_set_level(_resetPinAlt, 0);
        delayMicroseconds(1);
        if (_resetPin >= 0)
            gpio_set_level(_resetPin, 1);
        if (_resetPinAlt >= 0)
            gpio_set_level(_resetPinAlt, 1);
        delayMicroseconds(1);

        // Clear mask values
        for (uint32_t extenderIdx = 0; extenderIdx < _busExtenderRecs.size(); extenderIdx++)
        {
            _busExtenderRecs[extenderIdx].curBitMask = 0;
        }
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Initialise bus extender records
void BusExtenderMgr::initBusExtenderRecs()
{
    // Clear existing records
    _busExtenderRecs.clear();
    _busExtenderRecs.resize(_maxAddr-_minAddr+1);
    _busExtenderCount = 0;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get extender and slot index from slotNum
/// @param slotNum Slot number (1-based)
/// @param extenderIdx Extender index
/// @param slotIdx Slot index
/// @return True if valid
/// @note this is used when scanning to work through all slots and then loop back to 0 (main bus)
bool BusExtenderMgr::getExtenderAndSlotIdx(uint32_t slotNum, uint32_t& extenderIdx, uint32_t& slotIdx)
{
    // Check valid slot
    if ((slotNum == 0) || (slotNum > I2C_BUS_EXTENDER_SLOT_COUNT * _busExtenderRecs.size()))
        return false;

    // Calculate the bus extender index and slot index
    extenderIdx = (slotNum-1) / I2C_BUS_EXTENDER_SLOT_COUNT;
    slotIdx = (slotNum-1) % I2C_BUS_EXTENDER_SLOT_COUNT;
    return true;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get next slot
/// @param slotNum Slot number (1-based)
/// @note this is used when scanning to work through all slots and then loop back to 0 (main bus)
/// @return Next slot number (1-based)
uint32_t BusExtenderMgr::getNextSlotNum(uint32_t slotNum)
{
    // Check if no slots available
    if (_busExtenderSlotIndices.size() == 0)
        return 0;
    // Find first slot which is >= slotNum
    for (uint32_t slotIdx : _busExtenderSlotIndices)
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
/// @return true if slot setting is still valid
bool BusExtenderMgr::attemptToClearBusStuck(bool failAfterSlotSet, uint32_t slotNum)
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
        if (failAfterSlotSet)
        {
            // Check if the slot is power controlled
            if (_busPowerController.isSlotPowerControlled(slotNum))
            {
                // Inform the bus status manager that a slot is powering down
                _busStatusMgr.slotPoweringDown(slotNum);

                // Start power cycling the slot
                _busPowerController.powerCycleSlot(slotNum);

                // Wait here to allow power off to take effect
                delay(200);
            }

            // Attempt to clear the bus-stuck issue by clocking the bus
            _busStuckHandler.clearStuckByClocking();
        }

        // Check if still stuck
        busIsStuck = _busStuckHandler.isStuck();
        if (busIsStuck)
        {
            // Clear the stuck bus problem by power cycling the entire bus
            _busPowerController.powerCycleSlot(0);

            // Wait here to allow power off to take effect
            delay(200);

            // Attempt to clear the bus-stuck issue by clocking the bus
            _busStuckHandler.clearStuckByClocking();

            // Check if still stuck
            busIsStuck = _busStuckHandler.isStuck();
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

    return !busIsStuck;
}
