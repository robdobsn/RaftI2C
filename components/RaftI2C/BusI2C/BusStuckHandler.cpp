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
#include "BusExtenderMgr.h"
#include "driver/gpio.h"

#define DEBUG_BUS_STUCK_HANDLER

static const char* MODULE_PREFIX = "BusStuck";

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Constructor
BusStuckHandler::BusStuckHandler(BusExtenderMgr& busExtenderMgr) : 
            _BusExtenderMgr(busExtenderMgr)
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
void BusStuckHandler::service()
{
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Check bus stuck
bool BusStuckHandler::isStuck()
{
    // Check if pins are pulled up
    if (_sdaPin < 0 || _sclPin < 0)
        return false;
    
    // Check if SDA and SCL are high
    return gpio_get_level(_sdaPin) && gpio_get_level(_sclPin);
}
