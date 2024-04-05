/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Data aggregator
//
// Rob Dobson 2024
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <stdint.h>
#include <vector>

class PollDataAggregator
{
public:
    /// @brief Initialise circular buffer
    /// @param numResultsToStore Number of results to store
    void init(uint32_t numResultsToStore)
    {
        _data.resize(numResultsToStore);
        _ringBufPos = 0;
        _ringBufCount = 0;
    }

    /// @brief Put a vector of uint8_t data to one slot in the circular buffer
    /// @param data Data to add
    bool put(std::vector<uint8_t>& data)
    {
        // Check buffer size > 0
        if (_data.size() == 0)
            return false;

        // Add data
        _data[_ringBufPos] = data;

        // Update ring buffer
        _ringBufPos++;
        if (_ringBufPos >= _data.size())
            _ringBufPos = 0;
        if (_ringBufCount < _data.size())
            _ringBufCount++;
        return true;
    }

    /// @brief Get the last vector of uint8_t data from the circular buffer
    /// @param data (output) Data to get
    bool get(std::vector<uint8_t>& data)
    {
        // Clear data
        data.clear();

        // Check if buffer empty
        if (_ringBufCount == 0)
            return false;
        
        // Copy data
        uint32_t pos = (_ringBufPos + _data.size() - _ringBufCount) % _data.size();
        data = _data[pos];
        _data[pos].clear();

        // Update ring buffer
        _ringBufCount--;
        return true;
    }

    /// @brief Get the number of results stored
    uint32_t count()
    {
        return _ringBufCount;
    }

    /// @brief Get the max number of results stored
    uint32_t getMaxCount()
    {
        return _data.size();
    }

private:
    std::vector<std::vector<uint8_t>> _data;
    uint32_t _ringBufPos;
    uint32_t _ringBufCount;
};
