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

    void set(uint32_t pollIntervalMs, std::vector<BusI2CRequestRec>& pollReqRecs)
    {
        // Set poll interval
        this->pollIntervalMs = pollIntervalMs;
        this->pollReqs.clear();

        // Add a new poll request for each poll request
        for (auto& pollReqRec : pollReqRecs)
            pollReqs.push_back(pollReqRec);
    }

    // cmdId used for ident-polling
    static const uint32_t DEV_IDENT_POLL_CMD_ID = UINT32_MAX;


    // Last poll time
    uint32_t lastPollTimeMs = 0;

    // Poll interval
    uint32_t pollIntervalMs = 0;

    // Poll request rec
    std::vector<BusI2CRequestRec> pollReqs;
};
