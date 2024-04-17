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

    void clear()
    {
        lastPollTimeUs = 0;
        pollIntervalUs = 0;
        pollResultSizeIncTimestamp = 0;
        pollReqs.clear();
    }

    void set(uint32_t pollIntervalMs, uint32_t numPollResultsToStore, uint32_t pollResultSizeIncTimestamp, 
                    std::vector<BusI2CRequestRec>& pollReqRecs)
    {
        // Set poll info
        this->pollIntervalUs = pollIntervalMs*1000;
        this->pollResultSizeIncTimestamp = pollResultSizeIncTimestamp;
        this->numPollResultsToStore = numPollResultsToStore;

        // Add a new poll request for each poll request
        this->pollReqs.clear();
        for (auto& pollReqRec : pollReqRecs)
            pollReqs.push_back(pollReqRec);
    }

    // cmdId used for ident-polling
    static const uint32_t DEV_IDENT_POLL_CMD_ID = UINT32_MAX;

    // Last poll time
    uint64_t lastPollTimeUs = 0;

    // Poll interval
    uint32_t pollIntervalUs = 0;

    // Num poll results to store
    uint32_t numPollResultsToStore = 1;

    // Size of poll result (including timestamp)
    uint32_t pollResultSizeIncTimestamp = 0;

    // Poll request rec
    std::vector<BusI2CRequestRec> pollReqs;

    // Poll result timestamp size
    static const uint32_t POLL_RESULT_TIMESTAMP_SIZE = 2;
};
