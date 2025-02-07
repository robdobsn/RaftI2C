/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// I2C Bus Power Controller
//
// Rob Dobson 2024
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "BusPowerController.h"
#include "RaftUtils.h"
#include "Logger.h"
#include "RaftJson.h"

// #define WARN_ON_SLOT_0_NOT_POWER_CONTROLLED

// #define DEBUG_POWER_CONTROL_SETUP
// #define DEBUG_POWER_CONTROL_STATES
// #define DEBUG_POWER_CONTROL_BIT_SETTINGS
// #define DEBUG_POWER_CONTROL_SLOT_STABLE
// #define DEBUG_POWER_CONTROL_SLOT_UNSTABLE

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Constructor
BusPowerController::BusPowerController(BusReqSyncFn busI2CReqSyncFn, BusIOExpanders& busIOExpanders)
        : _busReqSyncFn(busI2CReqSyncFn), _busIOExpanders(busIOExpanders)
{
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Destructor
BusPowerController::~BusPowerController()
{
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Setup
/// @param config Configuration
void BusPowerController::setup(const RaftJsonIF& config)
{
    // Check if already setup
    if (_powerControlEnabled)
        return;

    // Check voltage level names are present (otherwise power control is disabled)
    config.getArrayElems("voltageLevels", _voltageLevelNames);

    // Check voltageLevels exists
    if (_voltageLevelNames.size() == 0)
    {
        LOG_I(MODULE_PREFIX, "No config voltageLevels found - I2C power control disabled");
        return;
    }

    // Check number of levels
    if (_voltageLevelNames.size() != POWER_CONTROL_NUM_LEVELS)
    {
        LOG_I(MODULE_PREFIX, "%s voltageLevels size %d INVALID (should be %d) - I2C power control disabled", __func__, 
                    _voltageLevelNames.size(), POWER_CONTROL_NUM_LEVELS);
        return;
    }

    //////////////////////////////////////////////////////////////////////////
    // Slot groups

    // Clear slot groups
    _slotPowerCtrlGroups.clear();

    // Get array of group info
    std::vector<String> slotGroupArray;
    config.getArrayElems("slotGroups", slotGroupArray);

    // Iterate over slot groups
    for (RaftJson slotGroupElem : slotGroupArray)
    {
        // Get the group name
        String groupName = slotGroupElem.getString("name", "");

        // Get the start slot number
        uint32_t startSlotNum = slotGroupElem.getLong("startSlotNum", 0);

        // Num slots
        uint32_t numSlots = slotGroupElem.getLong("numSlots", 0);

        // Get the default level
        int defaultLevel = slotGroupElem.getInt("defaultLevel", 0);

        // Check this is in the range of the voltage level names
        if ((defaultLevel < 0) || (defaultLevel >= POWER_CONTROL_NUM_LEVELS))
        {
            LOG_W(MODULE_PREFIX, "%s defaultLevel %d INVALID (> %d)", __func__, 
                        defaultLevel, POWER_CONTROL_NUM_LEVELS);
            continue;
        }
        PowerControlLevels defaultLevelEnum = (PowerControlLevels)defaultLevel;

        // Get the voltage level array
        std::vector<String> levelsExclOff;
        slotGroupElem.getArrayElems("levelsExclOff", levelsExclOff);

        // Check that this is one less than the length of the voltage level names
        if (levelsExclOff.size() != _voltageLevelNames.size() - 1)
        {
            LOG_W(MODULE_PREFIX, "%s levelsExclOff size + 1 (%d) != _voltageLevelNames size (%d)", __func__, 
                    levelsExclOff.size() + 1, _voltageLevelNames.size());
            continue;
        }

        // Temporary VoltageLevelPinRec records
        std::vector<std::vector<VoltageLevelPinRec>> voltageLevelPins;
        voltageLevelPins.resize(numSlots);
        for (uint32_t slotIdx = 0; slotIdx < numSlots; slotIdx++)
        {
            voltageLevelPins[slotIdx].resize(levelsExclOff.size());
        }

        // Iterate over voltage levels
        uint32_t voltageLevelIdx = 0;
        for (RaftJson voltageLevelElem : levelsExclOff)
        {
            // Get the pin and level arrays
            std::vector<int> vPinArray;
            voltageLevelElem.getArrayInts("vPins", vPinArray);
            std::vector<int> voltageOnLevelsArray;
            voltageLevelElem.getArrayInts("on", voltageOnLevelsArray);

            // Check sizes
            if ((vPinArray.size() != numSlots) || (vPinArray.size() != voltageOnLevelsArray.size()))
            {
                LOG_W(MODULE_PREFIX, "%s vPinArray size %d != voltageOnLevelsArray size %d != numSlots %d", __func__, 
                        vPinArray.size(), voltageOnLevelsArray.size(), numSlots);
                continue;
            }

            // Add to temporary records
            for (uint32_t slotIdx = 0; slotIdx < numSlots; slotIdx++)
            {
                voltageLevelPins[slotIdx][voltageLevelIdx] = 
                            VoltageLevelPinRec(vPinArray[slotIdx], voltageOnLevelsArray[slotIdx], vPinArray[slotIdx] >= 0);
            }

            // Next voltage level
            voltageLevelIdx++;
        }

        // Generate the slot records
        std::vector<SlotPowerControlRec> slotRecs;
        if (numSlots != 0)
        {
            // Check other params valid
            if (voltageLevelPins.size() == 0)
            {
                LOG_W(MODULE_PREFIX, "%s voltageLevelPins size %d INVALID", __func__, voltageLevelPins.size());
            }
            else
            {
                // Add to slot records
                for (uint32_t slotIdx = 0; slotIdx < numSlots; slotIdx++)
                {
                    slotRecs.push_back(SlotPowerControlRec(voltageLevelPins[slotIdx]));
                }
            }
        }

        // Add to slot groups
        _slotPowerCtrlGroups.push_back(SlotPowerControlGroup(groupName, startSlotNum, defaultLevelEnum, slotRecs));
    }

    // Power control is enabled
    _powerControlEnabled = true;

    // Debug
#ifdef DEBUG_POWER_CONTROL_SETUP

    // Debug voltage level names
    String voltageLevelNamesStr;
    for (String& vLevelName : _voltageLevelNames)
    {
        voltageLevelNamesStr += Raft::formatString(100, "%s ; ", vLevelName.c_str());
    }
    LOG_I(MODULE_PREFIX, "Voltage levels: %s", voltageLevelNamesStr.c_str());

    // Debug slot groups
    String slotGroupStr;
    for (SlotPowerControlGroup& slotGroup : _slotPowerCtrlGroups)
    {
        LOG_I(MODULE_PREFIX, "Slot group %s start %d defaultLevel %d", slotGroup.groupName.c_str(),
                        slotGroup.startSlotNum, slotGroup.defaultLevel);
        uint32_t slotNum = slotGroup.startSlotNum;
        for (SlotPowerControlRec& slotRec : slotGroup.slotRecs)
        {
            String voltageLevelStr;
            for (VoltageLevelPinRec& vPinRec : slotRec.voltageLevelPins)
            {
                if (voltageLevelStr.length() > 0)
                    voltageLevelStr += ", ";
                if (vPinRec.isValid)
                    voltageLevelStr += Raft::formatString(100, "vPin %d lvl %s", vPinRec.pinNum, vPinRec.onLevel ? "HIGH" : "LOW");
                else
                    voltageLevelStr += "INVALID";
            }
            LOG_I(MODULE_PREFIX, "Slot %d: %s", slotNum, voltageLevelStr.c_str());
            slotNum++;
        }
    }

#endif

}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Post-setup (called after all other modules have setup)
/// @return True if hardware initialized ok
bool BusPowerController::postSetup()
{
    // Check if power control is enabled
    if (!_powerControlEnabled)
        return false;

    // Turn all power off
    powerOffAll();

    // Hardware is now initialized
    _hardwareInitialized = true;
    return true;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Service
void BusPowerController::loop()
{
}

// /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// /// @brief Check if address is a bus power controller
// /// @param i2cAddr address of bus power controller
// /// @param muxAddr address of mux (0 if on main I2C bus)
// /// @param muxChannel channel on mux
// /// @return true if address is a bus power controller
// bool BusPowerController::isBusPowerController(uint16_t i2cAddr, uint16_t muxAddr, uint16_t muxChannel)
// {
//     // Check if power control is enabled
//     if (!_powerControlEnabled)
//         return false;

//     // Check if address is in the IO expander range
//     for (IOExpanderRec& ioExpRec : _ioExpanderRecs)
//     {
//         if ((i2cAddr == ioExpRec.addr) && (muxAddr == ioExpRec.muxAddr) && (muxChannel == ioExpRec.muxChanIdx))
//             return true;
//     }

//     // Not found
//     return false;
// }

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Check if power on a slot is stable
/// @param slotNum Slot number (1-based)
/// @return True if power is stable
bool BusPowerController::isSlotPowerStable(uint32_t slotNum)
{
    // Check if power control is enabled
    if (!_powerControlEnabled)
        return true;

    // Get the slot record - if it doesn't exist assume power is stable
    SlotPowerControlRec* pSlotRec = getSlotRecord(slotNum);
    if (!pSlotRec)
    {
        // Check if slot is 0 (main bus) power is stable
        pSlotRec = getSlotRecord(0);
        if (!pSlotRec)
        {
#ifdef DEBUG_POWER_CONTROL_SLOT_STABLE
            LOG_I(MODULE_PREFIX, "isSlotPowerStable slotNum %d not power controlled returning yes", slotNum);
#endif
            return true;
        }
        else
        {
#ifdef DEBUG_POWER_CONTROL_SLOT_STABLE
            LOG_I(MODULE_PREFIX, "isSlotPowerStable DERFER TO SLOT 0 slotNum %d", slotNum);
#endif
        }
    }

#ifdef DEBUG_POWER_CONTROL_SLOT_UNSTABLE
    // Debug
    if (!((pSlotRec->pwrCtrlState == SLOT_POWER_ON_LOW_V) || (pSlotRec->pwrCtrlState == SLOT_POWER_ON_HIGH_V)))
    {
        LOG_I(MODULE_PREFIX, "isSlotPowerStable UNSTABLE slotNum %d pwrCtrlState %d", slotNum, pSlotRec->pwrCtrlState);
    }
#endif

#ifdef DEBUG_POWER_CONTROL_SLOT_STABLE
    // Debug
    LOG_I(MODULE_PREFIX, "isSlotPowerStable slotNum %d pwrCtrlState %d returning %d", 
                slotNum, pSlotRec->pwrCtrlState, (pSlotRec->pwrCtrlState == SLOT_POWER_ON_LOW_V) || (pSlotRec->pwrCtrlState == SLOT_POWER_ON_HIGH_V));
#endif

    // Check if power is stable
    return (pSlotRec->pwrCtrlState == SLOT_POWER_ON_LOW_V) || (pSlotRec->pwrCtrlState == SLOT_POWER_ON_HIGH_V);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Power cycle slot
/// @param slotNum slot number (1 based) (0 to power cycle bus)
void BusPowerController::powerCycleSlot(uint32_t slotNum)
{
    
#ifdef DEBUG_POWER_CONTROL_STATES
    LOG_I(MODULE_PREFIX, "powerCycleSlot POWER OFF slotNum %d slotNum %d", slotNum, slotNum);
#endif

    // Check for slot 0 (power cycle bus)
    if (slotNum == 0)
    {
        // Turn off the main bus power
        setVoltageLevel(0, POWER_CONTROL_OFF, true);
        return;
    }

    // Turn the slot power off
    setVoltageLevel(slotNum, POWER_CONTROL_OFF, true);

    // Set the state to power off pending cycling
    setSlotState(slotNum, SLOT_POWER_OFF_PENDING_CYCLING, millis());
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Task loop (called from I2C task)
void BusPowerController::taskService(uint64_t timeNowUs)
{
    // Enaure hardware initialized
    if (!_hardwareInitialized)
        return;

    // Service state machine for power cycling
    uint32_t timeNowMs = timeNowUs / 1000;

    // Iterate over slot groups
    for (SlotPowerControlGroup& slotGroup : _slotPowerCtrlGroups)
    {
        // Iterate over slots
        for (uint32_t slotNum = slotGroup.startSlotNum; slotNum < slotGroup.startSlotNum + slotGroup.slotRecs.size(); slotNum++)
        {
            // Get slot record
            SlotPowerControlRec& slotRec = slotGroup.slotRecs[slotNum - slotGroup.startSlotNum];

            // Check time to change power control state
            switch (slotRec.pwrCtrlState)
            {
                case SLOT_POWER_OFF_PERMANENTLY:
                    break;
                case SLOT_POWER_OFF_PRE_INIT:
                    if (Raft::isTimeout(millis(), slotRec.pwrCtrlStateLastMs, STARTUP_POWER_OFF_MS))
                    {
#ifdef DEBUG_POWER_CONTROL_STATES
                        LOG_I(MODULE_PREFIX, "taskService slotNum %d now off pending cycling", slotNum);
#endif
                        setVoltageLevel(slotNum, POWER_CONTROL_OFF, false);
                        slotRec.setState(SLOT_POWER_OFF_PENDING_CYCLING, timeNowMs);
                    }
                    break;
                case SLOT_POWER_ON_WAIT_STABLE:
                    if (Raft::isTimeout(millis(), slotRec.pwrCtrlStateLastMs, VOLTAGE_STABILIZING_TIME_MS))
                    {
#ifdef DEBUG_POWER_CONTROL_STATES
                        LOG_I(MODULE_PREFIX, "taskService slotNum %d voltage is stable", slotNum);
#endif
                        slotRec.setState(SLOT_POWER_ON_LOW_V, timeNowMs);
                    }
                    break;
                case SLOT_POWER_OFF_PENDING_CYCLING:
                    if (Raft::isTimeout(millis(), slotRec.pwrCtrlStateLastMs, POWER_CYCLE_OFF_TIME_MS))
                    {
#ifdef DEBUG_POWER_CONTROL_STATES
                        LOG_I(MODULE_PREFIX, "taskService slotNum %d state is wait_stable", slotNum);
#endif
                        setVoltageLevel(slotNum, slotGroup.defaultLevel, false);
                        slotRec.setState(SLOT_POWER_ON_WAIT_STABLE, timeNowMs);
                    }
                    break;
                case SLOT_POWER_ON_LOW_V:
                case SLOT_POWER_ON_HIGH_V:
                    break;
                default:
                    break;
            }
        }
    }

    // Action changes to I2C IO expanders
    _busIOExpanders.syncI2CIOStateChanges(false, _busReqSyncFn);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Check if slot is power controlled
/// @param slotNum Slot number (1-based)
/// @return True if slot is power controlled
bool BusPowerController::isSlotPowerControlled(uint32_t slotNum)
{
    // Enaure hardware initialized
    if (!_hardwareInitialized)
        return false;

    // Get slot record
    SlotPowerControlRec* pSlotRec = getSlotRecord(slotNum);
    return (pSlotRec != nullptr);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Set voltage level for a slot
/// @param slotNum Slot number (1-based)
/// @param powerLevel enum PowerControlLevels
void BusPowerController::setVoltageLevel(uint32_t slotNum, PowerControlLevels powerLevel, bool actionIOExpanderChanges)
{
    // Get slot record
    SlotPowerControlRec* pSlotRec = getSlotRecord(slotNum);
    if (!pSlotRec)
    {
#ifdef WARN_ON_SLOT_0_NOT_POWER_CONTROLLED
        LOG_I(MODULE_PREFIX, "setVoltageLevel slotNum %d level %d slot is not power controlled", 
                    slotNum, powerLevel);
#endif
        return;
    }

    // Compute pin action to set the voltage level
    switch(powerLevel)
    {
        case POWER_CONTROL_OFF:
            // Turn off all pins
            for (VoltageLevelPinRec& vPinRec : pSlotRec->voltageLevelPins)
            {
                _busIOExpanders.virtualPinWrite(vPinRec.pinNum, !vPinRec.onLevel);
            }
            break;
        case POWER_CONTROL_LOW_V:
        case POWER_CONTROL_HIGH_V:
            uint32_t reqLevelIdx = powerLevel - POWER_CONTROL_LOW_V;
            // First turn off all pins other than voltage level we want
            uint32_t levelIdx = 0;
            for (VoltageLevelPinRec& vPinRec : pSlotRec->voltageLevelPins)
            {
                if (levelIdx != reqLevelIdx)
                {
                    _busIOExpanders.virtualPinWrite(vPinRec.pinNum, !vPinRec.onLevel);
                }
                levelIdx++;
            }
            // Now turn on the required voltage level
            VoltageLevelPinRec& vPinRec = pSlotRec->voltageLevelPins[reqLevelIdx];
            _busIOExpanders.virtualPinWrite(vPinRec.pinNum, vPinRec.onLevel);
            break;
    }

    // Action changes in I2C IO state (if required)
    if (actionIOExpanderChanges)
        _busIOExpanders.syncI2CIOStateChanges(false, _busReqSyncFn);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get slot record
/// @param slotNum Slot number (1-based)
/// @return Slot record or nullptr if not found
BusPowerController::SlotPowerControlRec* BusPowerController::getSlotRecord(uint32_t slotNum)
{
    // Iterate through the slot groups to find the group with this slot (or return nullptr)
    for (SlotPowerControlGroup& slotGroup : _slotPowerCtrlGroups)
    {
        if ((slotNum >= slotGroup.startSlotNum) && (slotNum < slotGroup.startSlotNum + slotGroup.slotRecs.size()))
        {
            return &slotGroup.slotRecs[slotNum - slotGroup.startSlotNum];
        }
    }
    return nullptr;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Power off all
void BusPowerController::powerOffAll()
{
    // Set main bus power off
    setVoltageLevel(0, POWER_CONTROL_OFF, false);

    // Iterate slot groups
    for (SlotPowerControlGroup& slotGroup : _slotPowerCtrlGroups)
    {
        // Iterate slots
        for (uint32_t slotNum = slotGroup.startSlotNum; slotNum < slotGroup.startSlotNum + slotGroup.slotRecs.size(); slotNum++)
        {
            setVoltageLevel(slotNum, POWER_CONTROL_OFF, false);
        }
    }

    // Action changes
    _busIOExpanders.syncI2CIOStateChanges(true, _busReqSyncFn);
}
