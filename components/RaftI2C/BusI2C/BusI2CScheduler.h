/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Scheduler for Round-Robin with Priority
//
// Rob Dobson 2019-2020
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <vector>
#include "Logger.h"
#include "RaftUtils.h"
#include "RaftArduino.h"
class BusI2CScheduler
{
public:
    BusI2CScheduler()
    {
        _pollCurIdx = 0;
        _elemWithFastestRateIdx = 0;
        _pollLastTimeMs = 0;
    }

    void clear()
    {
        _pollFreqsHz.clear();
        _pollCurIdx = 0;
        _elemWithFastestRateIdx = 0;
    }

    void addNode(double pollFreqHz)
    {
        _pollFreqsHz.push_back(pollFreqHz);
        prepStats();
    }

    void prepStats();
    int getNext();

private:
    // All these vectors should be the same length!
    std::vector<double> _pollFreqsHz;
    std::vector<uint16_t> _pollCountTotal;
    std::vector<uint16_t> _pollCountCur;

    // Current poll index (index into above arrays)
    // Moved on by getNext()
    uint32_t _pollCurIdx;

    // Minimum time between polls for any element
    uint32_t _pollMinTimeMs;
    uint32_t _elemWithFastestRateIdx;

    // Last time a poll occurred
    uint32_t _pollLastTimeMs;
};

