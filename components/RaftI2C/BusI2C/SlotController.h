/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// SlotController
//
// Per-bus slot mode controller. Each slot can be in one of three modes:
//   - I2C         : slot is an ordinary I2C channel via the bus multiplexer
//   - SerialFull  : slot is repurposed as a full-duplex UART (concurrent RX/TX)
//   - SerialHalf  : slot is repurposed as a half-duplex / single-wire UART
//
// In Phase 1 the controller only sequences the bus multiplexer's slot data
// and the serial-enable virtual pin so that the I2C scanner / poller stops
// touching slots that have been switched to a serial mode. UART allocation
// and the BusSerial / HalfDuplexSerialChannel adapters are added in Phase 2.
//
// SerialFull <-> SerialHalf transitions do NOT touch the multiplexer or the
// serial-enable pin in Phase 1 (or in Phase 2): they only differ in how
// the UART layer drives the bus.
//
// Rob Dobson 2026
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <vector>
#include "RaftArduino.h"
#include "RaftRetCode.h"
#include "RaftJsonIF.h"

class RaftBus;

class SlotController
{
public:
    /// @brief Slot mode
    enum class SlotMode : uint8_t
    {
        I2C = 0,
        SerialFull = 1,
        SerialHalf = 2,
    };

    SlotController() = default;
    ~SlotController() = default;

    /// @brief Setup
    /// @param slotsCfg JSON config containing a "slots" array (one entry per slot, slot 1 = slots[0])
    /// @param pBus Pointer to the bus that owns the multiplexer (used for enableSlot / virtualPinsSet)
    /// @return true if setup was successful (always true unless config is malformed)
    bool setup(const RaftJsonIF& slotsCfg, RaftBus* pBus);

    /// @brief Apply each slot's defaultMode (called after the bus is up)
    void applyDefaults();

    /// @brief Set the mode of a slot
    /// @param slotNum 1-based slot number
    /// @param mode New mode
    /// @return RAFT_OK on success, RAFT_INVALID_DATA for an out-of-range slot, or the result of the
    ///         underlying enableSlot / virtualPinsSet calls.
    RaftRetCode setMode(uint32_t slotNum, SlotMode mode);

    /// @brief Get the current mode of a slot
    /// @param slotNum 1-based slot number
    /// @return SlotMode (defaults to I2C for an out-of-range slot)
    SlotMode getMode(uint32_t slotNum) const;

    /// @brief Get number of configured slots
    uint32_t getNumSlots() const { return (uint32_t)_slots.size(); }

    /// @brief Convert a mode to a canonical string
    static const char* modeToStr(SlotMode m);

    /// @brief Parse a mode string. Accepts "i2c", "serial-full" / "full",
    ///        "serial-half" / "half", and "serial" (legacy synonym for serial-full).
    /// @return true if parsed; on false outMode is unchanged.
    static bool parseModeStr(const char* pStr, SlotMode& outMode);

    /// @brief Get a JSON object describing all slots and their modes (for diagnostics)
    String getStatusJson() const;

private:

    // Per-slot configuration / state
    struct SlotEntry
    {
        // Config (from JSON)
        int serEnPin = -1;
        int serEnLevel = 0;
        int rxPin = -1;
        int txPin = -1;
        SlotMode defaultMode = SlotMode::I2C;

        // Current mode
        SlotMode currentMode = SlotMode::I2C;
    };
    std::vector<SlotEntry> _slots;

    // Bus pointer (not owned)
    RaftBus* _pBus = nullptr;

    // Helpers
    static SlotMode parseDefaultModeField(const RaftJsonIF& slotJson);
    bool isModeSerial(SlotMode m) const { return m == SlotMode::SerialFull || m == SlotMode::SerialHalf; }
    RaftRetCode applySerialEnableLevel(const SlotEntry& slot, bool enableSerial);

    // Module name for logging
    static constexpr const char* MODULE_PREFIX = "SlotController";
};
