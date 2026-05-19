/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// I2C Bus Stuck Handler
//
// Rob Dobson 2024
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "BusStuckHandler.h"
#include "Logger.h"
#include "RaftUtils.h"
#include "driver/gpio.h"

// #define DEBUG_BUS_STUCK_HANDLER

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Constructor
BusStuckHandler::BusStuckHandler(BusReqSyncFn busReqSyncFn) :
    _busReqSyncFn(busReqSyncFn)
{
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Destructor
BusStuckHandler::~BusStuckHandler()
{
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Setup
/// @param config Configuration
void BusStuckHandler::setup(const RaftJsonIF& config)
{
    // Get the bus pins so we can check if they are pulled-up ok
    _sdaPin = (gpio_num_t) config.getLong("sdaPin", -1);
    _sclPin = (gpio_num_t) config.getLong("sclPin", -1);

   // Debug
    LOG_I(MODULE_PREFIX, "setup sdaPin %d sclPin %d", _sdaPin, _sclPin);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Service
void BusStuckHandler::loopSync()
{
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Check bus stuck (must be called from I2C task)
/// @note Fast path: when SDA and SCL are both high (the common case) this returns immediately
///       with no added delay. SDA-alone-low is treated as a candidate glitch (e.g. slot-enable
///       noise coupling onto SDA) and is debounced by sampling across ~15us; if any sample
///       reads high the line is treated as not-stuck. SCL-low (with or without SDA-low) is
///       the classic genuinely-stuck signature and uses the original short confirmation only.
bool BusStuckHandler::isStuck()
{
    // Check if pins are configured
    if (_sdaPin < 0 || _sclPin < 0)
        return false;

    // Fast path: both lines idle high
    int sdaLevel = gpio_get_level(_sdaPin);
    int sclLevel = gpio_get_level(_sclPin);
    if (sdaLevel && sclLevel)
    {
        _wasStuck = false;
        return false;
    }

    // SDA-alone-low: the typical slot-enable glitch pattern. Sample across a ~15us window
    // (8 samples x ~2us). If any sample shows SDA high the low was a transient.
    if (!sdaLevel && sclLevel)
    {
        static constexpr int STUCK_SAMPLE_COUNT = 8;
        static constexpr int STUCK_SAMPLE_INTERVAL_US = 2;
        for (int i = 0; i < STUCK_SAMPLE_COUNT; i++)
        {
            delayMicroseconds(STUCK_SAMPLE_INTERVAL_US);
            if (gpio_get_level(_sdaPin))
            {
                _wasStuck = false;
                return false;
            }
        }
        // SDA remained low for the full window - treat as stuck
        _wasStuck = true;
        return true;
    }

    // SCL-low (with or without SDA-low): unusual / real stuck signature. Short debounce only.
    delayMicroseconds(1);
    _wasStuck = !(gpio_get_level(_sdaPin) && gpio_get_level(_sclPin));
    return _wasStuck;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Clear bus stuck by clocking
void BusStuckHandler::clearStuckByClocking()
{
    // Iterate
    for (int i = 0; i < I2C_BUS_STUCK_REPEAT_COUNT; i++)
    {
        // Attempt to clear bus stuck by clocking
        BusRequestInfo reqRec(BUS_REQ_TYPE_FAST_SCAN, 
                    I2C_BUS_STUCK_CLEAR_ADDR,
                    0, 0,
                    nullptr,
                    0,
                    0, 
                    nullptr, 
                    this);
        _busReqSyncFn(&reqRec, nullptr);
    }
}
