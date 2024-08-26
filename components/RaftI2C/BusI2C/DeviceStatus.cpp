/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Device status
//
// Rob Dobson 2024
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "DeviceStatus.h"

// #define DEBUG_DEVICE_STATUS

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get pending ident poll requests 
/// @param timeNowUs time in us (passed in to aid testing)
/// @param pollInfo (out) polling info
/// @return true if there is a pending request
bool DeviceStatus::getPendingIdentPollInfo(uint64_t timeNowUs, DevicePollingInfo& pollInfo)
{
    // Check if any pending
    if (Raft::isTimeout(timeNowUs, deviceIdentPolling.lastPollTimeUs, deviceIdentPolling.pollIntervalUs))
    {
        // Update timestamp
        deviceIdentPolling.lastPollTimeUs = timeNowUs;

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
