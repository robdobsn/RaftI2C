
#pragma once 

#include "limits.h"
#include "RaftUtils.h"
#include "DevicePollingInfo.h"
#include "PollDataAggregator.h"

class DeviceStatus
{
public:
    static const uint16_t DEVICE_TYPE_INDEX_INVALID = USHRT_MAX;

    DeviceStatus()
    {
    }

    void clear()
    {
        deviceTypeIndex = DEVICE_TYPE_INDEX_INVALID;
        deviceIdentPolling.clear();
        dataAggregator.clear();
    }

    bool isValid() const
    {
        return deviceTypeIndex != DEVICE_TYPE_INDEX_INVALID;
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
    uint16_t deviceTypeIndex = DEVICE_TYPE_INDEX_INVALID;

    // Device ident polling - polling related to the device type
    DevicePollingInfo deviceIdentPolling;

    // Data aggregator
    PollDataAggregator dataAggregator;
};
