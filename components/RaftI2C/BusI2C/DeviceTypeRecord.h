#pragma once

#include <stdint.h>
#include "RaftArduino.h"
#include "RaftBus.h"

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get decoded data record
/// @param pPollBuf buffer containing data
/// @param pollBufLen length of buffer
/// @param pStructOut pointer to structure to fill
/// @param structOutSize size of structure
/// @param maxRecCount maximum number of records to decode
/// @param decodeState decode state (used for stateful decoding including timestamp wrap-around handling)
/// @return number of records decoded
typedef uint32_t (*DeviceTypeRecordDecodeFn)(const uint8_t* pPollBuf, uint32_t pollBufLen, void* pStructOut, uint32_t structOutSize, 
            uint16_t maxRecCount, RaftBusDeviceDecodeState& decodeState);

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @class DeviceTypeRecord
/// @brief Device Type Record
class DeviceTypeRecord
{
public:
    const char* deviceType = nullptr;
    const char* addresses = nullptr;
    const char* detectionValues = nullptr;
    const char* initValues = nullptr;
    const char* pollInfo = nullptr;
    uint16_t pollDataSizeBytes = 0;
    const char* devInfoJson = nullptr;
    DeviceTypeRecordDecodeFn pollResultDecodeFn = nullptr;

    String getJson(bool includePlugAndPlayInfo) const
    {
        // Check if plug and play info required
        if (!includePlugAndPlayInfo)
        {
            return devInfoJson;
        }

        // Form JSON string
        String devTypeInfo = "{";
        devTypeInfo += "\"type\":\"" + String(deviceType) + "\",";
        devTypeInfo += "\"addr\":\"" + String(addresses) + "\",";
        devTypeInfo += "\"det\":\"" + String(detectionValues) + "\",";
        devTypeInfo += "\"init\":\"" + String(initValues) + "\",";
        devTypeInfo += "\"poll\":\"" + String(pollInfo) + "\",";
        devTypeInfo += "\"pollSize\":" + String(pollDataSizeBytes) + ",";
        devTypeInfo += "\"info\":" + String(devInfoJson);
        devTypeInfo += "}";
        return devTypeInfo;
    }
};

