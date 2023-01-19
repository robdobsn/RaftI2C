/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// RaftI2CCentral
// I2C implemented using direct ESP32 register access
//
// Rob Dobson 2021
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "RaftI2CCentralIF.h"
#include <Utils.h>
#include <soc/i2c_struct.h>
#include <soc/i2c_reg.h>
#include "esp_intr_alloc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// #define DEBUG_RAFT_I2C_CENTRAL_ISR
// #define DEBUG_RAFT_I2C_CENTRAL_ISR_ALL_SOURCES
// #define DEBUG_RAFT_I2C_CENTRAL_ISR_ON_FAIL

class RaftI2CCentral : public RaftI2CCentralIF
{
public:
    RaftI2CCentral();
    virtual ~RaftI2CCentral();

    // Init/de-init
    virtual bool init(uint8_t i2cPort, uint16_t pinSDA, uint16_t pinSCL, uint32_t busFrequency, 
                uint32_t busFilteringLevel = DEFAULT_BUS_FILTER_LEVEL) override final;

    // Busy
    virtual bool isBusy() override final;

    // Access the bus
    virtual AccessResultCode access(uint16_t address, uint8_t* pWriteBuf, uint32_t numToWrite,
                    uint8_t* pReadBuf, uint32_t numToRead, uint32_t& numRead) override final;

    // Check if bus operating ok
    virtual bool isOperatingOk() override final;
     
private:
    // Settings
    uint8_t _i2cPort = 0;
    int16_t _pinSDA = -1;
    int16_t _pinSCL = -1;
    uint32_t _busFrequency = 100000;
    uint32_t _busFilteringLevel = DEFAULT_BUS_FILTER_LEVEL;

    // Init flag
    bool _isInitialised = false;

    // Address bytes to add to FIFO when required
    uint8_t _startAddrPlusRW = 0;
    bool _startAddrPlusRWRequired = false;
    uint8_t _restartAddrPlusRW = 0;
    bool _restartAddrPlusRWRequired = false;

    // Read buffer
    volatile uint32_t _readBufPos = 0;
    volatile uint8_t* _readBufStartPtr = nullptr;
    volatile uint32_t _readBufMaxLen = 0;

    // Write buffer
    volatile uint32_t _writeBufPos = 0;
    volatile const uint8_t* _writeBufStartPtr = nullptr;
    volatile uint32_t _writeBufLen = 0;

    // Access result code
    volatile AccessResultCode _accessResultCode = ACCESS_RESULT_PENDING;

    // Interrupt handle, clear and enable flags
    intr_handle_t _i2cISRHandle = nullptr;
    uint32_t _interruptClearFlags = 0;
    uint32_t _interruptEnFlags = 0;

    // FIFO size
    static const uint32_t I2C_ENGINE_FIFO_SIZE = sizeof(I2C0.ram_data)/sizeof(I2C0.ram_data[0]);

    // Command max send/receive size
    static const uint32_t I2C_ENGINE_CMD_MAX_TX_BYTES = 255;
    static const uint32_t I2C_ENGINE_CMD_MAX_RX_BYTES = 255;

    // Command queue siz
    static const uint32_t I2C_ENGINE_CMD_QUEUE_SIZE = sizeof(I2C0.command)/sizeof(I2C0.command[0]);

    // Interrupts flags
    static const uint32_t INTERRUPT_BASE_ENABLES = 
                    I2C_ACK_ERR_INT_ENA | 
                    I2C_TIME_OUT_INT_ENA | 
                    I2C_TRANS_COMPLETE_INT_ENA |
                    I2C_ARBITRATION_LOST_INT_ENA | 
                    I2C_TXFIFO_EMPTY_INT_ENA | 
                    I2C_RXFIFO_FULL_INT_ENA 
#ifdef DEBUG_RAFT_I2C_CENTRAL_ISR_ALL_SOURCES
                    | I2C_TRANS_START_INT_ENA 
                    | I2C_END_DETECT_INT_ENA |
                    I2C_MASTER_TRAN_COMP_INT_ENA | 
                    I2C_RXFIFO_OVF_INT_ENA
#endif
                    ;

    // Interrupt and FIFO consts
    static const uint32_t INTERRUPT_BASE_CLEARS = INTERRUPT_BASE_ENABLES;
    static const uint32_t TX_FIFO_NEARING_EMPTY_INT = I2C_TXFIFO_EMPTY_INT_ENA;
    static const uint32_t RX_FIFO_NEARING_FULL_INT = I2C_RXFIFO_FULL_INT_ENA;

