/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// I2C Address Status
//
// Rob Dobson 2024
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "BusI2CAddrStatus.h"
#include "BusStatusMgr.h"

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Handle device responding information
/// @param isResponding true if device is responding
/// @param flagSpuriousRecord (out) true if this is a spurious record
/// @return true if status has changed
bool BusI2CAddrStatus::handleResponding(bool isResponding, bool &flagSpuriousRecord)
{
    // Handle is responding or not
    if (isResponding)
    {
        // If not already online then count upwards
        if (!isOnline)
        {
            // Check if we've reached the threshold for online
            count = (count < BusStatusMgr::I2C_ADDR_RESP_COUNT_OK_MAX) ? count + 1 : count;
            if (count >= BusStatusMgr::I2C_ADDR_RESP_COUNT_OK_MAX)
            {
                // Now online
                isChange = !isChange;
                count = 0;
                isOnline = true;
                wasOnceOnline = true;
                return true;
            }
        }
    }
    else
    {
        // Not responding - check for change to offline
        if (isOnline || !wasOnceOnline)
        {
            // Count down to offline/spurious threshold
            count = (count < -BusStatusMgr::I2C_ADDR_RESP_COUNT_FAIL_MAX) ? count : count - 1;
            if (count <= -BusStatusMgr::I2C_ADDR_RESP_COUNT_FAIL_MAX)
            {
                // Now offline/spurious
                count = 0;
                if (!wasOnceOnline)
                    flagSpuriousRecord = true;
                else
                    isChange = !isChange;
                isOnline = false;
                return true;
            }
        }
    }
    return false;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get JSON for device status
/// @return JSON string
String BusI2CAddrStatus::getJson() const
{
    // Create JSON
    String jsonStr = "{";
    jsonStr += "\"a\":\"0x" + String(addrAndSlot.addr,16) + "@" + String(addrAndSlot.slotNum) + "\"";
    jsonStr += ",\"s\":\"" + String(isOnline ? "O" : "X") + String(wasOnceOnline ? "W" : "X") + String(isNewlyIdentified ? "N" : "X") + "\"";
    jsonStr += "}";
    return jsonStr;
}
