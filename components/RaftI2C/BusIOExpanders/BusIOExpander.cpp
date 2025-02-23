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

// #define DEBUG_SET_VIRTUAL_PIN_LEVEL
// #define DEBUG_IO_EXPANDER_SYNC_COMMS
// #define DEBUG_IO_EXPANDER_ASYNC_COMMS
// #define DEBUG_IO_EXPANDER_RESET

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Set virtual pin levels on IO expander (pins must be on the same expander or on GPIO)
/// @param numPins - number of pins to set
/// @param pPinNums - array of pin numbers
/// @param pLevels - array of levels (0 for low)
/// @param pResultCallback - callback for result when complete/failed
/// @param pCallbackData - callback data
/// @return RAFT_OK if successful
RaftRetCode BusIOExpander::virtualPinsSet(uint32_t numPins, const int* pPinNums, const uint8_t* pLevels, 
                VirtualPinSetCallbackType pResultCallback, void* pCallbackData)
{
    // Check valid
    if (numPins == 0 || !pPinNums || !pLevels)
        return RAFT_INVALID_DATA;

    // Check if first pin is within range
    if ((pPinNums[0] < _virtualPinBase) || (pPinNums[0] >= _virtualPinBase + _numVirtualPins))
    {
#ifdef DEBUG_SET_VIRTUAL_PIN_LEVEL
        LOG_W(MODULE_PREFIX, "virtualPinsSet firstPin out of range %d vPinBase %d numVPins %d", 
                    pPinNums[0], _virtualPinBase, _numVirtualPins);
#endif
        return RAFT_INVALID_DATA;
    }

    // Lock access to registers
    if (RaftMutex_lock(_regMutex, 10))
    {
        for (uint32_t idx = 0; idx < numPins; idx++)
        {
            // Check in range
            if ((pPinNums[idx] < _virtualPinBase) || (pPinNums[idx] >= _virtualPinBase + _numVirtualPins))
                continue;

            // Get pinIdx and mask
            int pinIdx = pPinNums[idx] - _virtualPinBase;
            uint32_t pinMask = 1 << pinIdx;

            // Check for config reg change
            if ((_configReg & pinMask) != 0)
            {
                _configReg &= ~pinMask;
                _configRegDirty = true;
            }

            // Check for output reg change
            if (((_outputsReg & pinMask) != 0) != pLevels[idx])
            {
                if (pLevels[idx])
                    _outputsReg |= pinMask;
                else
                    _outputsReg &= ~pinMask;
                _outputsRegDirty = true;
            }
        }

        // Add callback to list if unique
        if (pResultCallback)
        {
            bool callbackFound = false;
            for (auto& callbackInfo : _virtualPinSetCallbacks)
            {
                if ((callbackInfo.pResultCallback == pResultCallback) && 
                    (callbackInfo.pCallbackData == pCallbackData))
                {
                    callbackFound = true;
                    break;
                }
            }
             
            // Add if not found
            if (!callbackFound)
                _virtualPinSetCallbacks.push_back({pResultCallback, pCallbackData});
        }

        // Unlock access to registers
        RaftMutex_unlock(_regMutex);
    }

#ifdef DEBUG_SET_VIRTUAL_PIN_LEVEL
    LOG_W(MODULE_PREFIX, "virtualPinSet numPins %d config 0x%04x data 0x%04x", 
        numPins, _configReg, _outputsReg);
#endif
    
    return RAFT_OK;
}

/// @brief Get virtual pin level on IO expander
/// @param pinNum - pin number
/// @param busI2CReqAsyncFn - function to call to perform I2C request
/// @param vPinCallback - callback for virtual pin changes
/// @param pCallbackData - callback data
/// @return RaftRetCode - RAFT_OK if successful
RaftRetCode BusIOExpander::virtualPinRead(int pinNum, BusReqAsyncFn busI2CReqAsyncFn, 
                VirtualPinReadCallbackType vPinCallback, void *pCallbackData)
{
    // Check if pin valid
    if (pinNum < 0)
        return RAFT_INVALID_DATA;

    // Check if pin is within range
    int pinIdx = pinNum - _virtualPinBase;
    if ((pinIdx < 0) || (pinIdx >= _numVirtualPins))
        return RAFT_INVALID_DATA;

    // Create the bus request to get the input register
    uint8_t regNumberBuf[1] = {PCA9535_INPUT_PORT_0};
    BusRequestInfo reqRec(BUS_REQ_TYPE_STD,
        _addr,
        0, 
        sizeof(regNumberBuf),
        regNumberBuf,
        2,
        0,
        [vPinCallback, pCallbackData, pinNum, pinIdx](void* pBusReqCBData, BusRequestResult& busRequestResult) {
            if (busRequestResult.getReadDataVec().size() != 2)
            {
                if (vPinCallback)
                    vPinCallback(pCallbackData, VirtualPinResult(pinNum, false, RAFT_BUS_INCOMPLETE));
                return RAFT_BUS_INCOMPLETE;
            }
            uint32_t readInputReg = busRequestResult.getReadDataVec()[0] | (busRequestResult.getReadDataVec()[1] << 8);
            bool pinLevel = (readInputReg & (1 << pinIdx)) != 0;
            vPinCallback(pCallbackData, VirtualPinResult(pinNum, pinLevel, busRequestResult.getResult()));
            return RAFT_OK;
        },
        this);

    // Send the request
    busI2CReqAsyncFn(&reqRec, 0);

    // Return OK
    return RAFT_OK;
}

