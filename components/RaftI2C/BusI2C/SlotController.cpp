/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// SlotController
//
// Rob Dobson 2026
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "SlotController.h"
#include "RaftBus.h"
#include "RaftJson.h"
#include "Logger.h"
#include <strings.h>

// Uncomment for verbose setup / setMode logging
#define DEBUG_SLOT_CONTROLLER_SETUP
#define DEBUG_SLOT_CONTROLLER_SETMODE

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Setup
bool SlotController::setup(const RaftJsonIF& slotsCfg, RaftBus* pBus)
{
    _pBus = pBus;
    _slots.clear();

    // Get the slots array
    std::vector<String> slotJsonStrs;
    slotsCfg.getArrayElems("slots", slotJsonStrs);
    _slots.resize(slotJsonStrs.size());
    for (size_t i = 0; i < slotJsonStrs.size(); ++i)
    {
        RaftJson slotJson(slotJsonStrs[i]);
        SlotEntry& slot = _slots[i];
        slot.serEnPin = slotJson.getInt("serEnPin", -1);
        slot.serEnLevel = slotJson.getInt("serEnLevel", 0);
        slot.rxPin = slotJson.getInt("rxPin", -1);
        slot.txPin = slotJson.getInt("txPin", -1);
        slot.defaultMode = parseDefaultModeField(slotJson);
        slot.currentMode = SlotMode::I2C;
    }

#ifdef DEBUG_SLOT_CONTROLLER_SETUP
    LOG_I(MODULE_PREFIX, "setup numSlots %d bus %s",
          (int)_slots.size(),
          _pBus ? _pBus->getBusName().c_str() : "(none)");
    for (size_t i = 0; i < _slots.size(); ++i)
    {
        const SlotEntry& s = _slots[i];
        LOG_I(MODULE_PREFIX, "  slot %d serEnPin=%d serEnLevel=%d rxPin=%d txPin=%d defaultMode=%s",
              (int)i + 1, s.serEnPin, s.serEnLevel, s.rxPin, s.txPin, modeToStr(s.defaultMode));
    }
#endif

    return true;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Apply default modes from configuration
void SlotController::applyDefaults()
{
    // First pass: drive every serial-enable pin to its inactive (I2C) level so we have a
    // known-good starting state regardless of any prior IO-expander state.
    for (size_t i = 0; i < _slots.size(); ++i)
    {
        const SlotEntry& s = _slots[i];
        if (s.serEnPin >= 0)
            applySerialEnableLevel(s, false);
    }

    // Second pass: any slot whose default is a serial mode is moved into that mode.
    for (size_t i = 0; i < _slots.size(); ++i)
    {
        if (_slots[i].defaultMode != SlotMode::I2C)
            setMode((uint32_t)i + 1, _slots[i].defaultMode);
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Set the mode of a slot
RaftRetCode SlotController::setMode(uint32_t slotNum, SlotMode newMode)
{
    if ((slotNum == 0) || (slotNum > _slots.size()))
    {
        LOG_W(MODULE_PREFIX, "setMode slotNum %d out of range (1..%d)", slotNum, (int)_slots.size());
        return RAFT_INVALID_DATA;
    }
    if (!_pBus)
    {
        LOG_W(MODULE_PREFIX, "setMode no bus");
        return RAFT_INVALID_OPERATION;
    }

    SlotEntry& slot = _slots[slotNum - 1];
    SlotMode oldMode = slot.currentMode;
    if (oldMode == newMode)
    {
#ifdef DEBUG_SLOT_CONTROLLER_SETMODE
        LOG_I(MODULE_PREFIX, "setMode slot %d already in mode %s (no-op)", slotNum, modeToStr(newMode));
#endif
        return RAFT_OK;
    }

    bool oldIsSerial = isModeSerial(oldMode);
    bool newIsSerial = isModeSerial(newMode);

    RaftRetCode retc = RAFT_OK;

    if (!oldIsSerial && newIsSerial)
    {
        // I2C -> Serial*
        // 1) Mask off mux data for this slot so the scanner / poller stops touching it.
        retc = _pBus->enableSlot(slotNum, true, false);
        if (retc != RAFT_OK)
        {
            LOG_W(MODULE_PREFIX, "setMode slot %d: enableSlot(false) failed retc=%d", slotNum, retc);
            return retc;
        }
        // 2) Drive the serial-enable pin to its active level.
        if (slot.serEnPin >= 0)
        {
            RaftRetCode pinRet = applySerialEnableLevel(slot, true);
            if (pinRet != RAFT_OK)
            {
                // Roll back the mux change.
                _pBus->enableSlot(slotNum, true, true);
                LOG_W(MODULE_PREFIX, "setMode slot %d: virtualPinsSet failed retc=%d (rolled back)", slotNum, pinRet);
                return pinRet;
            }
        }
    }
    else if (oldIsSerial && !newIsSerial)
    {
        // Serial* -> I2C
        // 1) Drive the serial-enable pin back to its inactive level.
        if (slot.serEnPin >= 0)
        {
            RaftRetCode pinRet = applySerialEnableLevel(slot, false);
            if (pinRet != RAFT_OK)
            {
                LOG_W(MODULE_PREFIX, "setMode slot %d: virtualPinsSet (off) failed retc=%d", slotNum, pinRet);
                return pinRet;
            }
        }
        // 2) Re-enable mux data so the slot is visible to the scanner / poller.
        retc = _pBus->enableSlot(slotNum, true, true);
        if (retc != RAFT_OK)
        {
            LOG_W(MODULE_PREFIX, "setMode slot %d: enableSlot(true) failed retc=%d", slotNum, retc);
            return retc;
        }
    }
    else
    {
        // Serial* <-> Serial* : UART-layer transition only. Nothing to do at this layer
        // in Phase 1; Phase 2 will recreate the BusSerial / HalfDuplexSerialChannel adapter.
    }

    slot.currentMode = newMode;

#ifdef DEBUG_SLOT_CONTROLLER_SETMODE
    LOG_I(MODULE_PREFIX, "setMode slot %d %s -> %s", slotNum, modeToStr(oldMode), modeToStr(newMode));
#endif
    return RAFT_OK;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get the current mode of a slot
SlotController::SlotMode SlotController::getMode(uint32_t slotNum) const
{
    if ((slotNum == 0) || (slotNum > _slots.size()))
        return SlotMode::I2C;
    return _slots[slotNum - 1].currentMode;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Convert a SlotMode to a canonical string
const char* SlotController::modeToStr(SlotMode m)
{
    switch (m)
    {
        case SlotMode::I2C:        return "i2c";
        case SlotMode::SerialFull: return "serial-full";
        case SlotMode::SerialHalf: return "serial-half";
    }
    return "i2c";
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Parse a mode string
bool SlotController::parseModeStr(const char* pStr, SlotMode& outMode)
{
    if (!pStr)
        return false;
    if (strcasecmp(pStr, "i2c") == 0)
    {
        outMode = SlotMode::I2C;
        return true;
    }
    if (strcasecmp(pStr, "serial-full") == 0 || strcasecmp(pStr, "full") == 0
        || strcasecmp(pStr, "serial") == 0 /* legacy alias for serial-full */)
    {
        outMode = SlotMode::SerialFull;
        return true;
    }
    if (strcasecmp(pStr, "serial-half") == 0 || strcasecmp(pStr, "half") == 0)
    {
        outMode = SlotMode::SerialHalf;
        return true;
    }
    return false;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get a JSON object describing all slots and their modes
String SlotController::getStatusJson() const
{
    String json = "[";
    for (size_t i = 0; i < _slots.size(); ++i)
    {
        if (i > 0)
            json += ",";
        const SlotEntry& s = _slots[i];
        json += "{\"slot\":" + String((int)i + 1)
              + ",\"mode\":\"" + String(modeToStr(s.currentMode)) + "\""
              + ",\"defaultMode\":\"" + String(modeToStr(s.defaultMode)) + "\""
              + ",\"serEnPin\":" + String(s.serEnPin)
              + ",\"rxPin\":" + String(s.rxPin)
              + ",\"txPin\":" + String(s.txPin)
              + "}";
    }
    json += "]";
    return json;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Parse the defaultMode field, accepting the legacy serDefault boolean as a fallback
SlotController::SlotMode SlotController::parseDefaultModeField(const RaftJsonIF& slotJson)
{
    String modeStr = slotJson.getString("defaultMode", "");
    if (modeStr.length() > 0)
    {
        SlotMode m = SlotMode::I2C;
        if (parseModeStr(modeStr.c_str(), m))
            return m;
        LOG_W(MODULE_PREFIX, "parseDefaultModeField unknown defaultMode='%s' falling back to i2c", modeStr.c_str());
        return SlotMode::I2C;
    }
    // Legacy: serDefault non-zero means serial-full at boot
    int legacy = slotJson.getInt("serDefault", -1);
    if (legacy > 0)
    {
        LOG_W(MODULE_PREFIX, "parseDefaultModeField using legacy serDefault=%d -> serial-full (please migrate to defaultMode)", legacy);
        return SlotMode::SerialFull;
    }
    return SlotMode::I2C;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Drive the serEnPin to either its active (serial) or inactive (I2C) level
RaftRetCode SlotController::applySerialEnableLevel(const SlotEntry& slot, bool enableSerial)
{
    if (slot.serEnPin < 0 || !_pBus)
        return RAFT_OK;
    int pin = slot.serEnPin;
    uint8_t level = enableSerial ? (uint8_t)slot.serEnLevel
                                 : (uint8_t)(slot.serEnLevel == 0 ? 1 : 0);
    return _pBus->virtualPinsSet(1, &pin, &level, nullptr, nullptr);
}
