/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// I2C Bus IO Expander
//
// Rob Dobson 2025
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "BusIOExpander.h"
#include "BusRequestInfo.h"
#include "BusRequestResult.h"

#define DEBUG_IO_EXPANDER_SYNC_COMMS
#define DEBUG_IO_EXPANDER_ASYNC_COMMS
#define DEBUG_IO_EXPANDER_RESET

/// @brief Set virtual pin mode on IO expander
/// @param pinNum - pin number
/// @param mode - true for input, false for output
/// @param level - true for high, false for low
void BusIOExpander::virtualPinMode(int pinNum, bool level)
{
    // Check if pin valid
    if (pinNum < 0)
        return;

    // Check if pin is within range
    if (pinNum < virtualPinBase || pinNum >= virtualPinBase + numPins)
        return;

    // Set the pin mode
    if (level)
        configReg &= ~(1 << (pinNum - virtualPinBase));
    else
        configReg |= (1 << (pinNum - virtualPinBase));

    // Set the level
    if (level)
        outputsReg |= (1 << (pinNum - virtualPinBase));
    else
        outputsReg &= ~(1 << (pinNum - virtualPinBase));

    // Set dirty flag
    configRegDirty = true;
    outputsRegDirty = true;
}

/// @brief Set virtual pin level on IO expander
/// @param pinNum - pin number
/// @param level - true for on, false for off
void BusIOExpander::virtualPinWrite(int pinNum, bool level)
{
    // Check if pin valid
    if (pinNum < 0)
        return;

    // Check if pin is within range
    if (pinNum < virtualPinBase || pinNum >= virtualPinBase + numPins)
        return;

    // Set the level
    if (level)
        outputsReg |= (1 << (pinNum - virtualPinBase));
    else
        outputsReg &= ~(1 << (pinNum - virtualPinBase));

    // Set dirty flag
    outputsRegDirty = true;
}

/// @brief Get virtual pin level on IO expander
/// @param pinNum - pin number
/// @param busI2CReqAsyncFn - function to call to perform I2C request
/// @param vPinCallback - callback for virtual pin changes
/// @param pCallbackData - callback data
void BusIOExpander::virtualPinRead(int pinNum, BusReqAsyncFn busI2CReqAsyncFn, VirtualPinCallbackType vPinCallback, void *pCallbackData)
{
    // Check if pin valid
    if (pinNum < 0)
        return;

    // Check if pin is within range
    if (pinNum < virtualPinBase || pinNum >= virtualPinBase + numPins)
    {
        if (vPinCallback)
            vPinCallback(pCallbackData, VirtualPinResult(-1, false, RAFT_INVALID_DATA));
        return;
    }

    // Create the bus request to get the input register
    uint8_t regNumberBuf[1] = {PCA9535_INPUT_PORT_0};
    uint32_t relativePin = pinNum - virtualPinBase;
    BusRequestInfo reqRec(BUS_REQ_TYPE_STD,
        addr,
        0, 
        sizeof(regNumberBuf),
        regNumberBuf,
        2,
        0,
        [vPinCallback, pCallbackData, pinNum, relativePin](void* pBusReqCBData, BusRequestResult& busRequestResult) {
            if (busRequestResult.getReadDataVec().size() != 2)
            {
                if (vPinCallback)
                    vPinCallback(pCallbackData, VirtualPinResult(pinNum, false, RAFT_INVALID_DATA));
                return;
            }
            uint32_t readInputReg = busRequestResult.getReadDataVec()[0] | (busRequestResult.getReadDataVec()[1] << 8);
            bool pinLevel = (readInputReg & (1 << relativePin)) != 0;
            vPinCallback(pCallbackData, VirtualPinResult(pinNum, pinLevel, busRequestResult.getResult()));
        },
        this);

    // Send the request
    busI2CReqAsyncFn(&reqRec, 0);
}

