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

#define DEBUG_BUS_EXTENDER_SETUP
#define DEBUG_BUS_EXTENDER_POWER_CONTROL
// #define DEBUG_BUS_EXTENDERS
// #define DEBUG_BUS_EXTENDER_ELEM_STATE_CHANGE

static const char* MODULE_PREFIX = "BusExtenderMgr";

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Constructor
BusExtenderMgr::BusExtenderMgr(BusI2CReqSyncFn busI2CReqSyncFn) :
    _busI2CReqSyncFn(busI2CReqSyncFn)
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

        // Create bus extender records
        RaftJsonPrefixed pwrCtrlConfig(config, "pwrCtrl");
        setupPowerControl(pwrCtrlConfig);

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

    // Perform hardware reset of bus extenders
    hardwareReset();

#ifdef DEBUG_BUS_EXTENDER_SETUP
    LOG_I(MODULE_PREFIX, "setup %s minAddr 0x%02x maxAddr 0x%02x numRecs %d rstPin %d rstPinAlt %d", 
            _isEnabled ? "ENABLED" : "DISABLED", _minAddr, _maxAddr, _busExtenderRecs.size(), _resetPin, _resetPinAlt);
#endif
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
    // Check enabled
    if (!_isEnabled)
        return;

    // Ensure bus extenders are initialised
    uint32_t addr = _minAddr;
    for (BusExtender& busExtender : _busExtenderRecs)
    {
        if (busExtender.isOnline && !busExtender.isInitialised)
        {
            if (setChannels(addr, I2C_BUS_EXTENDER_ALL_CHANS_ON) == 
                                RaftI2CCentralIF::ACCESS_RESULT_OK)
            {
#ifdef DEBUG_BUS_EXTENDERS
                LOG_I(MODULE_PREFIX, "service bus extender 0x%02x initialised", addr);
#endif                
                busExtender.isInitialised = true;
            }
        }
        addr++;
    }

    // Handle any changes to power control
    writePowerControlRegisters();

    // Check time to change power control initialisation state
    switch (_powerControlInitState)
    {
        case POWER_CONTROL_INIT_OFF:
            if (Raft::isTimeout(millis(), _powerControlInitLastMs, STARTUP_CHANGE_TO_DEFAULT_VOLTAGE_MS))
            {
                _powerControlInitState = POWER_CONTROL_INIT_ON;
                setVoltageLevel(0, _defaultVoltageLevel);
            }
            break;
        default:
            break;
    }
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
            _busExtenderRecs[elemIdx].isInitialised = false;
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
        // Check if it has gone offline in which case set for re-init
        _busExtenderRecs[elemIdx].isInitialised = false;
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
void BusExtenderMgr::enableOneSlot(uint32_t slotPlus1)
{
    // Get the bus extender index and slot index
    uint32_t slotIdx = 0;
    uint32_t extenderIdx = 0;
    if (!getExtenderAndSlotIdx(slotPlus1, extenderIdx, slotIdx))
        return;

    // Reset to ensure all slots disabled
    hardwareReset();

    // Set all bus extender channels off except for one
    uint32_t mask = 1 << slotIdx;
    uint32_t addr = _minAddr + extenderIdx;
    setChannels(addr, mask);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Hardware reset of bus extenders
void BusExtenderMgr::hardwareReset()
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
/// @brief Setup power control for bus extenders
/// @param pwrCtrlConfig Power control configuration
void BusExtenderMgr::setupPowerControl(const RaftJsonIF& pwrCtrlConfig)
{
    // Get array of power control info
    std::vector<String> pwrCtrlArray;
    pwrCtrlConfig.getArrayElems("ctrls", pwrCtrlArray);

    // Handle power control elements
    for (RaftJson pwrCtrlElem : pwrCtrlArray)
    {
        // Get the muxAddress
        uint32_t muxAddr = pwrCtrlElem.getLong("muxAddr", 0);

        // Check if address is within range and get index to bus extender recs
        if (muxAddr < _minAddr || muxAddr > _maxAddr)
        {
            LOG_W(MODULE_PREFIX, "setupPowerControl muxAddr 0x%02x out of range", muxAddr);
            continue;
        }
        uint32_t extenderIdx = muxAddr - _minAddr;

        // Get the power control device address
        uint32_t pwrCtrlDeviceAddr = pwrCtrlElem.getLong("devAddr", 0);
        if (pwrCtrlDeviceAddr == 0)
        {
            LOG_W(MODULE_PREFIX, "setupPowerControl devAddr 0x%02x INVALID", pwrCtrlDeviceAddr);
            continue;
        }

        // Get device type
        String pwrCtrlDeviceType = pwrCtrlElem.getString("dev", "");
        if (pwrCtrlDeviceType == "PCA9535")
        {
            _busExtenderRecs[extenderIdx].pwrCtrlAddr = pwrCtrlDeviceAddr;
            _busExtenderRecs[extenderIdx].pwrCtrlType = POWER_CONTROL_PCA9535;
        }
        else
        {
            LOG_W(MODULE_PREFIX, "setupPowerControl dev %s INVALID", pwrCtrlDeviceType.c_str());
            continue;
        }

#ifdef DEBUG_BUS_EXTENDER_SETUP
        LOG_I(MODULE_PREFIX, "setupPowerControl muxAddr 0x%02x index %d deviceAddr 0x%02x", muxAddr, extenderIdx, pwrCtrlDeviceAddr);
#endif
    }

    // Default voltage level
    String defaultVoltage = pwrCtrlConfig.getString("vDefault", "");
    _defaultVoltageLevel = POWER_CONTROL_OFF;
    if (defaultVoltage.startsWith("3"))
        _defaultVoltageLevel = POWER_CONTROL_3V3;
    else if (defaultVoltage.startsWith("5"))
        _defaultVoltageLevel = POWER_CONTROL_5V;

    // Turn off all power to external devices initially
    setVoltageLevel(0, POWER_CONTROL_OFF);
    _powerControlInitState = POWER_CONTROL_INIT_OFF;

#ifdef DEBUG_BUS_EXTENDER_SETUP
    LOG_I(MODULE_PREFIX, "setupPowerControl defaultVoltage %s", defaultVoltage.c_str());
#endif
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Set voltage level for a slot (or all slots)
/// @param slotPlus1 Slot number (0 is all slots)
/// @param powerLevel Power level
void BusExtenderMgr::setVoltageLevel(uint32_t slotPlus1, PowerControlLevels powerLevel)
{
    // Check for one or more slots
    if (slotPlus1 == 0)
    {
        // Iterate all slots
        for (slotPlus1 = 1; slotPlus1 < I2C_BUS_EXTENDER_SLOT_COUNT * _busExtenderRecs.size() + 1; slotPlus1++)
        {
            // Update mask and output registers for the slot
            updatePowerControlRegs(slotPlus1, powerLevel);
        }
    }
    else
    {
        // Update mask and output registers for the slot
        updatePowerControlRegs(slotPlus1, powerLevel);
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Update power control registers for a single slot
/// @param slotPlus1 Slot number (1-based)
/// @param powerLevel Power level
void BusExtenderMgr::updatePowerControlRegs(uint32_t slotPlus1, PowerControlLevels powerLevel)
{
    // Get the bus extender index and slot index
    uint32_t slotIdx = 0;
    uint32_t extenderIdx = 0;
    if (!getExtenderAndSlotIdx(slotPlus1, extenderIdx, slotIdx))
        return;

    // Get the bus extender record
    BusExtender& busExtenderRec = _busExtenderRecs[extenderIdx];

    // Check if power control is for PCA9535 (only one option supported currently)
    if (busExtenderRec.pwrCtrlType != POWER_CONTROL_PCA9535)
        return;

    // Base bit mask for the slot if two bits (one for 3V and one for 5V)
    // There is both a configuration register and an output register and both are written on each change
    // In both registers an enabled voltage output is a 0 in the corresponding bit position
    // The base mask is inverse to this logic so that a shift and inversion results in the total mask required
    uint16_t orMask = 0b11 << (slotIdx * 2);
    static const uint16_t BASE_MASK_BITS[] = {0b00, 0b01, 0b10};
    uint16_t baseMask = ~((BASE_MASK_BITS[powerLevel]) << (slotIdx * 2));

    // Compute the new value for the control register (16 bits)
    uint16_t newRegVal = (busExtenderRec.pwrCtrlGPIOReg | orMask) & baseMask;

#ifdef DEBUG_BUS_EXTENDER_SETUP
    uint16_t prevReg = busExtenderRec.pwrCtrlGPIOReg;
#endif

    // Check if the mask has changed
    if (newRegVal != busExtenderRec.pwrCtrlGPIOReg)
    {
        // Update the bus extender record
        busExtenderRec.pwrCtrlGPIOReg = newRegVal;
        busExtenderRec.pwrCtrlDirty = true;
    }

#ifdef DEBUG_BUS_EXTENDER_POWER_CONTROL
    LOG_I(MODULE_PREFIX, "updatePowerLevel hasChanged %s slotPlus1 %d SlotPlus1-1 %d extenderIdx %d slotIdx %d powerLevel %d newRegVal 0x%02x(was 0x%02x)", 
            busExtenderRec.pwrCtrlDirty ? "YES" : "NO",
            slotPlus1, slotPlus1-1, extenderIdx, slotIdx, powerLevel, 
            busExtenderRec.pwrCtrlGPIOReg, prevReg);
#endif
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Write power control registers for all slots
void BusExtenderMgr::writePowerControlRegisters()
{
    // Iterate through bus extenders
    for (BusExtender& busExtender : _busExtenderRecs)
    {
        if (busExtender.isOnline && busExtender.pwrCtrlDirty && 
                    (busExtender.pwrCtrlAddr != 0) && (busExtender.pwrCtrlType == POWER_CONTROL_PCA9535))
        {
            // Set the output register first (to avoid unexpected power changes)
            uint8_t writeData[3] = { PCA9535_OUTPUT_PORT_0, 
                        uint8_t(busExtender.pwrCtrlGPIOReg & 0xff), 
                        uint8_t(busExtender.pwrCtrlGPIOReg >> 8)};
            BusI2CAddrAndSlot addrAndSlot(busExtender.pwrCtrlAddr, 0);
            BusI2CRequestRec reqRec(BUS_REQ_TYPE_FAST_SCAN,
                        addrAndSlot,
                        0, sizeof(writeData),
                        writeData,
                        0,
                        0, 
                        nullptr, 
                        this);
            bool rsltOk = _busI2CReqSyncFn(&reqRec, nullptr) == RaftI2CCentralIF::ACCESS_RESULT_OK;

            // Write the configuration register
            writeData[0] = PCA9535_CONFIG_PORT_0;
            BusI2CRequestRec reqRec2(BUS_REQ_TYPE_FAST_SCAN,
                        addrAndSlot,
                        0, sizeof(writeData),
                        writeData,
                        0,
                        0, 
                        nullptr, 
                        this);
            rsltOk &= _busI2CReqSyncFn(&reqRec2, nullptr) == RaftI2CCentralIF::ACCESS_RESULT_OK;

            // Clear the dirty flag if result is ok
            busExtender.pwrCtrlDirty = !rsltOk;

#ifdef DEBUG_BUS_EXTENDER_POWER_CONTROL
            LOG_I(MODULE_PREFIX, "writePowerControlRegisters addr 0x%02x reg 0x%04x rslt %s", 
                    busExtender.pwrCtrlAddr, busExtender.pwrCtrlGPIOReg, rsltOk ? "OK" : "FAIL");
#endif
        }
    }
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
