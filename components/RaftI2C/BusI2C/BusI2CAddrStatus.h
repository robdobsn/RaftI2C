/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// I2C Address Status
//
// Rob Dobson 2024
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "RaftDeviceConsts.h"
#include "BusI2CConsts.h"
#include "DeviceStatus.h"
#include "BusI2CAddrAndSlot.h"

// I2C address status
class BusI2CAddrStatus
{
public:
    // Address and slot
    BusI2CAddrAndSlot addrAndSlot;

    // Online/offline count
    int8_t count = 0;

    // State
    bool isChange : 1 = false;
    bool isOnline : 1 = false;
    bool wasOnceOnline : 1 = false;
    bool slotResolved : 1 = false;
    bool isNewlyIdentified : 1 = false;

    // Access barring
    uint32_t barStartMs = 0;
    uint16_t barDurationMs = 0;

    // Min between data change callbacks
    uint32_t minTimeBetweenReportsMs = 0;
    uint32_t lastDataChangeReportTimeMs = 0;

    // Device status
    DeviceStatus deviceStatus;

    // Device data change callback and info
    RaftDeviceDataChangeCB dataChangeCB = nullptr;
    const void* pCallbackInfo = nullptr;

    // Handle responding
    bool handleResponding(bool isResponding, bool &flagSpuriousRecord);
    
    // Register for data change
    void registerForDataChange(RaftDeviceDataChangeCB dataChangeCB, uint32_t minTimeBetweenReportsMs, const void* pCallbackInfo)
    {
        this->dataChangeCB = dataChangeCB;
        this->pCallbackInfo = pCallbackInfo;
        this->minTimeBetweenReportsMs = minTimeBetweenReportsMs;
    }

    // Get device data change callback
    RaftDeviceDataChangeCB getDataChangeCB() const
    {
        return dataChangeCB;
    }

    // Get device data change callback info
    const void* getCallbackInfo() const
    {
        return pCallbackInfo;
    }

    // Get JSON for device status
    String getJson() const;
};
