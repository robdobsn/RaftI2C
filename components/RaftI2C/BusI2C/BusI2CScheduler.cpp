/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Scheduler for Round-Robin with Priority
//
// Rob Dobson 2019-2020
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "BusI2CScheduler.h"

// #define DEBUG_POLLING_STATS
// #define DEBUG_POLLING_NEXT

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Prepare the scheduler
// The way the scheduler works is based on successive addition to simulate division
// Initially the element which is to be polled the fastest is located
// The minimum time between polls is then calculated based on the rate of that element
// Each device is then allocated a countTotal inversely proportional to its poll-rate
// relative to the fastest
// So the fastest (and others at the same speed) get a countTotal of 1
// And a device polled at 1/10th of that rate gets a countTotal of 10
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void BusI2CScheduler::prepStats()
{
    // Clear stats
    _pollCountTotal.clear();
    _pollCountCur.clear();
    _pollCurIdx = 0;

    // Sum the rates
    _elemWithFastestRateIdx = 0;
    double maxRateHz = 0;
    uint32_t curElemIdx = 0;
    for (double pollFreqHz : _pollFreqsHz)
    {
        if (maxRateHz < pollFreqHz)
        {
            maxRateHz = pollFreqHz;
            _elemWithFastestRateIdx = curElemIdx;
        }
        curElemIdx++;
    }

    // Set the minimum poll time - can't go faster than 1ms
    _pollMinTimeMs = 1000;
    if (maxRateHz > 0)
        _pollMinTimeMs = 1000/maxRateHz;
    if (_pollMinTimeMs < 1)
        _pollMinTimeMs = 1;

    // Since the test for timeout in Raft::isTimeout ensures that at least 1ms has elapsed
    // the average for interval is actually the minimum + 1ms - so fix that as long as
    // we aren't too close to zero
    if (_pollMinTimeMs >= 2)
        _pollMinTimeMs--;

    // Allocate each item a count which represents it poll rate
    for (double pollFreqHz : _pollFreqsHz)
    {
        // Total count for all elements
        uint32_t countTotal = 1;
        if (pollFreqHz != 0)
            countTotal = maxRateHz / pollFreqHz;
        _pollCountTotal.push_back(countTotal);
        _pollCountCur.push_back(0);
    }

    // Polling debug
#ifdef DEBUG_POLLING_STATS
    LOG_I(MODULE_PREFIX, "prepStats pollTimeMinMs %d pollCount %d maxRateHz %.1f fastestIdx %d", 
                _pollMinTimeMs, _pollCountTotal.size(), maxRateHz, _elemWithFastestRateIdx);
    for (uint32_t i = 0; i < _pollFreqsHz.size(); i++)
    {
        LOG_I(MODULE_PREFIX, "prepStats hzIdx %d pollCountTotal %d", i, _pollCountTotal[i]);
    }
#endif
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Get the next device index to poll
// Can return -1 if list is empty or not time to poll
// First checks if it is time to poll the element with the fastest rate
// Then uses the countTotal[] for each device to poll each in turn at a rate proportionate
// to the rate of the fastest
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

int BusI2CScheduler::getNext()
{
    // Check valid
    if (_pollFreqsHz.size() == 0)
    {
        // ESP_LOGV("SchedulerRRP", "list empty");
        return -1;
    }

    // Ensure current index is valid
    if (_pollCurIdx >= _pollFreqsHz.size())
        _pollCurIdx = 0;

    // Check if poll time elapsed for fastest element
    if ((_pollCurIdx == _elemWithFastestRateIdx) && (!Raft::isTimeout(millis(), _pollLastTimeMs, _pollMinTimeMs)))
        return -1;

#ifdef DEBUG_POLLING_NEXT
    LOG_I(MODULE_PREFIX, "getNext time for next poll lastTimeMs %d elapsed %d fastestRateIdx %d curIdx %d curCount[curIdx] %d totalCount[curIdx] %d", 
                _pollLastTimeMs, (int)Raft::timeElapsed(millis(), _pollLastTimeMs), _elemWithFastestRateIdx, 
                _pollCurIdx, _pollCountCur[_pollCurIdx], _pollCountTotal[_pollCurIdx]);
#endif

    // Loop through to find the next element to service
    for (int i = 0; i < _pollFreqsHz.size(); i++)
    {
        // Bump the current index and ensure valid
        _pollCurIdx++;
        if (_pollCurIdx >= _pollFreqsHz.size())
            _pollCurIdx = 0;

        // Inc cur count for this index
        _pollCountCur[_pollCurIdx]++;
        if (_pollCountCur[_pollCurIdx] >= _pollCountTotal[_pollCurIdx])
        {
            _pollCountCur[_pollCurIdx] = 0;
            // ESP_LOGV("SchedulerRRP", "returning %d", _pollCurIdx);
            if (_pollCurIdx == _elemWithFastestRateIdx)
                _pollLastTimeMs = millis();

            // When we come back check the next index
#ifdef DEBUG_POLLING_NEXT
            LOG_I(MODULE_PREFIX, "getNext returning %d pollLastTimeMs %d", _pollCurIdx, _pollLastTimeMs);
#endif
            return _pollCurIdx;
        }
    }

    // Should never get here
    _pollLastTimeMs = millis();
    LOG_D(MODULE_PREFIX, "SchedulerRRP: dropped out");
    return -1;
}
