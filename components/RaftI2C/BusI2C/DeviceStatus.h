
#pragma once 

#include "limits.h"
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
        deviceTypeIndex = USHRT_MAX;
        deviceIdentPolling.clear();
        dataAggregator.clear();
    }

    bool isValid() const
    {
        return deviceTypeIndex != USHRT_MAX;
    }

    // Get pending ident poll info
    bool getPendingIdentPollInfo(uint32_t timeNowMs, DevicePollingInfo& pollInfo);

    // Store poll results
    bool pollResultStore(uint32_t timeNowMs, const DevicePollingInfo& pollInfo, const std::vector<uint8_t>& pollResult)
    {
        return dataAggregator.put(pollResult);
    }

    // Get device type index
    uint16_t getDeviceTypeIndex() const
    {
        return deviceTypeIndex;
    }

    // Device type index
    uint16_t deviceTypeIndex = USHRT_MAX;

    // Device ident polling - polling related to the device type
    DevicePollingInfo deviceIdentPolling;

    // Data aggregator
    PollDataAggregator dataAggregator;
};