/// @brief Update power control registers for all slots
/// @param force true to force update (even if not dirty)
/// @param busI2CReqSyncFn function to call to perform I2C request
void BusIOExpander::updateSync(bool force, BusReqSyncFn busI2CReqSyncFn)
{
    // Check if io expander is dirty
    if (!(force || _configRegDirty || _outputsRegDirty))
        return;

    // Get the values to write and any callback info
    if (!RaftMutex_lock(_regMutex, 10))
        return;
    uint32_t outputsRegLocal = _outputsReg;
    uint32_t configRegLocal = _configReg;
    std::vector<VirtualPinSetCallbackInfo> virtualPinSetCallbacksLocal = _virtualPinSetCallbacks;

    // Clear the flags and callbacks
    _configRegDirty = false;
    _outputsRegDirty = false;
    _virtualPinSetCallbacks.clear();
    RaftMutex_unlock(_regMutex);

    // Setup multiplexer (if connected via a multiplexer)
    bool rsltOk = false;
    if (_muxAddr != 0)
    {
        // Check reset pin is output and set to inactive
        if (_muxResetPin >= 0)
        {
            pinMode(_muxResetPin, OUTPUT);
            digitalWrite(_muxResetPin, HIGH);
#ifdef DEBUG_IO_EXPANDER_RESET
            LOG_I(MODULE_PREFIX, "update muxResetPin %d set to HIGH", _muxResetPin);
#endif
        }

        // Set the mux channel
        uint8_t muxWriteData[1] = {(uint8_t)(1 << _muxChanIdx)};
        BusRequestInfo reqRec(BUS_REQ_TYPE_FAST_SCAN,
                              _muxAddr,
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
                                 uint8_t(outputsRegLocal & 0xff),
                                 uint8_t(outputsRegLocal >> 8)};
    BusRequestInfo reqRec(BUS_REQ_TYPE_FAST_SCAN,
                          _addr,
                          0, sizeof(outputPortData),
                          outputPortData,
                          0,
                          0,
                          nullptr,
                          this);
    rsltOk = busI2CReqSyncFn(&reqRec, nullptr) == RAFT_OK;

    // Write the configuration register
    uint8_t configPortData[3] = {PCA9535_CONFIG_PORT_0,
                                 uint8_t(configRegLocal & 0xff),
                                 uint8_t(configRegLocal >> 8)};
    BusRequestInfo reqRec2(BUS_REQ_TYPE_FAST_SCAN,
                           _addr,
                           0, sizeof(configPortData),
                           configPortData,
                           0,
                           0,
                           nullptr,
                           this);
    rsltOk &= busI2CReqSyncFn(&reqRec2, nullptr) == RAFT_OK;

    // Clear multiplexer
    if (_muxAddr != 0)
    {
        // Clear the mux channel
        uint8_t muxWriteData[1] = {0};
        BusRequestInfo reqRec(BUS_REQ_TYPE_FAST_SCAN,
                              _muxAddr,
                              0, sizeof(muxWriteData),
                              muxWriteData,
                              0,
                              0,
                              nullptr,
                              this);
        busI2CReqSyncFn(&reqRec, nullptr);
    }

#ifdef DEBUG_IO_EXPANDER_SYNC_COMMS
    LOG_I(MODULE_PREFIX, "update sync _addr 0x%02x outputReg 0x%04x configReg 0x%04x force %d rslt %s",
          _addr, outputsRegLocal, configRegLocal, force, rsltOk ? "OK" : "FAIL");
#endif

    // Perform any required callbacks
    for (auto& callbackInfo : virtualPinSetCallbacksLocal)
    {
        if (callbackInfo.pResultCallback)
            callbackInfo.pResultCallback(callbackInfo.pCallbackData, rsltOk ? RAFT_OK : RAFT_OTHER_FAILURE);
    }
}
