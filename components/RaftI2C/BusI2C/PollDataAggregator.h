/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Data aggregator
//
// Rob Dobson 2024
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "PollDataAggregatorIF.h"
#include <RaftThreading.h>
#include <cstring>

class PollDataAggregator : public PollDataAggregatorIF
{
public:
    /// @brief Constructor
    /// @param numResultsToStore Number of results to store
    /// @param resultSize Size of each result
    PollDataAggregator(uint32_t numResultsToStore, uint32_t resultSize)
    {
        // Access semaphore
        RaftMutex_init(_accessMutex);

        // Ring buffer
        _ringBuffer.resize(numResultsToStore*resultSize);
        _ringBufHeadOffset = 0;
        _ringBufCount = 0;
        _maxElems = numResultsToStore;
        _resultSize = resultSize;
    }

    ////////////////////////////////////////////////////////////////////////////
    /// @brief Clear the circular buffer
    void clear() override
    {
        // Obtain access
        if (!RaftMutex_lock(_accessMutex, RAFT_MUTEX_WAIT_FOREVER))
            return;
        _ringBufHeadOffset = 0;
        _ringBufCount = 0;
        RaftMutex_unlock(_accessMutex);
    }

    ////////////////////////////////////////////////////////////////////////////
    /// @brief Put a vector of uint8_t data to one slot in the circular buffer
    /// @param data Data to add
    bool put(uint64_t timeNowUs, const std::vector<uint8_t>& data) override
    {
        // Check buffer size > size of a single result
        if (data.size() != _resultSize)
            return false;

        // Obtain access
        if (!RaftMutex_lock(_accessMutex, RAFT_MUTEX_WAIT_FOREVER))
            return false;

        // Add data
        memcpy(_ringBuffer.data() + _ringBufHeadOffset, data.data(), _resultSize);

        // Update ring buffer
        _ringBufHeadOffset += _resultSize;
        if (_ringBufHeadOffset >= _ringBuffer.size())
            _ringBufHeadOffset = 0;
        if (_ringBufCount < _maxElems)
            _ringBufCount++;

        // Store the latest values
        _latestValue = data;
        _lastestValueTimeUs = timeNowUs;
        _latestValueIsNew = true;

        // Release access
        RaftMutex_unlock(_accessMutex);
        return true;
    }

    ////////////////////////////////////////////////////////////////////////////
    /// @brief Get the last vector of uint8_t data from the circular buffer
    /// @param data (output) Data to get
    /// @return true if data available
    bool get(std::vector<uint8_t>& data) override
    {
        // Clear data
        data.clear();

        // Obtain access
        if (!RaftMutex_lock(_accessMutex, RAFT_MUTEX_WAIT_FOREVER))
            return false;

        // Check if buffer empty
        bool dataAvailble = _ringBufCount != 0;
        if (dataAvailble)
        {        
            // Get position of tail
            uint32_t pos = (_ringBufHeadOffset + _ringBuffer.size() - _ringBufCount*_resultSize) % _ringBuffer.size();

            // Copy data
            data.resize(_resultSize);
            memcpy(data.data(), _ringBuffer.data() + pos, _resultSize);

            // Update ring buffer count
            _ringBufCount--;
        }

        // Release access
        RaftMutex_unlock(_accessMutex);
        return dataAvailble;
    }

    ////////////////////////////////////////////////////////////////////////////
    /// @brief Get the last vector of uint8_t data from the circular buffer
    /// @param data (output) Data to get
    /// @param responseSize (output) Size of each response
    /// @param maxResponsesToReturn Maximum number of responses to return (pass 0 for all available)
    /// @return number of responses returned
    uint32_t get(std::vector<uint8_t>& data, uint32_t& responseSize, uint32_t maxResponsesToReturn) override
    {
        // Clear data
        data.clear();

        // Obtain access
        if (!RaftMutex_lock(_accessMutex, RAFT_MUTEX_WAIT_FOREVER))
            return 0;

        // Num responses to return
        uint32_t numResponsesToReturn = (maxResponsesToReturn == 0) || (_ringBufCount < maxResponsesToReturn) ? _ringBufCount : maxResponsesToReturn;
        responseSize = _resultSize;

        // Check if buffer empty
        if (numResponsesToReturn == 0)
        {
            // Release access
            RaftMutex_unlock(_accessMutex);
            return 0;
        }

        // Get position of tail
        uint32_t pos = (_ringBufHeadOffset + _ringBuffer.size() - _ringBufCount*_resultSize) % _ringBuffer.size();

        // Output data
        data.resize(numResponsesToReturn*_resultSize);
        uint8_t* pOutData = data.data();

        // Copy responses
        for (uint32_t i = 0; i < numResponsesToReturn; i++)
        {
            // Copy data
            memcpy(pOutData, _ringBuffer.data() + pos, _resultSize);

            // Update positions
            pOutData += _resultSize;
            pos += _resultSize;
            if (pos >= _ringBuffer.size())
                pos = 0;
        }

        // Update records remaining count
        _ringBufCount -= numResponsesToReturn;

        // Release access
        RaftMutex_unlock(_accessMutex);
        return numResponsesToReturn;
    }

    ////////////////////////////////////////////////////////////////////////////
    /// @brief Get the number of results stored
    uint32_t count() const override
    {
        // Obtain access (use mutable cast for const method)
        if (!RaftMutex_lock(const_cast<RaftMutex&>(_accessMutex), RAFT_MUTEX_WAIT_FOREVER))
            return 0;

        uint32_t resultCount = _ringBufCount;

        // Release access
        RaftMutex_unlock(const_cast<RaftMutex&>(_accessMutex));
        return resultCount;
    }

    ////////////////////////////////////////////////////////////////////////////
    /// @brief Get the latest data
    /// @param data (output) Data to get
    /// @return true if data is new
    bool getLatestValue(uint64_t& dataTimeUs, std::vector<uint8_t>& data) override
    {
        // Obtain access
        if (!RaftMutex_lock(_accessMutex, RAFT_MUTEX_WAIT_FOREVER))
            return false;

        // Check if data is new
        bool dataNew = _latestValueIsNew;
        dataTimeUs = _lastestValueTimeUs;
        data = _latestValue;
        _latestValueIsNew = false;

        // Release access
        RaftMutex_unlock(_accessMutex);

        // Return data
        return dataNew;
    }

private:
    // Circular buffer
    std::vector<uint8_t> _ringBuffer;
    uint16_t _ringBufHeadOffset = 0;
    uint16_t _ringBufCount = 0;
    uint16_t _resultSize = 0;
    uint16_t _maxElems = 0;

    // Latest value
    std::vector<uint8_t> _latestValue;
    uint64_t _lastestValueTimeUs = 0;
    bool _latestValueIsNew = false;

    // Access mutex
    mutable RaftMutex _accessMutex;

    // Debug
    static constexpr const char* MODULE_PREFIX = "PollDataAgg";
};
