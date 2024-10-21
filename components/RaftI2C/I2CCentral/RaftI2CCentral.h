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
#include "RaftUtils.h"
#include "soc/i2c_struct.h"
#include "soc/i2c_reg.h"
#include "esp_intr_alloc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"
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
    virtual void deinit() override final;

    // Busy
    virtual bool isBusy() override final;

    // Access the bus
    virtual RaftRetCode access(uint32_t address, const uint8_t* pWriteBuf, uint32_t numToWrite,
                    uint8_t* pReadBuf, uint32_t numToRead, uint32_t& numRead) override final;

    // Check if bus operating ok
    virtual bool isOperatingOk() const override final;
     
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
    volatile bool _accessNackDetected = false;
    volatile RaftRetCode _accessResultCode = RAFT_BUS_PENDING;

    // Interrupt handle, clear and enable flags
    intr_handle_t _i2cISRHandle = nullptr;
    uint32_t _interruptClearFlags = 0;
    uint32_t _interruptEnFlags = 0;

    // FIFO size
    static const uint32_t I2C_ENGINE_FIFO_SIZE = 32;

    // Command max send/receive size
    static const uint32_t I2C_ENGINE_CMD_MAX_TX_BYTES = 255;
    static const uint32_t I2C_ENGINE_CMD_MAX_RX_BYTES = 255;

    // Last check of I2C ready
    static const uint32_t I2C_READY_CHECK_INTERVAL_FIRST_MS = 10;
    static const uint32_t I2C_READY_CHECK_INTERVAL_OTHER_MS = 5000;
    uint32_t _lastCheckI2CReadyMs = 0;
    uint32_t _lastCheckI2CReadyIntervalMs = I2C_READY_CHECK_INTERVAL_FIRST_MS;

    // Alternate definitions for chip variants
#if defined(CONFIG_IDF_TARGET_ESP32S3)
    // ESP32 S3
    static const uint32_t I2C_ENGINE_CMD_QUEUE_SIZE = 8;
    #define I2C_ACK_ERR_INT_ENA I2C_NACK_INT_ENA
    #define I2C_ACK_ERR_INT_ST I2C_NACK_INT_ST
    #define I2C_TXFIFO_EMPTY_INT_ENA I2C_TXFIFO_WM_INT_ENA
    #define I2C_TXFIFO_EMPTY_INT_ST I2C_TXFIFO_WM_INT_ST
    #define I2C_RXFIFO_FULL_INT_ENA I2C_RXFIFO_WM_INT_ENA
    #define I2C_RXFIFO_FULL_INT_ST I2C_RXFIFO_WM_INT_ST
    #define I2C_MASTER_TRAN_COMP_INT_ST I2C_MST_TXFIFO_UDF_INT_ST
    #define I2C_STATUS_REGISTER_NAME sr
    #define I2C_COMMAND_0_REGISTER_NAME comd[0]
    #define I2C_TX_FIFO_RAM_ADDR_0_NAME txfifo_start_addr
    #define I2C_DATA_FIFO_DATA_REG data
    #define I2C_DATA_FIFO_READ_DATA_REG fifo_rdata
    #define I2C_TIMEOUT_REG_NAME to
    #define I2C_STATUS_REGISTER_TX_FIFO_CNT_NAME txfifo_cnt
    #define I2C_STATUS_REGISTER_RX_FIFO_CNT_NAME rxfifo_cnt
    #define I2C_SCL_LOW_PERIOD_PERIOD_NAME scl_low_period
    #define I2C_SCL_HIGH_PERIOD_PERIOD_NAME scl_high_period
    #define I2C_SDA_HOLD_TIME_NAME sda_hold_time
    #define I2C_SDA_SAMPLE_TIME_NAME sda_sample_time
    #define I2C_SCL_RSTART_SETUP_TIME_NAME scl_rstart_setup_time
    #define I2C_SCL_STOP_SETUP_TIME_NAME scl_stop_setup_time
    #define I2C_SCL_START_HOLD_TIME_NAME scl_start_hold_time
    #define I2C_SCL_STOP_HOLD_TIME_NAME scl_stop_hold_time
    #define I2C_FILTER_CFG_SCL_THRESH filter_cfg.scl_filter_thres
    #define I2C_FILTER_CFG_SDA_THRESH filter_cfg.sda_filter_thres
    #define I2C_FILTER_CFG_SCL_ENABLE filter_cfg.scl_filter_en
    #define I2C_FILTER_CFG_SDA_ENABLE filter_cfg.sda_filter_en
