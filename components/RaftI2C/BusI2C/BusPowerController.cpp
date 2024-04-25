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

// #define DEBUG_POWER_CONTROL_SETUP
// #define DEBUG_POWER_CONTROL_STATES
// #define DEBUG_POWER_CONTROL_BIT_SETTINGS

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
    // Get array of power control info
    std::vector<String> pwrCtrlArray;
    config.getArrayElems("ctrl", pwrCtrlArray);

    // Handle power control elements
    for (RaftJson pwrCtrlElem : pwrCtrlArray)
    {
        // Get device type
        String pwrCtrlDeviceType = pwrCtrlElem.getString("dev", "");

        // Currently only supports PCA9535
        if (pwrCtrlDeviceType != "PCA9535")
        {
            LOG_W(MODULE_PREFIX, "setupPowerControl dev type %s INVALID", pwrCtrlDeviceType.c_str());
            continue;
        }

        // Get the power control device address
        uint32_t pwrCtrlDeviceAddr = pwrCtrlElem.getLong("addr", 0);
        if (pwrCtrlDeviceAddr == 0)
        {
            LOG_W(MODULE_PREFIX, "setupPowerControl addr 0x%02x INVALID", pwrCtrlDeviceAddr);
            continue;
        }

        // Get the min slot + 1 for the power controller
        uint32_t minSlotPlus1 = pwrCtrlElem.getLong("minSlotPlus1", 0);
        if (minSlotPlus1 == 0)
        {
            LOG_W(MODULE_PREFIX, "setupPowerControl minSlotPlus1 %d INVALID", minSlotPlus1);
            continue;
        }

        // Get num slots for the power controller
        uint32_t numSlots = pwrCtrlElem.getLong("numSlots", 0);
        if ((numSlots == 0) || (numSlots > POWER_CONTROLLER_MAX_SLOT_COUNT))
        {
            LOG_W(MODULE_PREFIX, "setupPowerControl numSlots %d INVALID", numSlots);
            continue;
        }

        // Create a record for this power controller
        _pwrCtrlRecs.push_back(PowerControlRec(pwrCtrlDeviceAddr, minSlotPlus1, numSlots));

#ifdef DEBUG_POWER_CONTROL_SETUP
        LOG_I(MODULE_PREFIX, "setupPowerControl dev %s addr 0x%02x minSlotPlus1 %d numSlots %d", 
                pwrCtrlDeviceType.c_str(), pwrCtrlDeviceAddr, minSlotPlus1, numSlots);
#endif
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Service
void BusPowerController::service()
{
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Check if power on a slot is stable
/// @param slotPlus1 Slot number (1-based)
/// @return True if power is stable
bool BusPowerController::isSlotPowerStable(uint32_t slotPlus1)
{
    // Get the power control record
    uint32_t slotIdx = 0;
    PowerControlRec* pPwrCtrlRec = getPowerControlRec(slotPlus1, slotIdx);
    if (pPwrCtrlRec == nullptr)
        return false;

    // Get the slot record
    PowerControlSlotRec& slotRec = pPwrCtrlRec->pwrCtrlSlotRecs[slotIdx];

    // Check if power is stable
    return (slotRec.pwrCtrlState == SLOT_POWER_ON_LOW_V) || (slotRec.pwrCtrlState == SLOT_POWER_ON_HIGH_V);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Power cycle slot
/// @param slotPlus1 Slot number (1-based)
void BusPowerController::powerCycleSlot(uint32_t slotPlus1)
{
    // Get the power control record
    uint32_t slotIdx = 0;
    PowerControlRec* pPwrCtrlRec = getPowerControlRec(slotPlus1, slotIdx);
    if (pPwrCtrlRec == nullptr)
        return;

    // Get the slot record
    PowerControlSlotRec& slotRec = pPwrCtrlRec->pwrCtrlSlotRecs[slotIdx];

    // Turn the slot power off
    pPwrCtrlRec->setVoltageLevel(slotIdx, POWER_CONTROL_OFF);

#ifdef DEBUG_POWER_CONTROL_STATES
    LOG_I(MODULE_PREFIX, "powerCycleSlot slotPlus1 %d slotIdx %d power off", slotPlus1, slotIdx);
#endif

    // Set the state to power off pending cycling
    slotRec.setState(SLOT_POWER_OFF_PENDING_CYCLING, millis());
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Task service (called from I2C task)
void BusPowerController::taskService(uint64_t timeNowUs)
{
    // Service state machine for each slot
    uint32_t timeNowMs = timeNowUs / 1000;
    for (PowerControlRec& pwrCtrlRec : _pwrCtrlRecs)
    {
        // Process slots
        for (uint32_t slotIdx = 0; slotIdx < pwrCtrlRec.pwrCtrlSlotRecs.size(); slotIdx++)
        {
            // Calculate slot number
            PowerControlSlotRec& slotRec = pwrCtrlRec.pwrCtrlSlotRecs[slotIdx];
            uint32_t slotPlus1 = pwrCtrlRec.minSlotPlus1 + slotIdx;

            // Check time to change power control state
            switch (slotRec.pwrCtrlState)
            {
                case SLOT_POWER_OFF_PERMANENTLY:
                    break;
                case SLOT_POWER_OFF_PRE_INIT:
                    if (Raft::isTimeout(millis(), slotRec.pwrCtrlStateLastMs, STARTUP_POWER_OFF_MS))
                    {
#ifdef DEBUG_POWER_CONTROL_STATES
                        LOG_I(MODULE_PREFIX, "taskService slotPlus1 %d slotIdx %d init voltage off", slotPlus1, slotIdx);
#endif
                        pwrCtrlRec.setVoltageLevel(slotIdx, POWER_CONTROL_OFF);
                        slotRec.setState(SLOT_POWER_OFF_PENDING_CYCLING, timeNowMs);
                    }
                    break;
                case SLOT_POWER_ON_WAIT_STABLE:
                    if (Raft::isTimeout(millis(), slotRec.pwrCtrlStateLastMs, VOLTAGE_STABILIZING_TIME_MS))
                    {
#ifdef DEBUG_POWER_CONTROL_STATES
                        LOG_I(MODULE_PREFIX, "taskService slotPlus1 %d slotIdx %d voltage is stable", slotPlus1, slotIdx);
#endif
                        slotRec.setState(SLOT_POWER_ON_LOW_V, timeNowMs);
                    }
                    break;
                case SLOT_POWER_OFF_PENDING_CYCLING:
                    if (Raft::isTimeout(millis(), slotRec.pwrCtrlStateLastMs, POWER_CYCLE_OFF_TIME_MS))
                    {
#ifdef DEBUG_POWER_CONTROL_STATES
                        LOG_I(MODULE_PREFIX, "taskService slotPlus1 %d slotIdx %d voltage 3V3", slotPlus1, slotIdx);
#endif
                        pwrCtrlRec.setVoltageLevel(slotIdx, POWER_CONTROL_3V3);
                        slotRec.setState(SLOT_POWER_ON_WAIT_STABLE, timeNowMs);
                    }
                    break;
                case SLOT_POWER_ON_LOW_V:
                    break;
                case SLOT_POWER_ON_HIGH_V:
                    break;
                default:
                    break;
            }
        }

        // Handle writing of any changes to power control registers
        pwrCtrlRec.updatePowerControlRegisters(true, _busI2CReqSyncFn);
    }

    // Process the state machine for 
    // // Check time to change power control initialisation state
    // switch (_powerControlInitState)
    // {
    //     case POWER_CONTROL_INIT_OFF:
    //         if (Raft::isTimeout(millis(), _powerControlInitLastMs, STARTUP_POWER_OFF_MS))
    //         {
    //             _powerControlInitState = POWER_CONTROL_INIT_ON;
    //             setVoltageLevel(0, _defaultVoltageLevel);
    //         }
    //         break;
    //     default:
    //         break;
    // }

    // // Handle power cycling state-machine for each slot
    // for (BusExtender& busExtender : _busExtenderRecs)
    // {
    //     // Check if power cycling is required
    //     if (busExtender.slotPowerCycleStatus == POWER_CYCLE_STATE_DISABLED)
    //         continue;

    //     // Check if power cycling is required
    //     if (busExtender.slotPowerCycleStatus == POWER_CYCLE_STATE_POWER_CYCLING_RESETTING)
    //     {
    //         // Check if time to reset
    //         if (Raft::isTimeout(millis(), busExtender.slotPowerCycleLastMs, _powerCycleResetMs))
    //         {
    //             // Reset the slot
    //             setVoltageLevel(busExtender.slotPowerCycleSlotPlus1, POWER_CONTROL_OFF);
    //             busExtender.slotPowerCycleStatus = POWER_CYCLE_STATE_POWER_CYCLING_ON;
    //             busExtender.slotPowerCycleLastMs = millis();
    //         }
    //     }
    //     else if (busExtender.slotPowerCycleStatus == POWER_CYCLE_STATE_POWER_CYCLING_ON)
    //     {
    //         // Check if time to power on
    //         if (Raft::isTimeout(millis(), busExtender.slotPowerCycleLastMs, _powerCycleOffMs))
    //         {
    //             // Power on the slot
    //             setVoltageLevel(busExtender.slotPowerCycleSlotPlus1, busExtender.slotPowerCycleVoltageLevel);
    //             busExtender.slotPowerCycleStatus = POWER_CYCLE_STATE_ON;
    //             busExtender.slotPowerCycleLastMs = millis();
    //         }
    //     }
    // }

    // // Handle any changes to power control
    // writePowerControlRegisters();

}

BusPowerController::PowerControlRec* BusPowerController::getPowerControlRec(uint32_t slotPlus1, uint32_t& slotIdx)
{
    // Iterate through power control records
    for (PowerControlRec& pwrCtrlRec : _pwrCtrlRecs)
    {
        if ((slotPlus1 >= pwrCtrlRec.minSlotPlus1) && (slotPlus1 < pwrCtrlRec.minSlotPlus1 + pwrCtrlRec.pwrCtrlSlotRecs.size()))
        {
            slotIdx = slotPlus1 - pwrCtrlRec.minSlotPlus1;
            return &pwrCtrlRec;
        }
    }
    return nullptr;
}

// /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// /// @brief Set voltage level for a slot (or all slots)
// /// @param slotPlus1 Slot number (0 is all slots)
// /// @param powerLevel Power level
// void BusPowerController::setVoltageLevel(uint32_t slotPlus1, PowerControlLevels powerLevel)
// {
//     // Check for one or more slots
//     if (slotPlus1 == 0)
//     {
//         // Iterate all slots
//         for (slotPlus1 = 1; slotPlus1 < I2C_BUS_EXTENDER_SLOT_COUNT * _busExtenderRecs.size() + 1; slotPlus1++)
//         {
//             // Update mask and output registers for the slot
//             updatePowerControlRegs(slotPlus1, powerLevel);
//         }

//         // Update state record for all slots
//         for (BusExtender& busExtender : _busExtenderRecs)
//         {
//             busExtender.slotPowerCycleStatus = (powerLevel == POWER_CONTROL_OFF) ? POWER_CYCLE_STATE_DISABLED : POWER_CYCLE_STATE_POWER_CYCLING_RESETTING;
//         }
//     }
//     else
//     {
//         // Update mask and output registers for the slot
//         updatePowerControlRegs(slotPlus1, powerLevel);
//     }
// }

// /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// /// @brief Update power control registers for a single slot
// /// @param slotPlus1 Slot number (1-based)
// /// @param powerLevel Power level
// void BusPowerController::updatePowerControlRegs(uint32_t slotPlus1, PowerControlLevels powerLevel)
// {
//     // Get the bus extender index and slot index
//     uint32_t slotIdx = 0;
//     uint32_t extenderIdx = 0;
//     if (!getExtenderAndSlotIdx(slotPlus1, extenderIdx, slotIdx))
//         return;

//     // Get the bus extender record
//     BusExtender& busExtenderRec = _busExtenderRecs[extenderIdx];

//     // Check if power control is for PCA9535 (only one option supported currently)
//     if (busExtenderRec.pwrCtrlType != POWER_CONTROL_PCA9535)
//         return;

//     // Base bit mask for the slot if two bits (one for 3V and one for 5V)
//     // There is both a configuration register and an output register and both are written on each change
//     // In both registers an enabled voltage output is a 0 in the corresponding bit position
//     // The base mask is inverse to this logic so that a shift and inversion results in the total mask required
//     uint16_t orMask = 0b11 << (slotIdx * 2);
//     static const uint16_t BASE_MASK_BITS[] = {0b00, 0b01, 0b10};
//     uint16_t baseMask = ~((BASE_MASK_BITS[powerLevel]) << (slotIdx * 2));

//     // Compute the new value for the control register (16 bits)
//     uint16_t newRegVal = (busExtenderRec.pwrCtrlGPIOReg | orMask) & baseMask;

// #ifdef DEBUG_BUS_EXTENDER_POWER_CONTROL
//     uint16_t prevReg = busExtenderRec.pwrCtrlGPIOReg;
// #endif

//     // Check if the mask has changed
//     if (newRegVal != busExtenderRec.pwrCtrlGPIOReg)
//     {
//         // Update the bus extender record
//         busExtenderRec.pwrCtrlGPIOReg = newRegVal;
//         busExtenderRec.pwrCtrlDirty = true;
//     }

// #ifdef DEBUG_BUS_EXTENDER_POWER_CONTROL
//     LOG_I(MODULE_PREFIX, "updatePowerLevel hasChanged %s slotPlus1 %d SlotPlus1-1 %d extenderIdx %d slotIdx %d powerLevel %d newRegVal 0x%02x(was 0x%02x)", 
//             busExtenderRec.pwrCtrlDirty ? "YES" : "NO",
//             slotPlus1, slotPlus1-1, extenderIdx, slotIdx, powerLevel, 
//             busExtenderRec.pwrCtrlGPIOReg, prevReg);
// #endif
// }

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Set voltage level for a slot
/// @param slotIdx
/// @param powerLevel
void BusPowerController::PowerControlRec::setVoltageLevel(uint32_t slotIdx, PowerControlLevels powerLevel)
{
    // Check slot
    if (slotIdx >= pwrCtrlSlotRecs.size())
        return;

    // Base bit mask for the slot uses two bits (one for 3V and one for 5V)
    // There is both a configuration register and an output register and both are written on each change
    // In both registers an enabled voltage output is a 0 in the corresponding bit position
    // The base mask is inverse to this logic so that a shift and inversion results in the total mask required
    uint16_t orMask = 0b11 << (slotIdx * 2);
    static const uint16_t BASE_MASK_BITS[] = {0b00, 0b01, 0b10};
    uint16_t baseMask = ~((BASE_MASK_BITS[powerLevel]) << (slotIdx * 2));

    // Compute the new value for the control register (16 bits)
    uint16_t newRegVal = (pwrCtrlGPIOReg | orMask) & baseMask;

#ifdef DEBUG_POWER_CONTROL_BIT_SETTINGS
    uint16_t prevReg = pwrCtrlGPIOReg;
#endif

    // Check if the mask has changed
    if (newRegVal != pwrCtrlGPIOReg)
    {
        // Update the bus extender record
        pwrCtrlGPIOReg = newRegVal;
        pwrCtrlDirty = true;
    }

#ifdef DEBUG_POWER_CONTROL_BIT_SETTINGS
    LOG_I(MODULE_PREFIX, "updatePowerLevel hasChanged %s slotIdx %d slotPlus1 %d powerLevel %d newRegVal 0x%02x(was 0x%02x)", 
            pwrCtrlDirty ? "YES" : "NO",
            slotIdx, minSlotPlus1+slotIdx, powerLevel, 
            pwrCtrlGPIOReg, prevReg);
#endif
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Write power control registers for all slots
void BusPowerController::PowerControlRec::updatePowerControlRegisters(bool onlyIfDirty, BusI2CReqSyncFn busI2CReqSyncFn)
{
    // Iterate through slots
    if (!onlyIfDirty || pwrCtrlDirty)
    {
        // Set the output register first (to avoid unexpected power changes)
        uint8_t writeData[3] = { PCA9535_OUTPUT_PORT_0, 
                    uint8_t(pwrCtrlGPIOReg & 0xff), 
                    uint8_t(pwrCtrlGPIOReg >> 8)};
        BusI2CAddrAndSlot addrAndSlot(pwrCtrlAddr, 0);
        BusI2CRequestRec reqRec(BUS_REQ_TYPE_FAST_SCAN,
                    addrAndSlot,
                    0, sizeof(writeData),
                    writeData,
                    0,
                    0, 
                    nullptr, 
                    this);
        bool rsltOk = busI2CReqSyncFn(&reqRec, nullptr) == RaftI2CCentralIF::ACCESS_RESULT_OK;

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
        rsltOk &= busI2CReqSyncFn(&reqRec2, nullptr) == RaftI2CCentralIF::ACCESS_RESULT_OK;

        // Clear the dirty flag if result is ok
        pwrCtrlDirty = !rsltOk;

#ifdef DEBUG_POWER_CONTROL_BIT_SETTINGS
        LOG_I(MODULE_PREFIX, "writePowerControlRegisters addr 0x%02x reg 0x%04x rslt %s", 
                pwrCtrlAddr, pwrCtrlGPIOReg, rsltOk ? "OK" : "FAIL");
#endif
    }
}
