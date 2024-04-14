/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Device status
//
// Rob Dobson 2024
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "DeviceStatus.h"

// #define DEBUG_DEVICE_STATUS

#ifdef DEBUG_DEVICE_STATUS
static const char* MODULE_PREFIX = "DeviceStatus";
#endif

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get pending ident poll requests 
/// @param timeNowMs time in ms (passed in to aid testing)
/// @param pollInfo (out) polling info
/// @return true if there is a pending request
bool DeviceStatus::getPendingIdentPollInfo(uint32_t timeNowMs, DevicePollingInfo& pollInfo)
{
    // Check if any pending
    if (Raft::isTimeout(timeNowMs, deviceIdentPolling.lastPollTimeMs, deviceIdentPolling.pollIntervalMs))
    {
        // Update timestamp
        deviceIdentPolling.lastPollTimeMs = timeNowMs;

        // Check poll requests isn't empty
        if (deviceIdentPolling.pollReqs.size() == 0)
            return false;

        // Copy polling info
        pollInfo = deviceIdentPolling;

        // Return true
        return true;
    }

    // Nothing pending
    return false;
}