/// @brief Update power control registers for all slots
/// @param force true to force update (even if not dirty)
/// @param busI2CReqSyncFn function to call to perform I2C request
void BusIOExpander::updateSync(bool force, BusReqSyncFn busI2CReqSyncFn)
{
    // Check if io expander is dirty
    if (!(force || configRegDirty || outputsRegDirty))
        return;

    // Setup multiplexer (if connected via a multiplexer)
    bool rsltOk = false;
    if (muxAddr != 0)
    {
        // Check reset pin is output and set to inactive
        if (muxResetPin >= 0)
        {
            pinMode(muxResetPin, OUTPUT);
            digitalWrite(muxResetPin, HIGH);
#ifdef DEBUG_IO_EXPANDER_RESET
            LOG_I(MODULE_PREFIX, "update muxResetPin %d set to HIGH", muxResetPin);
#endif
        }

        // Set the mux channel
        uint8_t muxWriteData[1] = {(uint8_t)(1 << muxChanIdx)};
        BusRequestInfo reqRec(BUS_REQ_TYPE_FAST_SCAN,
                              muxAddr,
                              0, sizeof(muxWriteData),
                              muxWriteData,
                              0,
                              0,
                              nullptr,
                              this);
        busI2CReqSyncFn(&reqRec, nullptr);
    }

    // Set the output register first (to avoid unexpected changes)
    uint8_t outputPortData[3] = {PCA9535_OUTPUT_PORT_0,
                                 uint8_t(outputsReg & 0xff),
                                 uint8_t(outputsReg >> 8)};
    BusRequestInfo reqRec(BUS_REQ_TYPE_FAST_SCAN,
                          addr,
                          0, sizeof(outputPortData),
                          outputPortData,
                          0,
                          0,
                          nullptr,
                          this);
    rsltOk = busI2CReqSyncFn(&reqRec, nullptr) == RAFT_OK;

    // Write the configuration register
    uint8_t configPortData[3] = {PCA9535_CONFIG_PORT_0,
                                 uint8_t(configReg & 0xff),
                                 uint8_t(configReg >> 8)};
    BusRequestInfo reqRec2(BUS_REQ_TYPE_FAST_SCAN,
                           addr,
                           0, sizeof(configPortData),
                           configPortData,
                           0,
                           0,
                           nullptr,
                           this);
    rsltOk &= busI2CReqSyncFn(&reqRec2, nullptr) == RAFT_OK;

    // Clear multiplexer
    if (muxAddr != 0)
    {
        // Clear the mux channel
        uint8_t muxWriteData[1] = {0};
        BusRequestInfo reqRec(BUS_REQ_TYPE_FAST_SCAN,
                              muxAddr,
                              0, sizeof(muxWriteData),
                              muxWriteData,
                              0,
                              0,
                              nullptr,
                              this);
        busI2CReqSyncFn(&reqRec, nullptr);
    }

    // Clear the dirty flag if result is ok
    configRegDirty = !rsltOk;
    outputsRegDirty = !rsltOk;

#ifdef DEBUG_IO_EXPANDER_SYNC_COMMS
    LOG_I(MODULE_PREFIX, "update addr 0x%02x outputReg 0x%04x configReg 0x%04x force %d rslt %s",
          addr, outputsReg, configReg, force, rsltOk ? "OK" : "FAIL");
#endif
}

/// @brief Update power control registers for all slots
/// @param force true to force update (even if not dirty)
/// @param busI2CReqSyncFn function to call to perform I2C request
/// @param vPinCallback callback for virtual pin changes
/// @param pCallbackData callback data
void BusIOExpander::updateAsync(bool force, BusReqAsyncFn busI2CReqAsyncFn, VirtualPinCallbackType vPinCallback, void *pCallbackData)
{
    // Check if io expander is dirty
    if (!(force || configRegDirty || outputsRegDirty))
    {
        if (vPinCallback)
            vPinCallback(pCallbackData, VirtualPinResult(-1, false, RAFT_OK));
        return;
    }

    // Check if output register dirty
    if (outputsRegDirty)
    {
        // Output register
        uint8_t outputPortData[3] = {PCA9535_OUTPUT_PORT_0,
                                     uint8_t(outputsReg & 0xff),
                                     uint8_t(outputsReg >> 8)};

        // Create the bus request for the output register
        if (configRegDirty)
        {
            BusRequestInfo reqRec(BUS_REQ_TYPE_STD,
                                addr,
                                0, 
                                sizeof(outputPortData),
                                outputPortData,
                                0,
                                0,
                                nullptr,
                                this);
            busI2CReqAsyncFn(&reqRec, 0);
        }
        else
        {
            BusRequestInfo reqRec(BUS_REQ_TYPE_STD,
                addr,
                0, 
                sizeof(outputPortData),
                outputPortData,
                0,
                0,
                [vPinCallback, pCallbackData](void* pBusReqCBData, BusRequestResult& busRequestResult) {
                        vPinCallback(pCallbackData, VirtualPinResult(-1, false, busRequestResult.getResult()));
                },
                this);
            busI2CReqAsyncFn(&reqRec, 0);
        }

        // Clear the dirty flag
        outputsRegDirty = false;
    }

    // Check if config register dirty
    if (configRegDirty)
    {
        // Create the bus request for the config register
        uint8_t configPortData[3] = {PCA9535_CONFIG_PORT_0,
                                     uint8_t(configReg & 0xff),
                                     uint8_t(configReg >> 8)};
        BusRequestInfo reqRec2(BUS_REQ_TYPE_STD,
                            addr,
                            0, sizeof(configPortData),
                            configPortData,
                            0,
                            0,
                            [vPinCallback, pCallbackData](void* pBusReqCBData, BusRequestResult& busRequestResult) {
                                vPinCallback(pCallbackData, VirtualPinResult(-1, false, busRequestResult.getResult()));
                            },
                           this);
        busI2CReqAsyncFn(&reqRec2, 0);

        // Clear the dirty flag
        configRegDirty = false;
    }
    
#ifdef DEBUG_IO_EXPANDER_ASYNC_COMMS
    LOG_I(MODULE_PREFIX, "update addr 0x%02x outputReg 0x%04x configReg 0x%04x force %d",
          addr, outputsReg, configReg, force);
#endif
}
