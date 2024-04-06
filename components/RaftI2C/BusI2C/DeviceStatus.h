
#pragma once 

#include "RaftUtils.h"
#include "DevicePollingInfo.h"
#include "PollDataAggregator.h"

class DeviceStatus
{
public:
    DeviceStatus()
    {
    }

    void clear()
    {
        deviceType.clear();
        deviceIdentPolling.clear();
        dataAggregator.clear();
    }

    bool isValid() const
    {
        return deviceType.length() > 0;
    }

    // Get pending requests
    void getPendingIdentPollRequests(uint32_t timeNowMs, std::vector<BusI2CRequestRec>& busRequests);

    // Store poll results
    void pollResultStore(const std::vector<uint8_t>& pollResult)
    {
        dataAggregator.put(pollResult);
    }

    // Device type
    String deviceType;

    // Device ident polling
    // This is polling related to the device identification - i.e. specified in the device info record
    // for the device type
    DevicePollingInfo deviceIdentPolling;

    // Data aggregator
    PollDataAggregator dataAggregator;
};
