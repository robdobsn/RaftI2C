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

class BusPowerController
{
public:
    // Constructor and destructor
    BusPowerController(BusI2CReqSyncFn busI2CReqSyncFn);
    virtual ~BusPowerController();

    // Setup
    void setup(const RaftJsonIF& config);

    // Service
    void loop();

    // Service called from I2C task
    void taskService(uint64_t timeNowUs);

    // Check if slot has stable power
    bool isSlotPowerStable(uint32_t slotNum);

    // Power cycle slot
    void powerCycleSlot(uint32_t slotNum);

    // Check if slot power is controlled
    bool isSlotPowerControlled(uint32_t slotNum);

private:
    // Bus access function
    BusI2CReqSyncFn _busI2CReqSyncFn;

    // Power control device types
    enum PowerControllerType
    {
        POWER_CONTROLLER_NONE = 0,
        POWER_CONTROLLER_PCA9535 = 1
    };

    // PCA9535 registers
    static const uint8_t PCA9535_CONFIG_PORT_0 = 0x06;
    static const uint8_t PCA9535_OUTPUT_PORT_0 = 0x02;

    // Power levels
    enum PowerControlLevels
    {
        POWER_CONTROL_OFF = 0,
        POWER_CONTROL_3V3 = 1,
        POWER_CONTROL_5V = 2
    };

    // Power control state
    enum PowerControlSlotState
    {
        SLOT_POWER_OFF_PERMANENTLY = 0,
        SLOT_POWER_OFF_PRE_INIT = 1,
        SLOT_POWER_ON_WAIT_STABLE = 2,
        SLOT_POWER_OFF_PENDING_CYCLING = 3,
        SLOT_POWER_ON_LOW_V = 4,
        SLOT_POWER_ON_HIGH_V = 5
    };

    // Slot records
    class PowerControlSlotRec
    {
    public:
        PowerControlSlotState pwrCtrlState = SLOT_POWER_OFF_PRE_INIT;
        uint32_t pwrCtrlStateLastMs = 0;

        void setState(PowerControlSlotState state, uint32_t timeNowMs)
        {
            pwrCtrlState = state;
            pwrCtrlStateLastMs = timeNowMs;
        }
    };

    // Power control record
    class PowerControlRec
    {
    public:
        PowerControlRec(uint32_t addr, uint32_t minSlotNum, uint32_t numSlots) :
            pwrCtrlAddr(addr), minSlotNum(minSlotNum)
        {
            pwrCtrlSlotRecs.resize(numSlots);
        }

        /// @brief Set voltage level for a slot
        /// @param slotIdx 
        /// @param powerLevel
        void setVoltageLevel(uint32_t slotIdx, PowerControlLevels powerLevel);

        /// @brief Update power control registers for all slots
        void updatePowerControlRegisters(bool onlyIfDirty, BusI2CReqSyncFn busI2CReqSyncFn);

        // Power controller address
        uint16_t pwrCtrlAddr = 0;

        // Power controller GPIO bits record
        uint16_t pwrCtrlGPIOReg = 0xffff;

        // IO expander data is dirty
        bool ioExpanderDirty = true;

        // Per slot info
        uint16_t minSlotNum = 0;
        std::vector<PowerControlSlotRec> pwrCtrlSlotRecs;
    };

    // Max slots on a controller
    static const uint32_t POWER_CONTROLLER_MAX_SLOT_COUNT = 8;

    // State machine timeouts
    static const uint32_t STARTUP_POWER_OFF_MS = 100;
    static const uint32_t VOLTAGE_STABILIZING_TIME_MS = 100;
    static const uint32_t POWER_CYCLE_OFF_TIME_MS = 500;

    // Power control records
    std::vector<PowerControlRec> _pwrCtrlRecs;

    // Helpers
    PowerControlRec* getPowerControlRec(uint32_t slotNum, uint32_t& slotIdx);

};
