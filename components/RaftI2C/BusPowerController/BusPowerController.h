/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// I2C Bus Power Controller
//
// Rob Dobson 2024
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <stdint.h>
#include <vector>
#include "RaftJsonIF.h"
#include "BusRequestInfo.h"
#include "BusIOExpanders.h"

/// @brief Bus power controller handles power to either the whole bus OR on a per slot basis
class BusPowerController
{
public:
    // Constructor and destructor
    BusPowerController(BusIOExpanders& busIOExpanders);
    ~BusPowerController();

    // Setup
    void setup(const RaftJsonIF& config);
    bool postSetup();

    // Service
    void loop();

    // Service called from I2C task
    void taskService(uint32_t timeNowMs);

    // Check if slot has stable power
    bool isSlotPowerStable(uint32_t slotNum);

    /// @brief Power cycle slot
    /// @param slotNum slot number (0 is the main bus)
    /// @param timeMs time in milliseconds
    void powerCycleSlot(uint32_t slotNum, uint32_t timeMs);

    /// @brief Check if slot power is controlled
    /// @param slotNum slot number (0 is the main bus)
    /// @return true if slot power is controlled
    bool isSlotPowerControlled(uint32_t slotNum);

    /// @brief Enable bus slot
    /// @param slotNum - slot number
    /// @param enablePower - true to enable, false to disable
    void enableSlot(uint32_t slotNum, bool enablePower);

private:

    // IO Expanders
    BusIOExpanders& _busIOExpanders;
    
    // Power control enabled
    bool _powerControlEnabled = false;
    
    // Hardware is initialized
    bool _hardwareInitialized = false;

    // Power levels (POWER_CONTROL_OFF must be 0)
    static const uint32_t POWER_CONTROL_OFF = 0;
    static const uint32_t POWER_CONTROL_MAX_LEVELS = 3;

    // Voltage level names
    std::vector<String> _voltageLevelNames;

    // State machine timeouts
    static const uint32_t STARTUP_POWER_OFF_MS = 100;
    static const uint32_t VOLTAGE_STABILIZING_TIME_MS = 100;
    static const uint32_t POWER_CYCLE_OFF_TIME_MS = 500;

    // Voltage level pin record
    class VoltageLevelPinRec
    {
    public:
        VoltageLevelPinRec() : 
            pinNum(0), onLevel(0), isValid(0)
        {
        }
        VoltageLevelPinRec(uint16_t pinNum, bool onLevel, bool isValid) :
            pinNum(pinNum), onLevel(onLevel), isValid(isValid)
        {
        }
        uint16_t pinNum:14;
        uint16_t onLevel:1;
        uint16_t isValid:1;
    };

    // Slot power control device types
    enum SlotPowerControllerType
    {
        SLOT_POWER_CONTROLLER_NONE = 0,
        SLOT_POWER_CONTROLLER_PCA9535 = 1
    };

    // Slot power control state
    enum SlotPowerControlState
    {
        SLOT_POWER_OFF_PERMANENTLY = 0,
        SLOT_POWER_OFF_PRE_INIT = 1,
        SLOT_POWER_ON_WAIT_STABLE = 2,
        SLOT_POWER_OFF_DURING_CYCLING = 3,
        SLOT_POWER_AT_REQUIRED_LEVEL = 4
    };

    /// @brief Get slot power control state string
    /// @param state slot power control state
    /// @return state string
    static const char* getSlotPowerControlStateStr(SlotPowerControlState state)
    {
        switch (state)
        {
            case SLOT_POWER_OFF_PERMANENTLY: return "OFF_PERMANENTLY";
            case SLOT_POWER_OFF_PRE_INIT: return "OFF_PRE_INIT";
            case SLOT_POWER_ON_WAIT_STABLE: return "ON_WAIT_STABLE";
            case SLOT_POWER_OFF_DURING_CYCLING: return "OFF_PENDING_CYCLING";
            case SLOT_POWER_AT_REQUIRED_LEVEL: return "AT_REQUIRED_LEVEL";
            default: return "INVALID";
        }
    }

    // Slot records
    class SlotPowerControlRec
    {
    public:
        SlotPowerControlRec(std::vector<VoltageLevelPinRec>& voltageLevelPins) :
            voltageLevelPins(voltageLevelPins)
        {
        }
        void setState(SlotPowerControlState state, uint32_t timeNowMs)
        {
            pwrCtrlState = state;
            pwrCtrlStateLastMs = timeNowMs;
        }

        // Power state
        SlotPowerControlState pwrCtrlState = SLOT_POWER_OFF_PRE_INIT;

        // Power control level index
        uint8_t _slotReqPowerControlLevelIdx = POWER_CONTROL_OFF;

        // Time of last state change
        uint32_t pwrCtrlStateLastMs = 0;

        // Virtual pin records for each voltage level
        std::vector<VoltageLevelPinRec> voltageLevelPins;

        // Power enbled flag (can be used to disable power to a slot)
        bool powerEnabled = true;
    };

    // Power control slot group
    class SlotPowerControlGroup
    {
    public:
        SlotPowerControlGroup(const String& groupName, uint32_t startSlotNum, uint32_t defaultLevelIdx,
                        std::vector<SlotPowerControlRec>& slotRecs) :
            groupName(groupName), startSlotNum(startSlotNum), defaultLevelIdx(defaultLevelIdx), slotRecs(slotRecs)
        {
        }

        // Params
        String groupName;
        uint32_t startSlotNum = 0;
        uint32_t defaultLevelIdx = POWER_CONTROL_OFF;

        // Per slot info
        std::vector<SlotPowerControlRec> slotRecs;
    };

    // Slot groups
    std::vector<SlotPowerControlGroup> _slotPowerCtrlGroups;

    /// @brief Get slot record
    /// @param slotNum Slot number (0 is the main bus)
    /// @return Slot record or nullptr if not found
    SlotPowerControlRec* getSlotRecord(uint32_t slotNum);

    /// @brief Set slot state (and time of state change)
    /// @param slotNum slot number (0 is the main bus)
    /// @param newState
    /// @param timeMs 
    void setSlotState(uint32_t slotNum, SlotPowerControlState newState, uint32_t timeMs)
    {
        SlotPowerControlRec* pSlotRec = getSlotRecord(slotNum);
        if (pSlotRec)
            pSlotRec->setState(newState, timeMs);
    }

    /// @brief Set voltage level for a slot
    /// @param slotNum slot number (0 is the main bus)
    /// @param powerLevelIdx power control level index (POWER_CONTROL_OFF to turn off)
    void setVoltageLevel(uint32_t slotNum, uint32_t powerLevelIdx);

    /// @brief Power off all
    void powerOffAll();

    // Debug
    static constexpr const char* MODULE_PREFIX = "BusPwrCtrl";
};
