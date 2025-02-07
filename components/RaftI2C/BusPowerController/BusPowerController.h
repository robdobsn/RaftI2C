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
    BusPowerController(BusReqSyncFn busI2CReqSyncFn, BusIOExpanders& busIOExpanders);
    ~BusPowerController();

    // Setup
    void setup(const RaftJsonIF& config);
    bool postSetup();

    // Service
    void loop();

    // Service called from I2C task
    void taskService(uint64_t timeNowUs);

    // Check if slot has stable power
    bool isSlotPowerStable(uint32_t slotNum);

    /// @brief Power cycle slot
    /// @param slotNum slot number (1 based) (0 to power cycle bus)
    void powerCycleSlot(uint32_t slotNum);

    /// @brief Check if slot power is controlled
    /// @param slotNum slot number (1 based)
    /// @return true if slot power is controlled
    bool isSlotPowerControlled(uint32_t slotNum);

    // /// @brief Check if address is a bus power controller
    // /// @param i2cAddr address of bus power controller
    // /// @param muxAddr address of mux (0 if on main I2C bus)
    // /// @param muxChannel channel on mux
    // /// @return true if address is a bus power controller
    // bool isBusPowerController(uint16_t i2cAddr, uint16_t muxAddr, uint16_t muxChannel);

private:

    // Bus access function
    BusReqSyncFn _busReqSyncFn;

    // IO Expanders
    BusIOExpanders& _busIOExpanders;
    
    // Power control enabled
    bool _powerControlEnabled = false;
    
    // Hardware is initialized
    bool _hardwareInitialized = false;

    // Power levels
    enum PowerControlLevels
    {
        POWER_CONTROL_OFF = 0,
        POWER_CONTROL_LOW_V = 1,
        POWER_CONTROL_HIGH_V = 2,
    };
    static const uint32_t POWER_CONTROL_NUM_LEVELS = 3;

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
        SLOT_POWER_OFF_PENDING_CYCLING = 3,
        SLOT_POWER_ON_LOW_V = 4,
        SLOT_POWER_ON_HIGH_V = 5
    };

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

        // Time of last state change
        uint16_t pwrCtrlStateLastMs = 0;

        // Virtual pin records for each voltage level
        std::vector<VoltageLevelPinRec> voltageLevelPins;
    };

    // Power control slot group
    class SlotPowerControlGroup
    {
    public:
        SlotPowerControlGroup(const String& groupName, uint32_t startSlotNum, PowerControlLevels defaultLevel,
                        std::vector<SlotPowerControlRec>& slotRecs) :
            groupName(groupName), startSlotNum(startSlotNum), defaultLevel(defaultLevel), slotRecs(slotRecs)
        {
        }

        // Params
        String groupName;
        uint32_t startSlotNum = 0;
        PowerControlLevels defaultLevel = POWER_CONTROL_OFF;

        // Per slot info
        std::vector<SlotPowerControlRec> slotRecs;
    };

    // Slot groups
    std::vector<SlotPowerControlGroup> _slotPowerCtrlGroups;

    /// @brief Get slot record
    /// @param slotNum Slot number (1-based)
    /// @return Slot record or nullptr if not found
    SlotPowerControlRec* getSlotRecord(uint32_t slotNum);

    /// @brief Set slot state (and time of state change)
    /// @param slotNum slot number (1 based)
    /// @param newState
    /// @param timeMs 
    void setSlotState(uint32_t slotNum, SlotPowerControlState newState, uint32_t timeMs)
    {
        SlotPowerControlRec* pSlotRec = getSlotRecord(slotNum);
        if (pSlotRec)
            pSlotRec->setState(newState, timeMs);
    }

    /// @brief Set voltage level for a slot
    /// @param slotNum slot number (1 based)
    /// @param powerLevel enum PowerControlLevels
    void setVoltageLevel(uint32_t slotNum, PowerControlLevels powerLevel, bool actionIOExpanderChanges);

    /// @brief Turn all power off
    void powerOffAll();

    // Debug
    static constexpr const char* MODULE_PREFIX = "BusPwrCtrl";
};
