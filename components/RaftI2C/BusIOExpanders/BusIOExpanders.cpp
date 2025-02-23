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

#define DEBUG_IO_EXPANDER_SETUP
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
    LOG_I(MODULE_PREFIX, "setup %s", ioExpRecStr.c_str());
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
/// @brief Set virtual pin levels on IO expander (pins must be on the same expander or on GPIO)
/// @param numPins - number of pins to set
/// @param pPinNums - array of pin numbers
/// @param pLevels - array of levels (0 for low)
/// @param pResultCallback - callback for result when complete/failed
/// @param pCallbackData - callback data
/// @return RAFT_OK if successful
RaftRetCode BusIOExpanders::virtualPinsSet(uint32_t numPins, const int* pPinNums, const uint8_t* pLevels, 
            VirtualPinSetCallbackType pResultCallback, void* pCallbackData)
{
    // Check if pins valid
    if ((numPins == 0) || (pPinNums == nullptr) || (pLevels == nullptr))
        return RAFT_INVALID_DATA;

    // Find the IO expander record for this virtual pin (or nullptr if not found)
    BusIOExpander* pBusIOExpander = findIOExpanderFromVPin(pPinNums[0]);

    // Handle not found
    if (pBusIOExpander == nullptr)
    {
#if defined(DEBUG_IO_BIT_SETTINGS)
        LOG_I(MODULE_PREFIX, "setVirtualPinsSet vPin %d not registered", pPinNums[0]);
#endif
        return RAFT_INVALID_DATA;
    }

    // Perform the IO expander operation
    RaftRetCode retc = pBusIOExpander->virtualPinsSet(numPins, pPinNums, pLevels, pResultCallback, pCallbackData);

#ifdef DEBUG_IO_BIT_SETTINGS
    LOG_I(MODULE_PREFIX, "virtualPinsSet IOExp first vPin %d level %d retc %d", pPinNums[0], pLevels[0], retc);
#endif

    return retc;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get virtual pin level on IO expander
/// @param pinNum - pin number
/// @param busI2CReqAsyncFn Function to call to perform I2C request
/// @param vPinCallback - callback for virtual pin changes
/// @param pCallbackData - callback data
/// @return RAFT_OK if successful
RaftRetCode BusIOExpanders::virtualPinRead(int pinNum, BusReqAsyncFn busI2CReqAsyncFn, 
                VirtualPinReadCallbackType vPinCallback, void* pCallbackData)
{
    // Check if pin valid
    if (pinNum < 0)
        return RAFT_INVALID_DATA;

    // Find the IO expander record for this virtual pin (or nullptr if it's a GPIO pin)
    BusIOExpander* pBusIOExpander = findIOExpanderFromVPin(pinNum);

    // Handle not found
    if (pBusIOExpander == nullptr)
        return RAFT_INVALID_DATA;

    // Perform the IO expander operation
    pBusIOExpander->virtualPinRead(pinNum, busI2CReqAsyncFn, vPinCallback, pCallbackData);

#ifdef DEBUG_IO_BIT_SETTINGS
    LOG_I(MODULE_PREFIX, "getVirtualPinLevel IOExp vPin %d callback pending", pinNum);
#endif

    return RAFT_OK;
}
