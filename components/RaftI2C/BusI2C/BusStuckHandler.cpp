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
bool BusStuckHandler::isStuck()
{
    // Check if pins are pulled up
    if (_sdaPin < 0 || _sclPin < 0)
        return false;
    
    // Check if SDA and SCL are high
    if (!(gpio_get_level(_sdaPin) && gpio_get_level(_sclPin)))
    {
        // Wait a moment and check again just in case it was a spurious measurement
        delayMicroseconds(1);
        _wasStuck = !(gpio_get_level(_sdaPin) && gpio_get_level(_sclPin));
        return _wasStuck;
    }
    _wasStuck = false;
    return false;
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
