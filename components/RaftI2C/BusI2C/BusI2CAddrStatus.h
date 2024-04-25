/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// I2C Address Status
//
// Rob Dobson 2024
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "BusI2CConsts.h"
#include "DeviceStatus.h"

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

    // Access barring
    uint32_t barStartMs = 0;
    uint16_t barDurationMs = 0;

    // Device status
    DeviceStatus deviceStatus;

    // Handle responding
    bool handleResponding(bool isResponding, bool &flagSpuriousRecord);
    
    // Get JSON for device status
    String getJson() const;
};
