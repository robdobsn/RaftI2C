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

// #define WARN_ON_SLOT_NOT_POWER_CONTROLLED

// #define DEBUG_POWER_CONTROL_SETUP
// #define DEBUG_POWER_CONTROL_STATES
// #define DEBUG_POWER_CONTROL_BIT_SETTINGS
// #define DEBUG_POWER_CONTROL_SLOT_STABLE
// #define DEBUG_POWER_CONTROL_SLOT_UNSTABLE

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Constructor
BusPowerController::BusPowerController(BusIOExpanders& busIOExpanders)
        : _busIOExpanders(busIOExpanders)
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
    if (_voltageLevelNames.size() >= POWER_CONTROL_MAX_LEVELS)
    {
        LOG_I(MODULE_PREFIX, "%s FAIL too many voltageLevels %d >= max %d (inc OFF) - I2C power control disabled", __func__, 
                    _voltageLevelNames.size(), POWER_CONTROL_MAX_LEVELS);
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
        int defaultLevelIdx = slotGroupElem.getInt("defaultLevelIdx", 0);

        // Check this is in the range of the voltage level names
        if ((defaultLevelIdx < 0) || (defaultLevelIdx >= POWER_CONTROL_MAX_LEVELS))
        {
            LOG_W(MODULE_PREFIX, "%s defaultLevelIdx %d INVALID (> %d)", __func__, 
                        defaultLevelIdx, POWER_CONTROL_MAX_LEVELS);
            continue;
        }

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
        _slotPowerCtrlGroups.push_back(SlotPowerControlGroup(groupName, startSlotNum, defaultLevelIdx, slotRecs));
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
        LOG_I(MODULE_PREFIX, "Slot group %s start %d defaultLevelIdx %d", slotGroup.groupName.c_str(),
                        slotGroup.startSlotNum, slotGroup.defaultLevelIdx);
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

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Check if power on a slot is stable
/// @param slotNum Slot number (0 is the main bus)
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
    if (!(pSlotRec->pwrCtrlState == SLOT_POWER_AT_REQUIRED_LEVEL))
    {
        LOG_I(MODULE_PREFIX, "isSlotPowerStable UNSTABLE slotNum %d pwrCtrlState %s", 
                    slotNum, getSlotPowerControlStateStr(pSlotRec->pwrCtrlState));
    }
#endif

#ifdef DEBUG_POWER_CONTROL_SLOT_STABLE
    // Debug
    LOG_I(MODULE_PREFIX, "isSlotPowerStable slotNum %d pwrCtrlState %s", 
                slotNum,
                getSlotPowerControlStateStr(pSlotRec->pwrCtrlState));
#endif

    // Check if power is stable
    return pSlotRec->pwrCtrlState == SLOT_POWER_AT_REQUIRED_LEVEL;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Power cycle slot
/// @param slotNum slot number (0 is the main bus)
void BusPowerController::powerCycleSlot(uint32_t slotNum, uint32_t timeMs)
{
    
#ifdef DEBUG_POWER_CONTROL_STATES
    LOG_I(MODULE_PREFIX, "powerCycleSlot POWER OFF slotNum %d slotNum %d timeMs %d", slotNum, slotNum, timeMs);
#endif

    // Turn the slot power off
    setVoltageLevel(slotNum, POWER_CONTROL_OFF);

    // Set the state to power off pending cycling
    setSlotState(slotNum, SLOT_POWER_OFF_DURING_CYCLING, timeMs);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Task loop (called from I2C task)
/// @param timeNowMs Time now in milliseconds
void BusPowerController::taskService(uint32_t timeNowMs)
{
    // Enaure hardware initialized
    if (!_hardwareInitialized)
        return;

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
                    if (Raft::isTimeout(timeNowMs, slotRec.pwrCtrlStateLastMs, STARTUP_POWER_OFF_MS))
                    {
#ifdef DEBUG_POWER_CONTROL_STATES
                        LOG_I(MODULE_PREFIX, "taskService slotNum %d now off pending cycling", slotNum);
#endif
                        setVoltageLevel(slotNum, POWER_CONTROL_OFF);
                        slotRec.setState(SLOT_POWER_OFF_DURING_CYCLING, timeNowMs);
                    }
                    break;
                case SLOT_POWER_ON_WAIT_STABLE:
                    if (Raft::isTimeout(timeNowMs, slotRec.pwrCtrlStateLastMs, VOLTAGE_STABILIZING_TIME_MS))
                    {
#ifdef DEBUG_POWER_CONTROL_STATES
                        LOG_I(MODULE_PREFIX, "taskService slotNum %d voltage is stable", slotNum);
#endif
                        slotRec.setState(SLOT_POWER_AT_REQUIRED_LEVEL, timeNowMs);
                    }
                    break;
                case SLOT_POWER_OFF_DURING_CYCLING:
                    if (Raft::isTimeout(timeNowMs, slotRec.pwrCtrlStateLastMs, POWER_CYCLE_OFF_TIME_MS))
                    {
#ifdef DEBUG_POWER_CONTROL_STATES
                        LOG_I(MODULE_PREFIX, "taskService slotNum %d state is wait_stable timeMd %d lastStateMs %d", 
                                slotNum, timeNowMs, slotRec.pwrCtrlStateLastMs);
#endif
                        if (slotRec.powerEnabled)
                            setVoltageLevel(slotNum, slotGroup.defaultLevelIdx);
                        slotRec.setState(SLOT_POWER_ON_WAIT_STABLE, timeNowMs);
                    }
                    break;
                case SLOT_POWER_AT_REQUIRED_LEVEL:
                    break;
                default:
                    break;
            }
        }
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Check if slot is power controlled
/// @param slotNum Slot number (0 is the main bus)
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
/// @param slotNum Slot number (0 is the main bus)
/// @param powerLevelIdx index of required power levels (POWER_CONTROL_OFF etc)
void BusPowerController::setVoltageLevel(uint32_t slotNum, uint32_t powerLevelIdx)
{
    // Get slot record
    SlotPowerControlRec* pSlotRec = getSlotRecord(slotNum);
    if (!pSlotRec)
    {
#ifdef WARN_ON_SLOT_NOT_POWER_CONTROLLED
        LOG_I(MODULE_PREFIX, "setVoltageLevel slotNum %d level %d slot is not power controlled", 
                    slotNum, powerLevelIdx);
#endif
        return;
    }

    // Store required power control level index
    pSlotRec->_slotReqPowerControlLevelIdx = powerLevelIdx;

    // Check for off (turn all vPins to off level)
    if (powerLevelIdx == POWER_CONTROL_OFF)
    {
        for (VoltageLevelPinRec& vPinRec : pSlotRec->voltageLevelPins)
        {
            _busIOExpanders.virtualPinSet(vPinRec.pinNum, OUTPUT, !vPinRec.onLevel);
#ifdef DEBUG_SET_VOLTAGE_LEVEL
            LOG_I(MODULE_PREFIX, "setVoltageLevel POWER_CONTROL_OFF slotNum %d level %d pin %d off", slotNum, powerLevelIdx, vPinRec.pinNum);
#endif
        }
    }

    // Check for other voltage levels (values start at powerLevelIdx == 1)
    else if (powerLevelIdx - 1 < pSlotRec->voltageLevelPins.size())
    {
        // First turn off all pins other than voltage level we want
        uint32_t levelIdx = 1;
        for (VoltageLevelPinRec& vPinRec : pSlotRec->voltageLevelPins)
        {
            if (levelIdx != powerLevelIdx)
            {
                _busIOExpanders.virtualPinSet(vPinRec.pinNum, OUTPUT, !vPinRec.onLevel);
#ifdef DEBUG_SET_VOLTAGE_LEVEL
                LOG_I(MODULE_PREFIX, "setVoltageLevel OTHER_OFF slotNum %d level %d pin %d off", slotNum, powerLevelIdx, vPinRec.pinNum);
#endif
            }
            levelIdx++;
        }

        // Now turn on the required voltage level
        VoltageLevelPinRec& vPinRec = pSlotRec->voltageLevelPins[powerLevelIdx - 1];
        _busIOExpanders.virtualPinSet(vPinRec.pinNum, OUTPUT, vPinRec.onLevel);
#ifdef DEBUG_SET_VOLTAGE_LEVEL
        LOG_I(MODULE_PREFIX, "setVoltageLevel slotNum PIN_ON %d level %d pin %d on", slotNum, powerLevelIdx, vPinRec.pinNum);
#endif
    }
    else
    {
#ifdef DEBUG_SET_VOLTAGE_LEVEL
        LOG_W(MODULE_PREFIX, "setVoltageLevel slotNum %d level %d INVALID", slotNum, powerLevelIdx);
#endif
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get slot record
/// @param slotNum Slot number (0 is the main bus)
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
    // Iterate slot groups
    for (SlotPowerControlGroup& slotGroup : _slotPowerCtrlGroups)
    {
        // Iterate slots
        for (uint32_t slotNum = slotGroup.startSlotNum; slotNum < slotGroup.startSlotNum + slotGroup.slotRecs.size(); slotNum++)
        {
            setVoltageLevel(slotNum, POWER_CONTROL_OFF);
        }
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Enable bus slot
/// @param slotNum - slot number
/// @param enablePower - true to enable, false to disable
void BusPowerController::enableSlot(uint32_t slotNum, bool enablePower)
{
    // Get slot record
    SlotPowerControlRec* pSlotRec = getSlotRecord(slotNum);
    if (pSlotRec)
    {
        pSlotRec->powerEnabled = enablePower;
    }
}