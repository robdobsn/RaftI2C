/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// I2C Bus IO Expanders
//
// Rob Dobson 2025
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "BusIOExpanders.h"
#include "RaftUtils.h"
#include "Logger.h"
#include "RaftJson.h"

// #define VIRTUAL_PIN_ASSUME_GPIO_IF_NOT_REGISTERED

// #define DEBUG_IO_EXPANDER_SETUP
// #define DEBUG_IO_BIT_SETTINGS

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Constructor
BusIOExpanders::BusIOExpanders()
{
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Destructor
BusIOExpanders::~BusIOExpanders()
{
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Setup
/// @param config Configuration
void BusIOExpanders::setup(const RaftJsonIF& config)
{
    // Check if already setup
    if (_ioExpanders.size() > 0)
        return;

    // Get array of IO expanders
    std::vector<String> ioExpanderArray;
    config.getArrayElems("exps", ioExpanderArray);
    _ioExpanders.clear();
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
        _ioExpanders.push_back(BusIOExpander(ioExpDeviceAddr, ioExpMuxAddr, ioExpMuxChanIdx, ioExpMuxResetPin, virtualPinBase, numPins));
    }

    // Debug
#ifdef DEBUG_IO_EXPANDER_SETUP

    // Debug IO expanders
    String ioExpRecStr;
    for (BusIOExpander& ioExpander : _ioExpanders)
    {        
        ioExpRecStr += ioExpander.getDebugStr();
    }
    LOG_I(MODULE_PREFIX, "setup IO expanders: %s", ioExpRecStr.c_str());
#endif
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Sync state changes in I2C IO expanders
/// @param force Force the action (even if not dirty)
void BusIOExpanders::syncI2CIOStateChanges(bool force, BusReqSyncFn busI2CReqSyncFn)
{
    // Iterate through IO records
    for (BusIOExpander& ioExp : _ioExpanders)
    {
        // Update IO devices
        ioExp.updateSync(force, busI2CReqSyncFn);
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Set virtual pin mode on IO expander
/// @param pinNum - pin number
/// @param mode - mode INPUT/OUTPUT
/// @param level - level (only used for OUTPUT)
void BusIOExpanders::virtualPinSet(int pinNum, uint8_t mode, bool level)
{
    // Check if pin valid
    if (pinNum < 0)
        return;

    // Find the IO expander record for this virtual pin (or nullptr if it's a GPIO pin)
    BusIOExpander* pBusIOExpander = findIOExpanderFromVPin(pinNum);

    // Handle not found
    if (pBusIOExpander == nullptr)
    {
#ifdef VIRTUAL_PIN_ASSUME_GPIO_IF_NOT_REGISTERED
        // Set the GPIO pin
        pinMode(pinNum, mode);
        digitalWrite(pinNum, level);

#ifdef DEBUG_IO_BIT_SETTINGS
        LOG_I(MODULE_PREFIX, "setVirtualPinLevel GPIO pin %d level %d", pinNum, level);
#endif
#elif defined(DEBUG_IO_BIT_SETTINGS)
        LOG_I(MODULE_PREFIX, "setVirtualPinLevel vPin %d not registered", pinNum);
#endif
        return;
    }

    // Perform the IO expander operation
    pBusIOExpander->virtualPinSet(pinNum, mode, level);

#ifdef DEBUG_IO_BIT_SETTINGS
    LOG_I(MODULE_PREFIX, "setVirtualPinLevel IOExp vPin %d level %d", pinNum, level);
#endif
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get virtual pin level on IO expander
/// @param pinNum - pin number
/// @param busI2CReqAsyncFn Function to call to perform I2C request
/// @param vPinCallback - callback for virtual pin changes
/// @param pCallbackData - callback data
void BusIOExpanders::virtualPinRead(int pinNum, BusReqAsyncFn busI2CReqAsyncFn, VirtualPinCallbackType vPinCallback, void* pCallbackData)
{
    // Check if pin valid
    if (pinNum < 0)
        return;

    // Find the IO expander record for this virtual pin (or nullptr if it's a GPIO pin)
    BusIOExpander* pBusIOExpander = findIOExpanderFromVPin(pinNum);

    // Handle not found
    if (pBusIOExpander == nullptr)
    {
#ifdef VIRTUAL_PIN_ASSUME_GPIO_IF_NOT_REGISTERED
        // Read the GPIO pin
        bool level = digitalRead(pinNum);
        
        // Callback
        if (vPinCallback)
            vPinCallback(pCallbackData, VirtualPinResult(pinNum, level, RAFT_OK));
#endif
        return;
    }

    // Perform the IO expander operation
    pBusIOExpander->virtualPinRead(pinNum, busI2CReqAsyncFn, vPinCallback, pCallbackData);

#ifdef DEBUG_IO_BIT_SETTINGS
    LOG_I(MODULE_PREFIX, "getVirtualPinLevel IOExp vPin %d callback pending", pinNum);
#endif
}
