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
// #define DEBUG_BUS_STUCK_WITH_GPIO_NUM 19
// #define DEBUG_BUS_STUCK
// #define DEBUG_BUS_EXTENDERS
// #define DEBUG_BUS_EXTENDER_ELEM_STATE_CHANGE

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

    // Disable all slots on bus extenders
    disableAllSlots();

    // Debug
    LOG_I(MODULE_PREFIX, "setup %s minAddr 0x%02x maxAddr 0x%02x numRecs %d rstPin %d rstPinAlt %d", 
            _isEnabled ? "ENABLED" : "DISABLED", _minAddr, _maxAddr, _busExtenderRecs.size(), _resetPin, _resetPinAlt);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Service
void BusExtenderMgr::service()
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

    // Update the available slots array
    if (changeDetected)
    {
        // Update the available slots array
        _busExtenderSlots.clear();
        for (uint32_t i = 0; i < _busExtenderRecs.size(); i++)
        {
            if (_busExtenderRecs[i].isDetected)
            {
                for (uint32_t slot = 0; slot < I2C_BUS_EXTENDER_SLOT_COUNT; slot++)
                    _busExtenderSlots.push_back(i * I2C_BUS_EXTENDER_SLOT_COUNT + slot);
            }
        }
    }

    // Set the online status
    _busExtenderRecs[elemIdx].isOnline = elemResponding;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Set bus extender channels using mask
/// @param addr Address of bus extender
/// @param channelMask Mask of channels to set (1 bit per channel, 0=off, 1=on)
RaftI2CCentralIF::AccessResultCode BusExtenderMgr::setChannels(uint32_t addr, uint32_t channelMask)
{
    // Initialise bus extender
    BusI2CAddrAndSlot addrAndSlot(addr, 0);
    uint8_t writeData[1] = { uint8_t(channelMask) };
    BusI2CRequestRec reqRec(BUS_REQ_TYPE_FAST_SCAN, 
                addrAndSlot,
                0, sizeof(writeData),
                writeData,
                0,
                0, 
                nullptr, 
                this);
    return _busI2CReqSyncFn(&reqRec, nullptr);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Enable a single slot on bus extender(s)
/// @param slotPlus1 Slot number (1-based)
/// @return True if valid (false if slot is stuck or invalid)
bool BusExtenderMgr::enableOneSlot(uint32_t slotPlus1)
{
    // Check if no slot is specified (in which case do nothing)
    if (slotPlus1 == 0)
        return true;
    // Get the bus extender index and slot index
    uint32_t slotIdx = 0;
    uint32_t extenderIdx = 0;
    if (!getExtenderAndSlotIdx(slotPlus1, extenderIdx, slotIdx))
        return false;

    // Check if the slot has stable power
    if (!_busPowerController.isSlotPowerStable(slotPlus1))
        return false;

    // Debug
#ifdef DEBUG_BUS_STUCK_WITH_GPIO_NUM
    pinMode(DEBUG_BUS_STUCK_WITH_GPIO_NUM, OUTPUT);
    digitalWrite(DEBUG_BUS_STUCK_WITH_GPIO_NUM, HIGH);
    delayMicroseconds(1);
    digitalWrite(DEBUG_BUS_STUCK_WITH_GPIO_NUM, LOW);
    delayMicroseconds(1);
    digitalWrite(DEBUG_BUS_STUCK_WITH_GPIO_NUM, HIGH);
    delayMicroseconds(1);
#endif

    // Set all bus extender channels off except for one
    uint32_t mask = 1 << slotIdx;
    uint32_t addr = _minAddr + extenderIdx;
    setChannels(addr, mask);

#ifdef DEBUG_BUS_STUCK_WITH_GPIO_NUM
    digitalWrite(DEBUG_BUS_STUCK_WITH_GPIO_NUM, LOW);
    delayMicroseconds(1);
    digitalWrite(DEBUG_BUS_STUCK_WITH_GPIO_NUM, HIGH);
    delayMicroseconds(1);
#endif

    // Check if bus is stuck
    bool slotIsStuck = _busStuckHandler.isStuck();
    if (slotIsStuck)
    {
#ifdef DEBUG_BUS_STUCK
        LOG_I(MODULE_PREFIX, "enableOneSlot bus stuck so power cycling slotPlus1 %d", slotPlus1);
#endif

        // Clear the stuck bus problem by disabling all slots for now
        disableAllSlots();
        
        // LOG_I(MODULE_PREFIX, "enableOneSlot bus stuck so resetting");
#ifdef DEBUG_BUS_STUCK_WITH_GPIO_NUM
        for (int i = 0; i < 5; i++)
        {
            digitalWrite(DEBUG_BUS_STUCK_WITH_GPIO_NUM, LOW);
            delayMicroseconds(1);
            digitalWrite(DEBUG_BUS_STUCK_WITH_GPIO_NUM, HIGH);
            delayMicroseconds(1);
        }
#endif

        delayMicroseconds(100);

        // Inform the bus status manager that a slot is powering down
        _busStatusMgr.slotPoweringDown(slotPlus1);

        // Indicate to the power controller that the bus is stuck
        _busPowerController.powerCycleSlot(slotPlus1);
    }

#ifdef DEBUG_BUS_STUCK_WITH_GPIO_NUM
    digitalWrite(DEBUG_BUS_STUCK_WITH_GPIO_NUM, LOW);
    delayMicroseconds(1);
#endif
    return !slotIsStuck;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Disable all slots on bus extenders
void BusExtenderMgr::disableAllSlots()
{
    // Reset bus extenders
    if (_resetPin >= 0)
        gpio_set_level(_resetPin, 0);
    if (_resetPinAlt >= 0)
        gpio_set_level(_resetPinAlt, 0);
    delayMicroseconds(10);
    if (_resetPin >= 0)
        gpio_set_level(_resetPin, 1);
    if (_resetPinAlt >= 0)
        gpio_set_level(_resetPinAlt, 1);
    delayMicroseconds(10);
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
/// @brief Get extender and slot index from slotPlus1
/// @param slotPlus1 Slot number (1-based)
/// @param extenderIdx Extender index
/// @param slotIdx Slot index
/// @return True if valid
/// @note this is used when scanning to work through all slots and then loop back to 0 (main bus)
bool BusExtenderMgr::getExtenderAndSlotIdx(uint32_t slotPlus1, uint32_t& extenderIdx, uint32_t& slotIdx)
{
    // Check valid slot
    if ((slotPlus1 == 0) || (slotPlus1 > I2C_BUS_EXTENDER_SLOT_COUNT * _busExtenderRecs.size()))
        return false;

    // Calculate the bus extender index and slot index
    extenderIdx = (slotPlus1-1) / I2C_BUS_EXTENDER_SLOT_COUNT;
    slotIdx = (slotPlus1-1) % I2C_BUS_EXTENDER_SLOT_COUNT;
    return true;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get next slot
/// @param slotPlus1 Slot number (1-based)
/// @note this is used when scanning to work through all slots and then loop back to 0 (main bus)
/// @return Next slot number (1-based)
uint32_t BusExtenderMgr::getNextSlot(uint32_t slotPlus1)
{
    // Check if no slots available
    if (_busExtenderSlots.size() == 0)
        return 0;
    // Find first slot which is >= slotPlus1
    for (uint32_t slot : _busExtenderSlots)
    {
        if (slot >= slotPlus1)
            return slot+1;
    }
    // Return to main bus
    return 0;
}
