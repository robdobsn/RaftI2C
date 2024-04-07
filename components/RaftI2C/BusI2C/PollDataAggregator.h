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
    void init(uint32_t numResultsToStore, uint32_t resultSize)
    {
        _ringBuffer.resize(numResultsToStore*resultSize);
        _pRingBufBase = _ringBuffer.data();
        _ringBufHeadOffset = 0;
        _ringBufCount = 0;
        _maxElems = numResultsToStore;
        _resultSize = resultSize;
    }

    /// @brief Clear the circular buffer
    void clear()
    {
        _ringBufHeadOffset = 0;
        _ringBufCount = 0;
    }

    /// @brief Put a vector of uint8_t data to one slot in the circular buffer
    /// @param data Data to add
    bool put(const std::vector<uint8_t>& data)
    {
        // Check buffer size > size of a single result
        if (data.size() != _resultSize)
            return false;

        // Add data
        memcpy(_pRingBufBase + _ringBufHeadOffset, data.data(), _resultSize);

        // Update ring buffer
        _ringBufHeadOffset += _resultSize;
        if (_ringBufHeadOffset >= _ringBuffer.size())
            _ringBufHeadOffset = 0;
        if (_ringBufCount < _maxElems)
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
        
        // Get position of tail
        uint32_t pos = (_ringBufHeadOffset + _ringBuffer.size() - _ringBufCount*_resultSize) % _ringBuffer.size();

        // Copy data
        data.resize(_resultSize);
        memcpy(data.data(), _pRingBufBase + pos, _resultSize);

        // Update ring buffer count
        _ringBufCount--;
        return true;
    }

    /// @brief Get the number of results stored
    uint32_t count() const
    {
        return _ringBufCount;
    }

    /// @brief Get the max number of results stored
    uint32_t getMaxCount() const
    {
        return _maxElems;
    }

private:
    std::vector<uint8_t> _ringBuffer;
    uint8_t* _pRingBufBase = nullptr;
    uint16_t _ringBufHeadOffset = 0;
    uint16_t _ringBufCount = 0;
    uint16_t _resultSize = 0;
    uint16_t _maxElems = 0;
};
