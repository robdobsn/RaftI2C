/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Device status
//
// Rob Dobson 2024
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "DeviceStatus.h"

static const char* MODULE_PREFIX = "DeviceStatus";

/// @brief Get pending ident poll requests 
/// @param timeNowMs time in ms (passed in to aid testing)
/// @param busRequests (out) bus requests
void DeviceStatus::getPendingIdentPollRequests(uint32_t timeNowMs, std::vector<BusI2CRequestRec>& busRequests)
{
    // Clear the bus requests
    busRequests.clear();

    // Check if any pending
    if (Raft::isTimeout(timeNowMs, deviceIdentPolling.lastPollTimeMs, deviceIdentPolling.pollIntervalMs))
    {
        // Iterate polling records
        for (BusI2CRequestRec& reqRec : deviceIdentPolling.pollReqs)
        {
            // Append to list
            busRequests.push_back(reqRec);
            deviceIdentPolling.lastPollTimeMs = timeNowMs;
        }
    }
}
