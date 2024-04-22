/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// I2C Bus Extender Manager
//
// Rob Dobson 2024
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "BusI2CRequestRec.h"
#include "RaftJsonIF.h"
#include "driver/gpio.h"

class BusExtenderMgr
{
public:
    // Constructor and destructor
    BusExtenderMgr(BusI2CReqSyncFn busI2CReqSyncFn);
    virtual ~BusExtenderMgr();

    // Setup
    void setup(const RaftJsonIF& config);

    // Service
    void service();

    // Service called from I2C task
    void taskService();
    
    // State change on an element (may or may not be a bus extender)
    void elemStateChange(uint32_t addr, bool elemResponding);

    // Check if address is a bus extender
    bool isBusExtender(uint8_t addr)
    {
        return _isEnabled && (addr >= _minAddr) && (addr <= _maxAddr);
    }

    // Get count of bus extenders
    uint32_t getBusExtenderCount() const
    {
        return _busExtenderCount;
    }

    // Get min address of bus extenders
    uint32_t getMinAddr() const
    {
        return _minAddr;
    }

    // Get max address of bus extenders
    uint32_t getMaxAddr() const
    {
        return _maxAddr;
    }

    // Get list of active extender addresses
    void getActiveExtenderAddrs(std::vector<uint32_t>& activeExtenderAddrs)
    {
        activeExtenderAddrs.clear();
        uint32_t addr = _minAddr;
        for (BusExtender& busExtender : _busExtenderRecs)
        {
            if (busExtender.isOnline)
                activeExtenderAddrs.push_back(addr);
            addr++;
        }
    }

    // Enable one slot on bus extender(s)
    void enableOneSlot(uint32_t slotPlus1);

    // Set channels on extender
    RaftI2CCentralIF::AccessResultCode setChannels(uint32_t addr, uint32_t channelMask);

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Hardware reset of bus extenders
    void hardwareReset();

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get bus extender slots
    /// @return Bus extender slots
    const std::vector<uint8_t>& getBusExtenderSlots() const
    {
        return _busExtenderSlots;
    }

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get next slot
    /// @param slotPlus1 Slot number (1-based)
    /// @note this is used when scanning to work through all slots and then loop back to 0 (main bus)
    /// @return Next slot number (1-based)
    uint32_t getNextSlot(uint32_t slotPlus1);

    // Bus extender slot count
    static const uint32_t I2C_BUS_EXTENDER_SLOT_COUNT = 8;

    // Bus extender channels
    static const uint32_t I2C_BUS_EXTENDER_ALL_CHANS_OFF = 0;
    static const uint32_t I2C_BUS_EXTENDER_ALL_CHANS_ON = 0xff;

private:
    // Extender functionality enabled
    bool _isEnabled = true;

    // Bus access function
    BusI2CReqSyncFn _busI2CReqSyncFn;

    // Bus extender address range
    uint32_t _minAddr = I2C_BUS_EXTENDER_BASE;
    uint32_t _maxAddr = I2C_BUS_EXTENDER_BASE+I2C_BUS_EXTENDERS_MAX-1;

    // Bus extender reset pin(s)
    gpio_num_t _resetPin = GPIO_NUM_NC;
    gpio_num_t _resetPinAlt = GPIO_NUM_NC;

    // Power control device types
    enum PowerControlType
    {
        POWER_CONTROL_NONE = 0,
        POWER_CONTROL_PCA9535 = 1
    };

    // PCA9535 registers
    static const uint8_t PCA9535_CONFIG_PORT_0 = 0x06;
    static const uint8_t PCA9535_OUTPUT_PORT_0 = 0x02;

    // Bus extender record
    class BusExtender
    {
    public:
        // Flags
        bool isDetected:1 = false,
             isOnline:1 = false,
             isInitialised:1 = false,
             pwrCtrlDirty:1 = true;

        // Power control
        PowerControlType pwrCtrlType = POWER_CONTROL_NONE;
        uint16_t pwrCtrlAddr = 0;
        uint16_t pwrCtrlGPIOReg = 0xffff;

        // Slot stuck indicators
        uint8_t _slotStuckBits = 0;
    };

    // Bus extenders
    std::vector<BusExtender> _busExtenderRecs;

    // Number of bus extenders detected so far
    uint8_t _busExtenderCount = 0;

    // Bus extender slot array
    std::vector<uint8_t> _busExtenderSlots;

    // Init bus extender records
    void initBusExtenderRecs();

    // Setup power control
    void setupPowerControl(const RaftJsonIF& config);

    // Power levels
    enum PowerControlLevels
    {
        POWER_CONTROL_OFF = 0,
        POWER_CONTROL_3V3 = 1,
        POWER_CONTROL_5V = 2
    };

    // Default voltage level
    PowerControlLevels _defaultVoltageLevel = POWER_CONTROL_OFF;

    // Initialisation state for power control
    enum PowerControlInitState
    {
        POWER_CONTROL_INIT_NONE = 0,
        POWER_CONTROL_INIT_OFF = 1,
        POWER_CONTROL_INIT_ON = 2,
    };
    PowerControlInitState _powerControlInitState = POWER_CONTROL_INIT_NONE;
    uint32_t _powerControlInitLastMs = 0;
    static const uint32_t STARTUP_CHANGE_TO_DEFAULT_VOLTAGE_MS = 5000;

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Set power level for a slot (or all slots)
    /// @param slotPlus1 Slot number (0 is all slots)
    /// @param powerLevel Power level
    void setVoltageLevel(uint32_t slotPlus1, PowerControlLevels powerLevel);

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Update power control registers for a single slot
    /// @param slotPlus1 Slot number (1-based)
    /// @param powerLevel Power level
    void updatePowerControlRegs(uint32_t slotPlus1, PowerControlLevels powerLevel);

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Write the power control registers
    void writePowerControlRegisters();

    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /// @brief Get extender and slot index from slotPlus1
    /// @param slotPlus1 Slot number (1-based)
    /// @param extenderIdx Extender index
    /// @param slotIdx Slot index
    /// @return True if valid
    bool getExtenderAndSlotIdx(uint32_t slotPlus1, uint32_t& extenderIdx, uint32_t& slotIdx);
};
