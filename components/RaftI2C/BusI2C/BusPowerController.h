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
#include "BusI2CRequestRec.h"

/// @brief Bus power controller handles power to either the whole bus OR on a per slot basis
class BusPowerController
{
public:
    // Constructor and destructor
    BusPowerController(BusI2CReqSyncFn busI2CReqSyncFn);
    virtual ~BusPowerController();

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

    // Check if slot power is controlled
    bool isSlotPowerControlled(uint32_t slotNum);

    // Maximum number of slots
    static const uint32_t MAX_SLOTS = 64;

private:
    // Bus access function
    BusI2CReqSyncFn _busI2CReqSyncFn;

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
    struct VoltageLevelPinRec
    {
        VoltageLevelPinRec()
        {
        }
        VoltageLevelPinRec(uint16_t pinNum, bool onLevel, bool isValid) :
            pinNum(pinNum), onLevel(onLevel), isValid(isValid)
        {
        }
        uint16_t pinNum:14 = 0;
        uint16_t onLevel:1 = 0;
        uint16_t isValid:1 = 0;
    };

    // Slot power control device types
    enum SlotPowerControllerType
    {
        SLOT_POWER_CONTROLLER_NONE = 0,
        SLOT_POWER_CONTROLLER_PCA9535 = 1
    };

    // PCA9535 registers
    static const uint8_t PCA9535_CONFIG_PORT_0 = 0x06;
    static const uint8_t PCA9535_OUTPUT_PORT_0 = 0x02;

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

    // Max pins on a power controller
    static const uint32_t IO_EXPANDER_MAX_PINS = 16;

    // IO exander records
    class IOExpanderRec
    {
    public:
        IOExpanderRec(uint32_t addr, uint32_t muxAddr, uint32_t muxChanIdx, int8_t muxResetPin, uint32_t virtualPinBase, uint32_t numPins) :
            addr(addr), muxAddr(muxAddr), muxChanIdx(muxChanIdx), muxResetPin(muxResetPin), virtualPinBase(virtualPinBase), numPins(numPins)
        {
        }

        /// @brief Update power control registers for all slots
        /// @param force true to force update (even if not dirty)
        /// @param busI2CReqSyncFn function to call to perform I2C request
        void update(bool force, BusI2CReqSyncFn busI2CReqSyncFn);

        // Power controller address
        uint8_t addr = 0;

        // Muliplexer address (0 if connected directly to main I2C bus)
        uint8_t muxAddr = 0;

        // Multiplexer channel index
        uint8_t muxChanIdx = 0;

        // Multiplexer reset pin
        int8_t muxResetPin = -1;

        // virtual pin (an extension of normal microcontroller pin numbering to include expander
        // pins based at this number for this device) start number
        uint16_t virtualPinBase = 0;

        // Number of pins
        uint16_t numPins = 0;

        // Power controller output register
        uint16_t outputsReg = 0xffff;

        // Power controller config bits record
        uint16_t configReg = 0xffff;

        // IO register is dirty
        bool ioRegDirty = true;
    };

    // IO exander records
    std::vector<IOExpanderRec> _ioExpanderRecs;

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

    /// @brief Find IO expander for a virtual pin
    /// @param vPin virtual pin number
    /// @return IO expander record or nullptr if not found
    IOExpanderRec* findIOExpanderFromVPin(uint32_t vPin)
    {
        for (uint32_t i = 0; i < _ioExpanderRecs.size(); i++)
        {
            if (vPin >= _ioExpanderRecs[i].virtualPinBase && 
                vPin < _ioExpanderRecs[i].virtualPinBase + _ioExpanderRecs[i].numPins)
            {
                return &_ioExpanderRecs[i];
            }
        }
        return nullptr;
    }

    /// @brief Set virtual pin level
    /// @param vPinRec virtual pin record
    /// @param turnOn true for on, false for off
    void setVirtualPinLevel(VoltageLevelPinRec& vPinRec, bool turnOn);

    /// @brief Action state changes in I2C IO expanders
    /// @param force true to force action
    void actionI2CIOStateChanges(bool force);

    /// @brief Turn all power off
    void powerOffAll();
};