    // State
    static const uint32_t SCL_STATUS_START = 1;
    static const uint32_t MAIN_STATUS_SEND_ACK = 5;
    static const uint32_t MAIN_STATUS_WAIT_ACK = 6;

    // Access type
    enum I2CAccessType
    {
        ACCESS_POLL,
        ACCESS_READ_ONLY,
        ACCESS_WRITE_ONLY,
        ACCESS_WRITE_RESTART_READ,        
    };

    // Critical section mutex for I2C access
    portMUX_TYPE _i2cAccessMutex;

    // Helpers
    bool ensureI2CReady();
    void prepareI2CAccess();
    void deinit();
    void reinitI2CModule();
    bool setBusFrequency(uint32_t busFreq);
    uint32_t getApbFrequency();
    void setI2CCommand(uint32_t cmdIdx, uint8_t op_code, uint8_t byte_num, bool ack_val, bool ack_exp, bool ack_en);
    bool initInterrupts();
    void initBusFiltering();
    bool checkI2CLinesOk();
    static void IRAM_ATTR i2cISRStatic(void* arg);
    void IRAM_ATTR i2cISR();
    uint32_t IRAM_ATTR fillTxFifo();
    uint32_t IRAM_ATTR emptyRxFifo();

    // Debugging
    static String debugMainStatusStr(const char* prefix, uint32_t statusFlags);
    static String debugFIFOStatusStr(const char* prefix, uint32_t statusFlags);
    static String debugINTFlagStr(const char* prefix, uint32_t statusFlags);
    void debugShowStatus(const char* prefix, uint32_t addr);

    // Debug I2C ISR
#if defined(DEBUG_RAFT_I2C_CENTRAL_ISR) || defined(DEBUG_RAFT_I2C_CENTRAL_ISR_ON_FAIL)
    class DebugI2CISRElem
    {
    public:
        DebugI2CISRElem()
        {
            clear();
        }
        void IRAM_ATTR clear()
        {
            _micros = 0;
            _msg[0] = 0;
            _intStatus = 0;
        }
        void IRAM_ATTR set(const char* msg, uint32_t intStatus, 
                        uint32_t mainStatus, uint32_t txFifoStatus)
        {
            _micros = micros();
            _intStatus = intStatus;
            _mainStatus = mainStatus;
            _txFifoStatus = txFifoStatus;
            strlcpy(_msg, msg, sizeof(_msg));
        }
        String toStr()
        {
            char outStr[300];
            snprintf(outStr, sizeof(outStr), "%lld %s INT (%08x) %s MAIN (%08x) %s FIFO (%08x) %s", 
                        _micros, _msg,
                        _intStatus, debugINTFlagStr("", _intStatus).c_str(),
                        _mainStatus, debugMainStatusStr("", _mainStatus).c_str(), 
                        _txFifoStatus, debugFIFOStatusStr("", _txFifoStatus).c_str()
            );
            return outStr;
        }
        uint64_t _micros;
        char _msg[20];
        uint32_t _intStatus;
        uint32_t _mainStatus;
        uint32_t _txFifoStatus;
    };
    class DebugI2CISR
    {
    public:
        DebugI2CISR()
        {
            clear();
        }
        void IRAM_ATTR clear()
        {
            _i2cISRDebugCount = 0;
        }
        static const uint32_t DEBUG_RAFT_I2C_CENTRAL_ISR_MAX = 100;
        DebugI2CISRElem _i2cISRDebugElems[DEBUG_RAFT_I2C_CENTRAL_ISR_MAX];
        uint32_t _i2cISRDebugCount;
        void IRAM_ATTR debugISRAdd(const char* msg, uint32_t intStatus, 
                        uint32_t mainStatus, uint32_t txFifoStatus)
        {
            if (_i2cISRDebugCount >= DEBUG_RAFT_I2C_CENTRAL_ISR_MAX)
                return;
            _i2cISRDebugElems[_i2cISRDebugCount].set(msg, intStatus, mainStatus, txFifoStatus);
            _i2cISRDebugCount++;
        }
        uint32_t getCount()
        {
            return _i2cISRDebugCount;
        }
        DebugI2CISRElem& getElem(uint32_t i)
        {
            return _i2cISRDebugElems[i];
        }
    };
    DebugI2CISR _debugI2CISR;
#endif
};
