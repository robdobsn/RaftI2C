/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Device Polling Info
//
// Rob Dobson 2024
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <stdint.h>
#include <list>
#include "BusI2CRequestRec.h"

class DevicePollingInfo 
{
public:
    DevicePollingInfo()
    {
    }

    void setDeviceIdentPolling(uint32_t pollIntervalMs, std::vector<BusI2CRequestRec>& pollReqRecs)
    {
        // Clear existing dev ident poll requests
        devIdentPollReqInfos.clear();

        // Add a new poll request for each poll request
        for (auto& pollReqRec : pollReqRecs)
            devIdentPollReqInfos.push_back(PollReqInfo(pollIntervalMs, pollReqRec));
    }

    // cmdId used for ident-polling
    static const uint32_t DEV_IDENT_POLL_CMD_ID = UINT32_MAX;

    // Poll request info
    class PollReqInfo
    {
    public:
        PollReqInfo(uint32_t pollIntervalMs, const BusI2CRequestRec& pollReq) :
            pollIntervalMs(pollIntervalMs),
            pollReq(pollReq)
        {
        }

        // Last poll time
        uint32_t lastPollTimeMs = 0;

        // Poll interval
        uint32_t pollIntervalMs = 0;

        // Poll request rec
        BusI2CRequestRec pollReq;
    };

    // Device ident polling requests
    std::list<PollReqInfo> devIdentPollReqInfos;
};