#elif defined(CONFIG_IDF_TARGET_ESP32C3)
    // ESP32 C3
    static const uint32_t I2C_ENGINE_CMD_QUEUE_SIZE = 8;
    #define I2C_ACK_ERR_INT_ENA I2C_NACK_INT_ENA
    #define I2C_ACK_ERR_INT_ST I2C_NACK_INT_ST
    #define I2C_TXFIFO_EMPTY_INT_ENA I2C_TXFIFO_WM_INT_ENA
    #define I2C_TXFIFO_EMPTY_INT_ST I2C_TXFIFO_WM_INT_ST
    #define I2C_RXFIFO_FULL_INT_ENA I2C_RXFIFO_WM_INT_ENA
    #define I2C_RXFIFO_FULL_INT_ST I2C_RXFIFO_WM_INT_ST
    #define I2C_MASTER_TRAN_COMP_INT_ST I2C_MST_TXFIFO_UDF_INT_ST
    #define I2C_STATUS_REGISTER_NAME sr
    #define I2C_COMMAND_0_REGISTER_NAME command[0]
    #define I2C_TX_FIFO_RAM_ADDR_0_NAME txfifo_start_addr
    #define I2C_DATA_FIFO_DATA_REG fifo_data
    #define I2C_DATA_FIFO_READ_DATA_REG data
    #define I2C_TIMEOUT_REG_NAME timeout
    #define I2C_STATUS_REGISTER_TX_FIFO_CNT_NAME tx_fifo_cnt
    #define I2C_STATUS_REGISTER_RX_FIFO_CNT_NAME rx_fifo_cnt
    #define I2C_SCL_LOW_PERIOD_PERIOD_NAME period
    #define I2C_SCL_HIGH_PERIOD_PERIOD_NAME period
    #define I2C_SDA_HOLD_TIME_NAME time
    #define I2C_SDA_SAMPLE_TIME_NAME time
    #define I2C_SCL_RSTART_SETUP_TIME_NAME time
    #define I2C_SCL_STOP_SETUP_TIME_NAME time
    #define I2C_SCL_START_HOLD_TIME_NAME time
    #define I2C_SCL_STOP_HOLD_TIME_NAME time
    #define I2C_FILTER_CFG_SCL_THRESH filter_cfg.scl_thres
    #define I2C_FILTER_CFG_SDA_THRESH filter_cfg.sda_thres
    #define I2C_FILTER_CFG_SCL_ENABLE filter_cfg.scl_en
    #define I2C_FILTER_CFG_SDA_ENABLE filter_cfg.sda_en
#else
    // ESP32
    static const uint32_t I2C_ENGINE_CMD_QUEUE_SIZE = 16;
    #define I2C_STATUS_REGISTER_NAME status_reg
    #define I2C_COMMAND_0_REGISTER_NAME command[0]
    #define I2C_TX_FIFO_RAM_ADDR_0_NAME ram_data
    #define I2C_TIMEOUT_REG_NAME timeout
    #define I2C_FILTER_CFG_SCL_THRESH scl_filter_cfg.thres
    #define I2C_FILTER_CFG_SDA_THRESH sda_filter_cfg.thres
    #define I2C_FILTER_CFG_SCL_ENABLE scl_filter_cfg.en
    #define I2C_FILTER_CFG_SDA_ENABLE sda_filter_cfg.en  
#endif

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

    // Command register type (same for ESP32 and ESP32S3)
    typedef union {
        struct {
            uint32_t byte_num:    8,
                    ack_en:      1,
                    ack_exp:     1,
                    ack_val:     1,
                    op_code:     3,
                    reserved14: 17,
                    done:        1;
        };
        uint32_t val;
    } I2C_COMMAND_REG_TYPE;

    // Access type
    enum I2CAccessType
    {
        ACCESS_POLL,
        ACCESS_READ_ONLY,
        ACCESS_WRITE_ONLY,
        ACCESS_WRITE_RESTART_READ,        
    };

    // Critical section mutex for I2C access
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(4, 3, 0)
    spinlock_t _i2cAccessMutex;
#else
    portMUX_TYPE _i2cAccessMutex;
#endif

    // Helpers
    bool ensureI2CReady();
    void prepareI2CAccess();
    void reinitI2CModule();
    bool setBusFrequency(uint32_t busFreq);
    uint32_t getApbFrequency();
    void setI2CCommand(uint32_t cmdIdx, uint8_t op_code, uint8_t byte_num, bool ack_val, bool ack_exp, bool ack_en);
    bool initInterrupts();
    void initBusFiltering();
    bool checkI2CLinesOk(String& busLinesErrorMsg);
    static void IRAM_ATTR i2cISRStatic(void* arg);
    void IRAM_ATTR i2cISR();
    uint32_t IRAM_ATTR fillTxFifo();
    uint32_t IRAM_ATTR emptyRxFifo();
    void setDefaultTimeout();

    // Debugging
    static String debugMainStatusStr(const char* prefix, uint32_t statusFlags);
    static String debugFIFOStatusStr(const char* prefix, uint32_t statusFlags);
    static String debugINTFlagStr(const char* prefix, uint32_t statusFlags);
    static void debugShowAllRegs(const char *prefix, i2c_dev_t* pDev);
    void debugShowStatus(const char* prefix, uint32_t addr);

    // Debug I2C ISR
#if defined(DEBUG_RAFT_I2C_CENTRAL_ISR) || defined(DEBUG_RAFT_I2C_CENTRAL_ISR_ON_FAIL)
    class DebugI2CISRElem
    {
    public:
        DebugI2CISRElem();
        void IRAM_ATTR clear();
        void IRAM_ATTR set(const char* msg, uint32_t intStatus, 
                        uint32_t mainStatus, uint32_t txFifoStatus);
        String toStr();
        uint64_t _micros;
        char _msg[20];
        uint32_t _intStatus;
        uint32_t _mainStatus;
        uint32_t _txFifoStatus;
    };
    class DebugI2CISR
    {
    public:
        DebugI2CISR();
        void IRAM_ATTR clear();
        static const uint32_t DEBUG_RAFT_I2C_CENTRAL_ISR_MAX = 100;
        DebugI2CISRElem _i2cISRDebugElems[DEBUG_RAFT_I2C_CENTRAL_ISR_MAX];
        uint32_t _i2cISRDebugCount;
        void IRAM_ATTR debugISRAdd(const char* msg, uint32_t intStatus, 
                        uint32_t mainStatus, uint32_t txFifoStatus);
        uint32_t getCount();
        DebugI2CISRElem& getElem(uint32_t i);
    };
    DebugI2CISR _debugI2CISR;
#endif
};
