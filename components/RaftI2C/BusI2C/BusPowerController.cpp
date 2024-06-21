/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// I2C Bus Power Controller
//
// Rob Dobson 2024
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include <BusPowerController.h>
#include "RaftUtils.h"
#include "Logger.h"
#include "RaftJson.h"

#define WARN_ON_SLOT_0_NOT_POWER_CONTROLLED

// #define DEBUG_POWER_CONTROL_SETUP
// #define DEBUG_POWER_CONTROL_STATES
// #define DEBUG_POWER_CONTROL_BIT_SETTINGS
// #define DEBUG_POWER_CONTROL_SLOT_STABLE

static const char* MODULE_PREFIX = "BusPowerCtrl";

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Constructor
BusPowerController::BusPowerController(BusI2CReqSyncFn busI2CReqSyncFn)
        : _busI2CReqSyncFn(busI2CReqSyncFn)
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
    //////////////////////////////////////////////////////////////////////////
    // IO Expanders

    // Get array of IO expanders
    std::vector<String> ioExpanderArray;
    config.getArrayElems("ioExps", ioExpanderArray);
    _ioExpanderRecs.clear();
    for (RaftJson ioExpElem : ioExpanderArray)
    {
        // Get device type
        String ioExpDeviceType = ioExpElem.getString("dev", "");

        // Currently only supports PCA9535
        if (ioExpDeviceType != "PCA9535")
        {
            LOG_W(MODULE_PREFIX, "%s dev type %s INVALID", __func__, ioExpDeviceType.c_str());
            continue;
        }

        // Get device address
        uint32_t ioExpDeviceAddr = ioExpElem.getLong("addr", 0);
        if (ioExpDeviceAddr == 0)
        {
            LOG_W(MODULE_PREFIX, "%s addr 0x%02x INVALID", __func__, ioExpDeviceAddr);
            continue;
        }

        // Check if device is on a multiplexer
        uint32_t ioExpMuxAddr = ioExpElem.getLong("muxAddr", 0);
        uint32_t ioExpMuxChanIdx = ioExpElem.getLong("muxChanIdx", 0);
        int8_t ioExpMuxResetPin = ioExpElem.getLong("muxRstPin", -1);

        // Virtual pin number start
        int virtualPinBase = ioExpElem.getLong("vPinBase", -1);
        if (virtualPinBase < 0)
        {
            LOG_W(MODULE_PREFIX, "%s vPinBase %d INVALID", __func__, virtualPinBase);
            continue;
        }

        // Get num pins on the IO expander
        uint32_t numPins = ioExpElem.getLong("numPins", 0);
        if ((numPins == 0) || (numPins > IO_EXPANDER_MAX_PINS))
        {
            LOG_W(MODULE_PREFIX, "%s numPins %d INVALID", __func__, numPins);
            continue;
        }

        // Create a record for this IO expander
        _ioExpanderRecs.push_back(IOExpanderRec(ioExpDeviceAddr, ioExpMuxAddr, ioExpMuxChanIdx, ioExpMuxResetPin, virtualPinBase, numPins));
    }

    //////////////////////////////////////////////////////////////////////////
    // Voltage level names
    config.getArrayElems("voltageLevels", _voltageLevelNames);

    // Check number of levels
    if (_voltageLevelNames.size() != POWER_CONTROL_NUM_LEVELS)
    {
        LOG_W(MODULE_PREFIX, "%s voltageLevels size %d INVALID (should be %d)", __func__, 
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

    // Debug
#ifdef DEBUG_POWER_CONTROL_SETUP

    // Debug IO expanders
    String ioExpRecStr;
    for (IOExpanderRec& pwrCtrlRec : _ioExpanderRecs)
    {
        ioExpRecStr += Raft::formatString(100, "addr 0x%02x %s vPinBase %d numPins %d ; ", 
                pwrCtrlRec.addr, 
                pwrCtrlRec.muxAddr != 0 ? Raft::formatString(100, "muxAddr 0x%02x muxChanIdx %d", pwrCtrlRec.muxAddr, pwrCtrlRec.muxChanIdx).c_str() : "MAIN_BUS",
                pwrCtrlRec.virtualPinBase, pwrCtrlRec.numPins);
    }
    LOG_I(MODULE_PREFIX, "Power controllers: %s", ioExpRecStr.c_str());

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
        String slotRecsStr;
        uint32_t slotNum = slotGroup.startSlotNum;
        for (SlotPowerControlRec& slotRec : slotGroup.slotRecs)
        {
            String voltageLevelStr;
            for (VoltageLevelPinRec& vPinRec : slotRec.voltageLevelPins)
            {
                voltageLevelStr += Raft::formatString(100, "vPin %d onLvl %d valid %d ; ", vPinRec.pinNum, vPinRec.onLevel, vPinRec.isValid);
            }
            slotRecsStr += Raft::formatString(500, "slotNum %d: vLevels %s\n", slotNum, voltageLevelStr.c_str());
            slotNum++;
        }
        slotGroupStr += Raft::formatString(500, "Group %s: startSlot %d defaultLevel %d\n%s", 
                slotGroup.groupName.c_str(), slotGroup.startSlotNum, slotGroup.defaultLevel, slotRecsStr.c_str());
    }
    LOG_I(MODULE_PREFIX, "Slot groups ...\n%s", slotGroupStr.c_str());

#endif

}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Post-setup (called after all other modules have setup)
/// @return True if hardware initialized ok
bool BusPowerController::postSetup()
{
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
/// @param slotNum Slot number (1-based)
/// @return True if power is stable
bool BusPowerController::isSlotPowerStable(uint32_t slotNum)
{
    // Get the slot record - if it doesn't exist assume power is stable
    SlotPowerControlRec* pSlotRec = getSlotRecord(slotNum);
    if (!pSlotRec)
    {
        // Check if slot is 0 (main bus) power is stable
        pSlotRec = getSlotRecord(0);
        if (!pSlotRec)
        {
#ifdef DEBUG_POWER_CONTROL_SLOT_STABLE
            LOG_I(MODULE_PREFIX, "isSlotPowerStable slotNum %d SLOT NOT FOUND! so saying yes", slotNum);
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

    // // Check for slot 0 (power cycle bus)
    // if (slotNum == 0)
    // {
    //     // Turn off the main bus power
    //     setVoltageLevel(0, POWER_CONTROL_OFF, true);

    //     // Set state
    //     _mainBusPowerControlState = MAIN_BUS_POWER_OFF;
    //     _busPowerControlStateLastMs = millis();
    //     return;
    // }

    // // Turn the slot power off
    // setVoltageLevel(slotNum, POWER_CONTROL_OFF, true);

    // // Set the state to power off pending cycling
    // setSlotState(slotNum, SLOT_POWER_OFF_PENDING_CYCLING, millis());
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
                        setVoltageLevel(slotNum, POWER_CONTROL_LOW_V, false);
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
    actionI2CIOStateChanges(false);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Check if slot is power controlled
/// @param slotNum Slot number (1-based)
/// @return True if slot is power controlled
bool BusPowerController::isSlotPowerControlled(uint32_t slotNum)
{
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
                setVirtualPinLevel(vPinRec, false);
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
                    setVirtualPinLevel(vPinRec, false);
                }
                levelIdx++;
            }
            // Now turn on the required voltage level
            setVirtualPinLevel(pSlotRec->voltageLevelPins[reqLevelIdx], true);
            break;
    }

    // Action changes in I2C IO state (if required)
    if (actionIOExpanderChanges)
        actionI2CIOStateChanges(false);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Set virtual pin level
/// @param vPinRec virtual pin record
/// @param turnOn true for on, false for off
void BusPowerController::setVirtualPinLevel(VoltageLevelPinRec& vPinRec, bool turnOn)
{
    // Check if pin valid
    if (!vPinRec.isValid)
        return;

    // Get the actual level to set
    bool setLevel = turnOn ? vPinRec.onLevel : !vPinRec.onLevel;

    // Find the IO expander record for this virtual pin (or nullptr if it's a GPIO pin)
    IOExpanderRec* pPwrCtrlRec = findIOExpanderFromVPin(vPinRec.pinNum);

    // If nullptr then it's a GPIO pin
    if (pPwrCtrlRec == nullptr)
    {
        // Set the GPIO pin
        pinMode(vPinRec.pinNum, OUTPUT);
        digitalWrite(vPinRec.pinNum, setLevel);

#ifdef DEBUG_POWER_CONTROL_BIT_SETTINGS
        LOG_I(MODULE_PREFIX, "setVirtualPinLevel GPIO vPin %d powerOnLevel %d turnOn %d setLevel %d", 
                    vPinRec.pinNum, vPinRec.onLevel, turnOn, setLevel);
#endif
        return;
    }

#ifdef DEBUG_POWER_CONTROL_BIT_SETTINGS
    uint32_t curOutputsReg = pPwrCtrlRec->outputsReg;
    uint32_t curConfigReg = pPwrCtrlRec->configReg;
#endif

    // Set/clear the bit in the IO output register
    uint32_t bitNum = vPinRec.pinNum - pPwrCtrlRec->virtualPinBase;
    if (setLevel)
        pPwrCtrlRec->outputsReg |= (1 << bitNum);
    else
        pPwrCtrlRec->outputsReg &= ~(1 << bitNum);

    // Clear the bit in the IO control register (make it an output)
    pPwrCtrlRec->configReg &= ~(1 << bitNum);

    // Set the dirty flag
    pPwrCtrlRec->ioRegDirty = true;

#ifdef DEBUG_POWER_CONTROL_BIT_SETTINGS
    LOG_I(MODULE_PREFIX, "setVirtualPinLevel vPin %d onLevel %d turnOn %d setLevel %d outputsReg 0x%04x -> 0x%04x configReg 0x%04x -> 0x%04x", 
            vPinRec.pinNum, vPinRec.onLevel, turnOn, setLevel,
            curOutputsReg, pPwrCtrlRec->outputsReg, curConfigReg, pPwrCtrlRec->configReg);
#endif
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
/// @brief Action state changes in I2C IO expanders
/// @param force Force the action (even if not dirty)
void BusPowerController::actionI2CIOStateChanges(bool force)
{
    // // Iterate through power control records
    // for (IOExpanderRec& pwrCtrlRec : _ioExpanderRecs)
    // {
    //     // Update power control registers
    //     pwrCtrlRec.update(force, _busI2CReqSyncFn);
    // }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Update power control registers for all slots
void BusPowerController::IOExpanderRec::update(bool force, BusI2CReqSyncFn busI2CReqSyncFn)
{
//     // Check if io expander is dirty
//     if (force || ioRegDirty)
//     {
//         // Setup multiplexer
//         if (muxAddr != 0)
//         {
//             // Check reset pin is output and set to 1
//             if (muxResetPin >= 0)
//             {
//                 pinMode(muxResetPin, OUTPUT);
//                 digitalWrite(muxResetPin, HIGH);
// #ifdef DEBUG_POWER_CONTROL_BIT_SETTINGS
//                 LOG_I(MODULE_PREFIX, "update muxResetPin %d set to HIGH", muxResetPin);
// #endif                
//             }

//             // Set the mux channel
//             BusI2CAddrAndSlot muxAddrAndSlot(muxAddr, 0);
//             uint8_t muxWriteData[1] = { (uint8_t)(1 << muxChanIdx) };
//             BusI2CRequestRec reqRec(BUS_REQ_TYPE_FAST_SCAN,
//                         muxAddrAndSlot,
//                         0, sizeof(muxWriteData),
//                         muxWriteData,
//                         0,
//                         0, 
//                         nullptr, 
//                         this);
//             busI2CReqSyncFn(&reqRec, nullptr);
//         }

//         // Set the output register first (to avoid unexpected power changes)
//         BusI2CAddrAndSlot addrAndSlot(addr, 0);
//         uint8_t outputPortData[3] = { PCA9535_OUTPUT_PORT_0, 
//                     uint8_t(outputsReg & 0xff), 
//                     uint8_t(outputsReg >> 8)};
//         BusI2CRequestRec reqRec(BUS_REQ_TYPE_FAST_SCAN,
//                     addrAndSlot,
//                     0, sizeof(outputPortData),
//                     outputPortData,
//                     0,
//                     0, 
//                     nullptr, 
//                     this);
//         bool rsltOk = busI2CReqSyncFn(&reqRec, nullptr) == RaftI2CCentralIF::ACCESS_RESULT_OK;

//         // Write the configuration register
//         uint8_t configPortData[3] = { PCA9535_CONFIG_PORT_0, 
//                     uint8_t(configReg & 0xff), 
//                     uint8_t(configReg >> 8)};
//         BusI2CRequestRec reqRec2(BUS_REQ_TYPE_FAST_SCAN,
//                     addrAndSlot,
//                     0, sizeof(configPortData),
//                     configPortData,
//                     0,
//                     0, 
//                     nullptr, 
//                     this);
//         rsltOk &= busI2CReqSyncFn(&reqRec2, nullptr) == RaftI2CCentralIF::ACCESS_RESULT_OK;

//         // Clear multiplexer
//         if (muxAddr != 0)
//         {
//             // Clear the mux channel
//             BusI2CAddrAndSlot muxAddrAndSlot(muxAddr, 0);
//             uint8_t muxWriteData[1] = { 0 };
//             BusI2CRequestRec reqRec(BUS_REQ_TYPE_FAST_SCAN,
//                         muxAddrAndSlot,
//                         0, sizeof(muxWriteData),
//                         muxWriteData,
//                         0,
//                         0, 
//                         nullptr, 
//                         this);
//             busI2CReqSyncFn(&reqRec, nullptr);
//         }

//         // Clear the dirty flag if result is ok
//         ioRegDirty = !rsltOk;

// #ifdef DEBUG_POWER_CONTROL_BIT_SETTINGS
//         LOG_I(MODULE_PREFIX, "update addr 0x%02x outputReg 0x%04x configReg 0x%04x force %d rslt %s", 
//                 addr, outputsReg, configReg, force, rsltOk ? "OK" : "FAIL");
// #endif
//     }
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
    actionI2CIOStateChanges(true);
}