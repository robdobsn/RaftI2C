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
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

class PollDataAggregator
{
public:
    PollDataAggregator()
    {
        // Access semaphore
        _accessMutex = xSemaphoreCreateMutex();
    }

    ////////////////////////////////////////////////////////////////////////////
    /// @brief Initialise circular buffer
    /// @param numResultsToStore Number of results to store
    /// @param resultSize Size of each result
    void init(uint32_t numResultsToStore, uint32_t resultSize)
    {
        _ringBuffer.resize(numResultsToStore*resultSize);
        _ringBufHeadOffset = 0;
        _ringBufCount = 0;
        _maxElems = numResultsToStore;
        _resultSize = resultSize;
    }

    ////////////////////////////////////////////////////////////////////////////
    /// @brief Clear the circular buffer
    void clear()
    {
        // Obtain access
        if (xSemaphoreTake(_accessMutex, portMAX_DELAY) != pdTRUE)
            return;
        _ringBufHeadOffset = 0;
        _ringBufCount = 0;
        xSemaphoreGive(_accessMutex);
    }

    ////////////////////////////////////////////////////////////////////////////
    /// @brief Put a vector of uint8_t data to one slot in the circular buffer
    /// @param data Data to add
    bool put(const std::vector<uint8_t>& data)
    {
        // Check buffer size > size of a single result
        if (data.size() != _resultSize)
            return false;

        // Obtain access
        if (xSemaphoreTake(_accessMutex, portMAX_DELAY) != pdTRUE)
            return false;

        // Add data
        memcpy(_ringBuffer.data() + _ringBufHeadOffset, data.data(), _resultSize);

        // Update ring buffer
        _ringBufHeadOffset += _resultSize;
        if (_ringBufHeadOffset >= _ringBuffer.size())
            _ringBufHeadOffset = 0;
        if (_ringBufCount < _maxElems)
            _ringBufCount++;

        // Release access
        xSemaphoreGive(_accessMutex);
        return true;
    }

    ////////////////////////////////////////////////////////////////////////////
    /// @brief Get the last vector of uint8_t data from the circular buffer
    /// @param data (output) Data to get
    /// @return true if data available
    bool get(std::vector<uint8_t>& data)
    {
        // Clear data
        data.clear();

        // Obtain access
        if (xSemaphoreTake(_accessMutex, portMAX_DELAY) != pdTRUE)
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
        xSemaphoreGive(_accessMutex);
        return dataAvailble;
    }

    ////////////////////////////////////////////////////////////////////////////
    /// @brief Get the last vector of uint8_t data from the circular buffer
    /// @param data (output) Data to get
    /// @param responseSize (output) Size of each response
    /// @param maxResponsesToReturn Maximum number of responses to return (pass 0 for all available)
    /// @return number of responses returned
    uint32_t get(std::vector<uint8_t>& data, uint32_t& responseSize, uint32_t maxResponsesToReturn)
    {
        // Clear data
        data.clear();

        // Obtain access
        if (xSemaphoreTake(_accessMutex, portMAX_DELAY) != pdTRUE)
            return 0;

        // Num responses to return
        uint32_t numResponsesToReturn = (maxResponsesToReturn == 0) || (_ringBufCount < maxResponsesToReturn) ? _ringBufCount : maxResponsesToReturn;
        responseSize = _resultSize;

        // Check if buffer empty
        if (numResponsesToReturn == 0)
        {
            // Release access
            xSemaphoreGive(_accessMutex);
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
        xSemaphoreGive(_accessMutex);
        return numResponsesToReturn;
    }

    ////////////////////////////////////////////////////////////////////////////
    /// @brief Get the number of results stored
    uint32_t count() const
    {
        // Obtain access
        if (!_accessMutex)
            return 0;
        if (xSemaphoreTake(_accessMutex, portMAX_DELAY) != pdTRUE)
            return 0;

        uint32_t resultCount = _ringBufCount;

        // Release access
        xSemaphoreGive(_accessMutex);
        return resultCount;
    }

private:
    std::vector<uint8_t> _ringBuffer;
    uint16_t _ringBufHeadOffset = 0;
    uint16_t _ringBufCount = 0;
    uint16_t _resultSize = 0;
    uint16_t _maxElems = 0;
    SemaphoreHandle_t _accessMutex = nullptr;
};
