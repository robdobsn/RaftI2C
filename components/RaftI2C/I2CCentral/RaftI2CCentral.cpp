/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// RaftI2CCentral
// I2C Central implemented using direct ESP32 register access
//
// Rob Dobson 2021
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "RaftI2CCentral.h"
#include "Logger.h"
#include "RaftUtils.h"
#include "RaftArduino.h"
#include "sdkconfig.h"
#include "driver/gpio.h"
#if defined(CONFIG_IDF_TARGET_ESP32) || defined(CONFIG_IDF_TARGET_ESP32S3)
#include "soc/dport_reg.h"
#endif
#include "soc/rtc.h"
#include "soc/i2c_periph.h"
#include "hal/i2c_types.h"
#include "esp_rom_gpio.h"
#include "soc/io_mux_reg.h"
#include "hal/gpio_hal.h"
#include "esp_private/esp_clk.h"
#include "esp_private/periph_ctrl.h"
#include "esp_idf_version.h"

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Consts
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static const char *MODULE_PREFIX = "RaftI2CCentral";

#define WARN_ON_BUS_IS_BUSY
#define WARN_ON_BUS_CANNOT_BE_RESET
#define WARN_RICI2C_ACCESS_INCOMPLETE

// #define DEBUG_RICI2C_ACCESS
// #define DEBUG_TIMING
// #define DEBUG_TIMEOUT_CALCS
// #define DEBUG_I2C_COMMANDS
// #define DEBUG_ALL_REGS
// #define DEBUG_ISR_USING_GPIO_NUM 9
// #define DEBUG_BUS_NOT_READY_WITH_GPIO_NUM 7

// I2C operation mode command (different for ESP32 and ESP32S3/C3)
#if defined(CONFIG_IDF_TARGET_ESP32S3) || defined(CONFIG_IDF_TARGET_ESP32C3) || defined(CONFIG_IDF_TARGET_ESP32C6)
static const uint8_t ESP32_I2C_CMD_RSTART = 6;
static const uint8_t ESP32_I2C_CMD_WRITE = 1;
static const uint8_t ESP32_I2C_CMD_READ = 3;
static const uint8_t ESP32_I2C_CMD_STOP = 2;
static const uint8_t ESP32_I2C_CMD_END = 4;
#else
static const uint8_t ESP32_I2C_CMD_RSTART = 0;
static const uint8_t ESP32_I2C_CMD_WRITE = 1;
static const uint8_t ESP32_I2C_CMD_READ = 2;
static const uint8_t ESP32_I2C_CMD_STOP = 3;
static const uint8_t ESP32_I2C_CMD_END = 4;
#endif

#if defined(CONFIG_IDF_TARGET_ESP32C3) || defined(CONFIG_IDF_TARGET_ESP32C6)
#define I2C_DEVICE I2C0
#else
#define I2C_DEVICE (_i2cPort == 0 ? I2C0 : I2C1)
#endif

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Constructor / Destructor
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

RaftI2CCentral::RaftI2CCentral()
{
    // Mutex for critical section to access I2C FIFO
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(4, 3, 0)
    spinlock_initialize(&_i2cAccessMutex);
    // LOG_I(MODULE_PREFIX, "------------------------ spinlock initialized");
#else
    vPortCPUInitializeMutex(&_i2cAccessMutex);
#endif
}

RaftI2CCentral::~RaftI2CCentral()
{
    // De-init
    deinit();
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Initialisation function
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool RaftI2CCentral::init(uint8_t i2cPort, uint16_t pinSDA, uint16_t pinSCL, uint32_t busFrequency,
                          uint32_t busFilteringLevel)
{
    // de-init first in case already initialised
    deinit();

    // Settings
    _i2cPort = i2cPort;
    _pinSDA = pinSDA;
    _pinSCL = pinSCL;
    _busFrequency = busFrequency;
    _busFilteringLevel = busFilteringLevel;

    // Attach SDA and SCL
#if defined(CONFIG_IDF_TARGET_ESP32S3) || defined(CONFIG_IDF_TARGET_ESP32C3) || defined(CONFIG_IDF_TARGET_ESP32C6)
    int sda_out_sig = i2c_periph_signal[_i2cPort].sda_out_sig;
    int sda_in_sig = i2c_periph_signal[_i2cPort].sda_in_sig;
    int scl_out_sig = i2c_periph_signal[_i2cPort].scl_out_sig;
    int scl_in_sig = i2c_periph_signal[_i2cPort].scl_in_sig;
    LOG_I(MODULE_PREFIX, "init i2cPort %d sdaPin %d sclPin %d sda_out_sig %d sda_in_sig %d scl_out_sig %d scl_in_sig %d",
          _i2cPort, _pinSDA, _pinSCL, sda_out_sig, sda_in_sig, scl_out_sig, scl_in_sig);
    if (_pinSDA >= 0)
    {
        gpio_set_level((gpio_num_t)_pinSDA, 1);
        gpio_hal_iomux_func_sel(GPIO_PIN_MUX_REG[_pinSDA], PIN_FUNC_GPIO);
        gpio_set_direction((gpio_num_t)_pinSDA, GPIO_MODE_INPUT_OUTPUT_OD);
        gpio_set_pull_mode((gpio_num_t)_pinSDA, GPIO_PULLUP_ONLY);
        esp_rom_gpio_connect_out_signal((gpio_num_t)_pinSDA, sda_out_sig, false, false);
        esp_rom_gpio_connect_in_signal((gpio_num_t)_pinSDA, sda_in_sig, false);
    }
    if (_pinSCL >= 0)
    {
        gpio_set_level((gpio_num_t)_pinSCL, 1);
        gpio_hal_iomux_func_sel(GPIO_PIN_MUX_REG[_pinSCL], PIN_FUNC_GPIO);
        gpio_set_direction((gpio_num_t)_pinSCL, GPIO_MODE_INPUT_OUTPUT_OD);
        gpio_set_pull_mode((gpio_num_t)_pinSCL, GPIO_PULLUP_ONLY);
        esp_rom_gpio_connect_out_signal((gpio_num_t)_pinSCL, scl_out_sig, false, false);
        esp_rom_gpio_connect_in_signal((gpio_num_t)_pinSCL, scl_in_sig, false);
    }
#else
    if (_pinSDA >= 0)
    {
        uint32_t sdaIdx = _i2cPort == 0 ? I2CEXT0_SDA_OUT_IDX : I2CEXT1_SDA_OUT_IDX;
        gpio_set_level((gpio_num_t)_pinSDA, 1);
        gpio_set_direction((gpio_num_t)_pinSDA, GPIO_MODE_INPUT_OUTPUT_OD);
        gpio_pullup_en((gpio_num_t)_pinSDA);
#if ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(5, 0, 0)
        gpio_matrix_out((gpio_num_t)_pinSDA, sdaIdx, false, false);
        gpio_matrix_in((gpio_num_t)_pinSDA, sdaIdx, false);
#else
        esp_rom_gpio_connect_out_signal((gpio_num_t)_pinSDA, sdaIdx, false, false);
        esp_rom_gpio_connect_in_signal((gpio_num_t)_pinSDA, sdaIdx, false);
#endif
    }
    if (_pinSCL >= 0)
    {
        uint32_t sclIdx = _i2cPort == 0 ? I2CEXT0_SCL_OUT_IDX : I2CEXT1_SCL_OUT_IDX;
        gpio_set_level((gpio_num_t)_pinSCL, 1);
        gpio_set_direction((gpio_num_t)_pinSCL, GPIO_MODE_INPUT_OUTPUT_OD);
        gpio_pullup_en((gpio_num_t)_pinSCL);
#if ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(5, 0, 0)
        gpio_matrix_out((gpio_num_t)_pinSCL, sclIdx, false, false);
        gpio_matrix_in((gpio_num_t)_pinSCL, sclIdx, false);
#else
        esp_rom_gpio_connect_out_signal((gpio_num_t)_pinSCL, sclIdx, false, false);
        esp_rom_gpio_connect_in_signal((gpio_num_t)_pinSCL, sclIdx, false);
#endif
    }
#endif

#if defined(CONFIG_IDF_TARGET_ESP32S3)
    // Enable peripheral on ESP32 S3
    periph_module_enable((i2cPort == 0) ? PERIPH_I2C0_MODULE : PERIPH_I2C1_MODULE);

    // Enable clock to I2C peripheral
    I2C_DEVICE.clk_conf.sclk_active = 1;
#elif defined(CONFIG_IDF_TARGET_ESP32C3)
    periph_module_enable(PERIPH_I2C0_MODULE);
    periph_module_reset(PERIPH_I2C0_MODULE);
    // i2c_hal_init(I2C_DEVICE, 0);    // Enable clock to I2C peripheral
    I2C_DEVICE.clk_conf.sclk_active = 1;
    SET_PERI_REG_MASK(RTC_CNTL_CLK_CONF_REG, RTC_CNTL_DIG_CLK8M_EN_M);
#elif defined(CONFIG_IDF_TARGET_ESP32C6)
    periph_module_enable(PERIPH_I2C0_MODULE);
    I2C_DEVICE.clk_conf.sclk_active = 1;
#endif

    // Setup interrupts on the required port
    initInterrupts();

    // Use re-init sequence as we have to do this on a lockup too
    reinitI2CModule();

    // Enable bus filtering if required
    initBusFiltering();

#ifdef DEBUG_ISR_USING_GPIO_NUM
    // Set GPIO 1 to output for debugging
    gpio_set_direction((gpio_num_t)1, GPIO_MODE_OUTPUT);
    gpio_set_level((gpio_num_t)1, 0);
#endif

    // Now inititalized
    _isInitialised = true;
    return true;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Check if bus is busy
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool RaftI2CCentral::isBusy()
{
    // Check if hardware is ready
    if (!_isInitialised)
        return true;

    bool isBusy = I2C_DEVICE.I2C_STATUS_REGISTER_NAME.bus_busy;
#ifdef DEBUG_RICI2C_ACCESS
    LOG_I(MODULE_PREFIX, "isBusy %s", isBusy ? "BUSY" : "IDLE");
#endif
    return isBusy;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Check if bus operating ok
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool RaftI2CCentral::isOperatingOk() const
{
    return _isInitialised;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Access the I2C bus
// Note:
// - a zero length read and zero length write sends address with R/W flag indicating write to test if a node ACKs
// - a write of non-zero length alone does what it says and can be of arbitrary length
// - a read on non-zero length also can be of arbitrary length
// - a write of non-zero length and read of non-zero length is allowed - write occurs first and can only be of max 14 bytes
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

RaftRetCode RaftI2CCentral::access(uint32_t address, const uint8_t *pWriteBuf, uint32_t numToWrite,
                                                          uint8_t *pReadBuf, uint32_t numToRead, uint32_t &numRead)
{
    // Check valid
    if ((numToWrite > 0) && !pWriteBuf)
        return RAFT_BUS_INVALID;
    if ((numToRead > 0) && !pReadBuf)
        return RAFT_BUS_INVALID;

#if defined(DEBUG_RAFT_I2C_CENTRAL_ISR) || defined(DEBUG_RAFT_I2C_CENTRAL_ISR_ON_FAIL)
    _debugI2CISR.clear();
#endif

    // Ensure the engine is ready
    if (!ensureI2CReady())
        return RAFT_BUS_NOT_READY;

    // Check what operation is required
    I2CAccessType i2cOpType = ACCESS_POLL;
    if ((numToRead > 0) && (numToWrite > 0))
        i2cOpType = ACCESS_WRITE_RESTART_READ;
    else if (numToRead > 0)
        i2cOpType = ACCESS_READ_ONLY;
    else if (numToWrite > 0)
        i2cOpType = ACCESS_WRITE_ONLY;

    // The ESP32 I2C engine can accommodate up to 14 write/read commands
    // each of which can request up to 255 bytes of writing/reading
    // If writing and reading reading is involved then an RSTART command and second address is needed
    // as well as a final command reading one byte which is NACKed
    // Check max lengths to write/read (including the RSTART command if needed)
    // don't result in exceeding these 14 commands
    uint32_t writeCommands = 0;
    writeCommands = (numToWrite + 1 + I2C_ENGINE_CMD_MAX_TX_BYTES - 1) / I2C_ENGINE_CMD_MAX_TX_BYTES;
    uint32_t readCommands = 0;
    if ((i2cOpType == ACCESS_READ_ONLY) || (i2cOpType == ACCESS_WRITE_RESTART_READ))
        readCommands = 1 + ((numToRead + I2C_ENGINE_CMD_MAX_RX_BYTES - 2) / I2C_ENGINE_CMD_MAX_RX_BYTES);
    uint32_t rstartAndSecondAddrCommands = (i2cOpType == ACCESS_READ_ONLY ? 1 : ((i2cOpType == ACCESS_WRITE_RESTART_READ) ? 2 : 0));
    if (writeCommands + readCommands + rstartAndSecondAddrCommands > (I2C_ENGINE_CMD_QUEUE_SIZE - 2))
        return RAFT_BUS_INVALID;

    // Prepare I2C engine for access
    prepareI2CAccess();

    // Command index
    uint32_t cmdIdx = 0;

    // Start condition
    setI2CCommand(cmdIdx++, ESP32_I2C_CMD_RSTART, 0, false, false, false);

    // Set the address write command(s) - needed in all cases
    // enable ACK processing and check we received an ACK
    // an I2C_NACK_INT will be generated if there is a NACK
    // Add one as the address needs to be written
    uint32_t writeAmountLeft = numToWrite + 1;
    for (uint32_t writeCmdIdx = 0; writeCmdIdx < writeCommands; writeCmdIdx++)
    {
        uint32_t writeAmount = (writeAmountLeft > I2C_ENGINE_CMD_MAX_TX_BYTES) ? I2C_ENGINE_CMD_MAX_TX_BYTES : writeAmountLeft;
        setI2CCommand(cmdIdx++, ESP32_I2C_CMD_WRITE, writeAmount, false, false, true);
        if (writeAmountLeft > 0)
            writeAmountLeft -= writeAmount;
#ifdef DEBUG_I2C_COMMANDS
        LOG_I(MODULE_PREFIX, "access cmdIdx %d writeAmount %d writeAmountLeft %d",
              cmdIdx - 1, writeAmount, writeAmountLeft);
#endif
    }

    // Handle adding a re-start command which is needed only for ACCESS_WRITE_RESTART_READ
    if (i2cOpType == ACCESS_WRITE_RESTART_READ)
        setI2CCommand(cmdIdx++, ESP32_I2C_CMD_RSTART, 0, false, false, false);

    // Handle adding an address+READ which is needed in ACCESS_WRITE_RESTART_READ
    if (i2cOpType == ACCESS_WRITE_RESTART_READ)
        setI2CCommand(cmdIdx++, ESP32_I2C_CMD_WRITE, 1, false, false, true);

    // Handle adding of read commands
    uint32_t readAmountLeft = numToRead;
    for (uint32_t readCmdIdx = 0; readCmdIdx < readCommands; readCmdIdx++)
    {
        // Calculate the amount to read in this command - if it is the penultimate command then
        // reduce the count by 1 to ensure only 1 byte is read in the last command (which is NACKed)
        uint32_t readAmount = (readAmountLeft > I2C_ENGINE_CMD_MAX_RX_BYTES) ? I2C_ENGINE_CMD_MAX_RX_BYTES : readAmountLeft;
        if (readCmdIdx == readCommands - 2)
            readAmount -= 1;
        // An ACK should be sent after each byte received
        setI2CCommand(cmdIdx++, ESP32_I2C_CMD_READ, readAmount, readCmdIdx == readCommands - 1, false, false);
        if (readAmountLeft > 0)
            readAmountLeft -= readAmount;
#ifdef DEBUG_I2C_COMMANDS
        LOG_I(MODULE_PREFIX, "access cmdIdx %d readAmount %d readAmountLeft %d", cmdIdx - 1, readAmount, readAmountLeft);
#endif
    }

    // Stop condition
    setI2CCommand(cmdIdx++, ESP32_I2C_CMD_STOP, 0, false, false, false);

    // Store the read and write buffer pointers and lengths
    _readBufStartPtr = pReadBuf;
    _readBufMaxLen = numToRead;
    _readBufPos = 0;
    _writeBufStartPtr = pWriteBuf;
    _writeBufPos = 0;
    _writeBufLen = numToWrite;

    // Fill the Tx FIFO with as much write data as possible (which may be 0 if we are not writing anything)
    _startAddrPlusRW = (address << 1) | (i2cOpType != ACCESS_READ_ONLY ? 0 : 1);
    _startAddrPlusRWRequired = true;
    _restartAddrPlusRW = (address << 1) | 1;
    _restartAddrPlusRWRequired = i2cOpType == ACCESS_WRITE_RESTART_READ;

    // Fill the Tx FIFO and ensure that FIFO interrupts are disabled if they are not required
    uint32_t interruptsToDisable = fillTxFifo();
    _interruptEnFlags = INTERRUPT_BASE_ENABLES & ~interruptsToDisable;

#ifdef DEBUG_I2C_COMMANDS
    LOG_I(MODULE_PREFIX, "access numWriteCmds %d cmdIdx %d numReadCmds %d numRStarts+2ndAddr %d restartReqd %d",
          writeCommands, cmdIdx, readCommands, rstartAndSecondAddrCommands, _restartAddrPlusRWRequired);
#endif

    // Debug
#ifdef DEBUG_RICI2C_ACCESS
    debugShowStatus("access before: ", address);
#endif

    // Calculate minimum time for entire transaction based on number of bits written/read
    uint32_t totalBytesTxAndRx = (numToRead + 1 + numToWrite + 1);
    uint32_t totalBitsTxAndRx = totalBytesTxAndRx * 10;
    uint32_t minTotalUs = (totalBitsTxAndRx * 1000) / (_busFrequency / 1000);

    // Add overhead for starting/restarting/ending transmission and any clock stretching, etc
    static const uint32_t CLOCK_STRETCH_MAX_PER_BYTE_US = 250;
    uint32_t I2C_START_RESTART_END_OVERHEAD_US = 500 + totalBytesTxAndRx * CLOCK_STRETCH_MAX_PER_BYTE_US;
    uint64_t maxExpectedUs = minTotalUs + I2C_START_RESTART_END_OVERHEAD_US;

#ifdef DEBUG_TIMEOUT_CALCS
    LOG_I(MODULE_PREFIX, "access totalBytesTxAndRx %d totalBitsTxAndRx %d minTotalUs %d overhead %d maxExpectedUs %lld",
          totalBytesTxAndRx, totalBitsTxAndRx, minTotalUs, I2C_START_RESTART_END_OVERHEAD_US, maxExpectedUs);
#endif

    // Clear interrupts and enable
    I2C_DEVICE.int_clr.val = _interruptClearFlags;
    I2C_DEVICE.int_ena.val = _interruptEnFlags;

    // Reset result pending flags
    _accessNackDetected = false;
    _accessResultCode = RAFT_BUS_PENDING;

    // Debug
#ifdef DEBUG_RICI2C_ACCESS
    debugShowStatus("access ready: ", address);
#endif

    // Debug
#ifdef DEBUG_ALL_REGS
    debugShowAllRegs("regsBefore: ", &I2C_DEVICE);
#endif

    // Start communicating
#if defined(CONFIG_IDF_TARGET_ESP32S3) || defined(CONFIG_IDF_TARGET_ESP32C3) || defined(CONFIG_IDF_TARGET_ESP32C6)
    I2C_DEVICE.ctr.clk_en = 1;
    I2C_DEVICE.ctr.conf_upgate = 1;
#endif
    I2C_DEVICE.ctr.trans_start = 1;

    // Wait for a result
    uint64_t startUs = micros();
    while ((_accessResultCode == RAFT_BUS_PENDING) &&
           !Raft::isTimeout((uint64_t)micros(), startUs, maxExpectedUs))
    {
        vTaskDelay(0);
    }

    // Check for software time-out
    if (_accessResultCode == RAFT_BUS_PENDING)
    {
        _accessResultCode = RAFT_BUS_SW_TIME_OUT;
        _i2cStats.recordSoftwareTimeout();
    }

    // Check all of the I2C commands to ensure everything was marked done
    if (_accessResultCode == RAFT_OK)
    {
        for (uint32_t i = 0; i < cmdIdx; i++)
        {
            I2C_COMMAND_REG_TYPE *pCmd = (I2C_COMMAND_REG_TYPE*) &(I2C_DEVICE.I2C_COMMAND_0_REGISTER_NAME);
            if (pCmd[i].done == 0)
            {
#ifdef WARN_RICI2C_ACCESS_INCOMPLETE
                LOG_I(MODULE_PREFIX, "access incomplete addr %02x writeLen %d readLen %d cmdIdx %d cmd %08lx not done",
                      address, numToWrite, numToRead, i, pCmd[i]);
#endif
                _accessResultCode = RAFT_BUS_INCOMPLETE;
                _i2cStats.recordIncompleteTransaction();
                break;
            }
        }
    }

    // Debug
#ifdef DEBUG_TIMING
    uint64_t nowUs = micros();
    String errorMsg;
    bool linesOk = checkI2CLinesOk(errorMsg);
    LOG_I(MODULE_PREFIX, "access timing now %lld elapsedUs %lld maxExpectedUs %lld startUs %lld accessResult %s linesOk %d linesHeld %d",
          nowUs, nowUs - startUs, maxExpectedUs, startUs, getAccessResultStr(_accessResultCode), linesOk, errorMsg.c_str());
#endif

#if defined(DEBUG_RAFT_I2C_CENTRAL_ISR) || defined(DEBUG_RAFT_I2C_CENTRAL_ISR_ON_FAIL)
#ifdef DEBUG_RAFT_I2C_CENTRAL_ISR_ON_FAIL
    if (_accessResultCode != ACCESS_RESULT_OK)
#endif
    {
        uint32_t numDebugElems = _debugI2CISR.getCount();
        debugShowStatus("access after: ", address);
        LOG_I(MODULE_PREFIX, "access rslt %s ISR calls ...", getAccessResultStr(_accessResultCode));
        for (uint32_t i = 0; i < numDebugElems; i++)
            LOG_I(MODULE_PREFIX, "... %s", _debugI2CISR.getElem(i).toStr().c_str());
    }
#endif

    // Empty Rx FIFO to extract any read data
    emptyRxFifo();
    numRead = _readBufPos;

    // Clear the read and write buffer pointers defensively - in case of spurious ISRs after this point
    _readBufStartPtr = nullptr;
    _writeBufStartPtr = nullptr;

    // Debug
#ifdef DEBUG_RICI2C_ACCESS
    LOG_I(MODULE_PREFIX, "access addr %02x %s", address, getAccessResultStr(_accessResultCode));
    debugShowStatus("access end: ", address);
#endif

    return _accessResultCode;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Check that the I2C module is ready and reset it if not
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool RaftI2CCentral::ensureI2CReady()
{
    // Check if busy (shouldn't be at this point!)
    if (isBusy())
    {
        // Reinit I2C
        reinitI2CModule();
    }

    // Check if still busy
    if (isBusy())
    {
        // Debug
#ifdef DEBUG_BUS_NOT_READY_WITH_GPIO_NUM
        pinMode(DEBUG_BUS_NOT_READY_WITH_GPIO_NUM, OUTPUT);
        for (int i = 0; i < 50; i++)
        {
            digitalWrite(DEBUG_BUS_NOT_READY_WITH_GPIO_NUM, HIGH);
            delayMicroseconds(1);
            digitalWrite(DEBUG_BUS_NOT_READY_WITH_GPIO_NUM, LOW);
            delayMicroseconds(1);
            if (!I2C_DEVICE.I2C_STATUS_REGISTER_NAME.bus_busy)
                break;
        }
#endif

        if (Raft::isTimeout(millis(), _lastCheckI2CReadyMs, _lastCheckI2CReadyIntervalMs))
        {

#ifdef WARN_ON_BUS_IS_BUSY
            LOG_W(MODULE_PREFIX, "ensureI2CReady bus is busy ... resetting\n");
#endif

            // Other checks on I2C should delayed more
            _lastCheckI2CReadyIntervalMs = I2C_READY_CHECK_INTERVAL_OTHER_MS;
            _lastCheckI2CReadyMs = millis();

            // Should not be busy - so reinit I2C
            reinitI2CModule();

            // Wait a moment
            delayMicroseconds(50);

            // Check if now not busy
            if (isBusy())
            {
                // Something more seriously wrong
#ifdef WARN_ON_BUS_CANNOT_BE_RESET
                String busLinesErrorMsg;
                checkI2CLinesOk(busLinesErrorMsg);
                LOG_W(MODULE_PREFIX, "ensureI2CReady bus still busy ... %s\n", busLinesErrorMsg.c_str());
#endif
            }
        }
        return false;
    }
    return true;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Prepare for I2C accsss
// This sets the FIFO thresholds, timeouts, etc
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void RaftI2CCentral::prepareI2CAccess()
{
    // Ensure the state machine is stopped
    I2C_DEVICE.ctr.trans_start = 0;

    // Default timeout
    setDefaultTimeout();

    // Lock fifos
    typeof(I2C_DEVICE.fifo_conf) fifoConf;
    fifoConf.val = I2C_DEVICE.fifo_conf.val;
    fifoConf.tx_fifo_rst = 1;
    fifoConf.rx_fifo_rst = 1;
    I2C_DEVICE.fifo_conf.val = fifoConf.val;

    // Set the fifo thresholds to reasonable values given 100KHz bus speed and 180MHz processor clock
    // the threshold must allow for interrupt latency
    // ESP IDF uses full threshold of 11 and empty threshold of 4
#if defined(CONFIG_IDF_TARGET_ESP32S3) || defined(CONFIG_IDF_TARGET_ESP32C6)
    fifoConf.rxfifo_wm_thrhd = 24;
    fifoConf.txfifo_wm_thrhd = 6;
    fifoConf.fifo_prt_en = 1;
#elif defined(CONFIG_IDF_TARGET_ESP32C3)
    fifoConf.rx_fifo_wm_thrhd = 24;
    fifoConf.tx_fifo_wm_thrhd = 6;
    fifoConf.fifo_prt_en = 1;
#else
    fifoConf.rx_fifo_full_thrhd = 24;
    fifoConf.tx_fifo_empty_thrhd = 6;
#endif
    I2C_DEVICE.fifo_conf.val = fifoConf.val;

    // Release fifos
    fifoConf.tx_fifo_rst = 0;
    fifoConf.rx_fifo_rst = 0;
    I2C_DEVICE.fifo_conf.val = fifoConf.val;

    // Disable interrupts and clear
    I2C_DEVICE.int_ena.val = 0;
    I2C_DEVICE.int_clr.val = UINT32_MAX;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Re-init the I2C hardware
// Used initially and when a bus lockup is detected
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void RaftI2CCentral::reinitI2CModule()
{
    // Clear interrupts
    I2C_DEVICE.int_ena.val = 0;
    I2C_DEVICE.int_clr.val = UINT32_MAX;

    // Reset hardware
#if defined(CONFIG_IDF_TARGET_ESP32S3) || defined(CONFIG_IDF_TARGET_ESP32C3) || defined(CONFIG_IDF_TARGET_ESP32C6)
    I2C_DEVICE.ctr.fsm_rst = 1;
    I2C_DEVICE.ctr.fsm_rst = 0;
    vTaskDelay(1);
#else
    DPORT_SET_PERI_REG_MASK(DPORT_PERIP_RST_EN_REG, _i2cPort == 0 ? DPORT_I2C_EXT0_RST : DPORT_I2C_EXT1_RST);
    DPORT_SET_PERI_REG_MASK(DPORT_PERIP_CLK_EN_REG, _i2cPort == 0 ? DPORT_I2C_EXT0_CLK_EN : DPORT_I2C_EXT1_CLK_EN);
    DPORT_CLEAR_PERI_REG_MASK(DPORT_PERIP_RST_EN_REG, _i2cPort == 0 ? DPORT_I2C_EXT0_RST : DPORT_I2C_EXT1_RST);
#endif

    // Clear hardware control register ...
    // Set master mode, normal sda and scl output, enable clock
    typeof(I2C_DEVICE.ctr) ctrl_reg;
    ctrl_reg.val = 0;
    ctrl_reg.ms_mode = 1;
    ctrl_reg.sda_force_out = 1;
    ctrl_reg.scl_force_out = 1;
    ctrl_reg.clk_en = 1;
    I2C_DEVICE.ctr.val = ctrl_reg.val;

    // Set data mode
#if defined(CONFIG_IDF_TARGET_ESP32S3) || defined(CONFIG_IDF_TARGET_ESP32C3) || defined(CONFIG_IDF_TARGET_ESP32C6)
    I2C_DEVICE.ctr.tx_lsb_first = I2C_DATA_MODE_MSB_FIRST;
    I2C_DEVICE.ctr.rx_lsb_first = I2C_DATA_MODE_MSB_FIRST;
#endif

    // Set timeout to default
    setDefaultTimeout();

    // Clear FIFO config
    I2C_DEVICE.fifo_conf.val = 0;

#if defined(CONFIG_IDF_TARGET_ESP32S3) || defined(CONFIG_IDF_TARGET_ESP32C3) || defined(CONFIG_IDF_TARGET_ESP32C6)
    // Set FIFO mode
    I2C_DEVICE.fifo_conf.nonfifo_en = 0;

    // Reset fifos
    I2C_DEVICE.fifo_conf.tx_fifo_rst = 1;
    I2C_DEVICE.fifo_conf.tx_fifo_rst = 0;
    I2C_DEVICE.fifo_conf.rx_fifo_rst = 1;
    I2C_DEVICE.fifo_conf.rx_fifo_rst = 0;
#endif

    // Set bus frequency
    setBusFrequency(_busFrequency);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Set I2C bus frequency
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool RaftI2CCentral::setBusFrequency(uint32_t busFreq)
{
#if defined(CONFIG_IDF_TARGET_ESP32S3) || defined(CONFIG_IDF_TARGET_ESP32C3) || defined(CONFIG_IDF_TARGET_ESP32C6)

    i2c_hal_clk_config_t clk_cal;
    uint32_t sourceClockFreq = esp_clk_xtal_freq();
    uint32_t clkm_div = sourceClockFreq / (busFreq * 1024) +1;
    uint32_t sclk_freq = sourceClockFreq / clkm_div;
    uint32_t half_cycle = sclk_freq / busFreq / 2;

    //SCL
    clk_cal.clkm_div = clkm_div;
    clk_cal.scl_low = half_cycle;
    // default, scl_wait_high < scl_high
    // Make 80KHz as a boundary here, because when working at lower frequency, too much scl_wait_high will faster the frequency
    // according to some hardware behaviors.
    clk_cal.scl_wait_high = (busFreq >= 80*1000) ? (half_cycle / 2 - 2) : (half_cycle / 4);
    clk_cal.scl_high = half_cycle - clk_cal.scl_wait_high;
    clk_cal.sda_hold = half_cycle / 4;
    clk_cal.sda_sample = half_cycle / 2 + clk_cal.scl_wait_high;
    clk_cal.setup = half_cycle;
    clk_cal.hold = half_cycle;
    //default we set the timeout value to about 10 bus cycles
    // log(20*half_cycle)/log(2) = log(half_cycle)/log(2) +  log(20)/log(2)
    clk_cal.tout = (int)(sizeof(half_cycle) * 8 - __builtin_clz(5 * half_cycle)) + 2;

    // LOG_I(MODULE_PREFIX, "setBusFrequency %ld, sourceClockFreq %ld, clkm_div %ld, sclk_freq %ld, half_cycle %ld, scl_low %ld, scl_wait_high %ld, scl_high %ld, sda_hold %ld, sda_sample %ld, setup %ld, hold %ld, tout %ld",
    //     busFreq, sourceClockFreq, clkm_div, sclk_freq, half_cycle, clk_cal.scl_low, clk_cal.scl_wait_high, clk_cal.scl_high, clk_cal.sda_hold, clk_cal.sda_sample, clk_cal.setup, clk_cal.hold, clk_cal.tout);

    // Ensure 32bit access
    typeof(I2C_DEVICE.clk_conf) tmpReg;
    tmpReg.val = I2C_DEVICE.clk_conf.val;
    tmpReg.sclk_sel = 0;
    tmpReg.sclk_div_num = clk_cal.clkm_div - 1;
    I2C_DEVICE.clk_conf.val = tmpReg.val;

    /* According to the Technical Reference Manual, the following timings must be subtracted by 1.
     * However, according to the practical measurement and some hardware behaviour, if wait_high_period and scl_high minus one.
     * The SCL frequency would be a little higher than expected. Therefore, the solution
     * here is not to minus scl_high as well as scl_wait high, and the frequency will be absolutely accurate to all frequency
     * to some extent. */
    typeof(I2C_DEVICE.scl_low_period) scl_low_period_reg;
    scl_low_period_reg.val = 0;
    scl_low_period_reg.I2C_SCL_LOW_PERIOD_PERIOD_NAME = clk_cal.scl_low - 1;
    I2C_DEVICE.scl_low_period.val = scl_low_period_reg.val;
    I2C_DEVICE.scl_high_period.I2C_SCL_HIGH_PERIOD_PERIOD_NAME = clk_cal.scl_high;
    I2C_DEVICE.scl_high_period.scl_wait_high_period = clk_cal.scl_wait_high;
    //sda sample
    I2C_DEVICE.sda_hold.I2C_SDA_HOLD_TIME_NAME = clk_cal.sda_hold - 1;
    I2C_DEVICE.sda_sample.I2C_SDA_SAMPLE_TIME_NAME = clk_cal.sda_sample - 1;
    //setup
    I2C_DEVICE.scl_rstart_setup.I2C_SCL_RSTART_SETUP_TIME_NAME = clk_cal.setup - 1;
    I2C_DEVICE.scl_stop_setup.I2C_SCL_STOP_SETUP_TIME_NAME = clk_cal.setup - 1;
    //hold
    I2C_DEVICE.scl_start_hold.I2C_SCL_START_HOLD_TIME_NAME= clk_cal.hold - 1;
    I2C_DEVICE.scl_stop_hold.I2C_SCL_STOP_HOLD_TIME_NAME = clk_cal.hold - 1;
    I2C_DEVICE.I2C_TIMEOUT_REG_NAME.time_out_value = clk_cal.tout;
    I2C_DEVICE.I2C_TIMEOUT_REG_NAME.time_out_en = 1;

    // LOG_I(MODULE_PREFIX, "scl_low_period_reg_val %ld wrote %ld", I2C_DEVICE.scl_low_period.val, scl_low_period_reg.val);

#else

    static const uint32_t MIN_RATIO_CPU_TICKS_TO_I2C_BUS_TICKS = 100;
    static const uint32_t MAX_BUS_CLOCK_PERIOD = 4095;

    // Check valid
    uint32_t apbFrequency = getApbFrequency();
    uint32_t period = (apbFrequency / busFreq);
    uint32_t minBusFreq = apbFrequency / 8192;
    uint32_t maxBusFreq = apbFrequency / MIN_RATIO_CPU_TICKS_TO_I2C_BUS_TICKS;
    if ((minBusFreq > busFreq) || (maxBusFreq < busFreq))
    {
        LOG_W(MODULE_PREFIX, "setFrequency outOfBounds requested %d min %d max %d modifying...", busFreq, minBusFreq, maxBusFreq);
    }

    // Modify to be valid
    if (period < (MIN_RATIO_CPU_TICKS_TO_I2C_BUS_TICKS / 2))
    {
        period = (MIN_RATIO_CPU_TICKS_TO_I2C_BUS_TICKS / 2);
        busFreq = apbFrequency / (period * 2);
        LOG_W(MODULE_PREFIX, "setFrequency reduced %d", busFreq);
    }
    else if (period > MAX_BUS_CLOCK_PERIOD)
    {
        period = MAX_BUS_CLOCK_PERIOD;
        busFreq = apbFrequency / (period * 2);
        LOG_W(MODULE_PREFIX, "setFrequency increased %d", busFreq);
    }

    // Calculate half and quarter periods
    uint32_t halfPeriod = period / 2;
    uint32_t quarterPeriod = period / 4;

    // Set the low-level and high-level width of SCL
    I2C_DEVICE.scl_low_period.period = halfPeriod;
    I2C_DEVICE.scl_high_period.period = halfPeriod;

    // Set the start-marker timing between SDA going low and SCL going low
    I2C_DEVICE.scl_start_hold.time = halfPeriod;

    // Set the restart-marker timing between SCL going high and SDA going low
    I2C_DEVICE.scl_rstart_setup.time = halfPeriod;

    // Set timing of stop-marker
    I2C_DEVICE.scl_stop_hold.time = halfPeriod;
    I2C_DEVICE.scl_stop_setup.time = halfPeriod;

    // Set the time period to hold data after SCL goes low and sampling SDA after SCL going high
    // These are actually not used in master mode
    I2C_DEVICE.sda_hold.time = quarterPeriod;
    I2C_DEVICE.sda_sample.time = quarterPeriod;

#endif

    return true;
}

// Get APB frequency (used for I2C)
uint32_t RaftI2CCentral::getApbFrequency()
{
    rtc_cpu_freq_config_t conf;
    rtc_clk_cpu_freq_get_config(&conf);
    if (conf.freq_mhz >= 80)
        return 80000000;
    return (conf.source_freq_mhz * 1000000U) / conf.div;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Set a command into the ESP32 I2C command registers
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void RaftI2CCentral::setI2CCommand(uint32_t cmdIdx, uint8_t op_code, uint8_t byte_num, bool ack_val, bool ack_exp, bool ack_en)
{
    // Check valid
    if (cmdIdx >= I2C_ENGINE_CMD_QUEUE_SIZE)
        return;

    // Clear and then set all values
    I2C_COMMAND_REG_TYPE cmdVal;
    cmdVal.val = 0;
    cmdVal.op_code = op_code;
    cmdVal.byte_num = byte_num;
    cmdVal.ack_val = ack_val;
    cmdVal.ack_exp = ack_exp;
    cmdVal.ack_en = ack_en;
#if defined(CONFIG_IDF_TARGET_ESP32S3) || defined(CONFIG_IDF_TARGET_ESP32C3) || defined(CONFIG_IDF_TARGET_ESP32C6)
    (&I2C_DEVICE.I2C_COMMAND_0_REGISTER_NAME)[cmdIdx].val = cmdVal.val;
    // LOG_I(MODULE_PREFIX, "setI2CCommand idx %d op %d byte %d ackv %d ackexp %d acken %d val %08x addr %p readback %08x", 
    //         cmdIdx, op_code, byte_num, ack_val, ack_exp, ack_en, cmdVal.val, (&I2C_DEVICE.comd0) + cmdIdx, 
    //         (&I2C_DEVICE.comd0)[cmdIdx].val);
#else
    I2C_DEVICE.command[cmdIdx].val = cmdVal.val;
#endif    
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Initialise interrupts
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool RaftI2CCentral::initInterrupts()
{
    // ISR flags allowing for calling if cache disabled, low/medium priority, shared interrupts
    uint32_t isrFlags = ESP_INTR_FLAG_IRAM | ESP_INTR_FLAG_LOWMED | ESP_INTR_FLAG_SHARED;

    // Enable flags
    _interruptEnFlags = INTERRUPT_BASE_ENABLES;

    // Clear flags
    _interruptClearFlags = INTERRUPT_BASE_CLEARS;

    // Clear any pending interrupt
    I2C_DEVICE.int_clr.val = _interruptClearFlags;

    // Create ISR using the mask to define which interrupts to test for
    esp_err_t retc = ESP_OK;
    if (_i2cPort == 0)
        esp_intr_alloc_intrstatus(ETS_I2C_EXT0_INTR_SOURCE, isrFlags, (uint32_t) & (I2C_DEVICE.int_status.val),
                                  _interruptEnFlags, i2cISRStatic, this, &_i2cISRHandle);
#if !defined(CONFIG_IDF_TARGET_ESP32C3) && !defined(CONFIG_IDF_TARGET_ESP32C6)
    else
        esp_intr_alloc_intrstatus(ETS_I2C_EXT1_INTR_SOURCE, isrFlags, (uint32_t) & (I2C_DEVICE.int_status.val),
                                  _interruptEnFlags, i2cISRStatic, this, &_i2cISRHandle);
#endif

    // Warn if problems
    if (retc != ESP_OK)
    {
        LOG_W(MODULE_PREFIX, "initInterrupts failed retc %d\n", retc);
    }
    return retc == ESP_OK;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Initialise bus filtering if enabled
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void RaftI2CCentral::initBusFiltering()
{
    I2C_DEVICE.I2C_FILTER_CFG_SCL_THRESH = _busFilteringLevel;
    I2C_DEVICE.I2C_FILTER_CFG_SDA_THRESH = _busFilteringLevel;
    I2C_DEVICE.I2C_FILTER_CFG_SCL_ENABLE = _busFilteringLevel != 0;
    I2C_DEVICE.I2C_FILTER_CFG_SDA_ENABLE = _busFilteringLevel != 0;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Check I2C lines are ok - assumes bus is idle so lines should be high
// Checks both I2C lines (SCL and SDA) for pin-held-low problems
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool RaftI2CCentral::checkI2CLinesOk(String& busLinesErrorMsg)
{
    // Check if SDA or SCL are held low
    bool sdaHeld = gpio_get_level((gpio_num_t)_pinSDA) == 0;
    bool sclHeld = gpio_get_level((gpio_num_t)_pinSCL) == 0;
    busLinesErrorMsg = sdaHeld && sclHeld ? "SDA & SCL held low" : sdaHeld ? "SDA held low" : sclHeld ? "SCL held low" : "";
    return sdaHeld || sclHeld;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Interrupt handler
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void RaftI2CCentral::i2cISRStatic(void *arg)
{
    // Get object
    RaftI2CCentral *pArg = (RaftI2CCentral *)arg;
    if (pArg)
        pArg->i2cISR();
}

void RaftI2CCentral::i2cISR()
{
    uint32_t intStatus = I2C_DEVICE.int_status.val;
#if defined(DEBUG_RAFT_I2C_CENTRAL_ISR) || defined(DEBUG_RAFT_I2C_CENTRAL_ISR_ON_FAIL)
    _debugI2CISR.debugISRAdd("", intStatus, I2C_DEVICE.I2C_STATUS_REGISTER_NAME.val, I2C_DEVICE.fifo_st.val);
#endif

    // Update
    _i2cStats.update(intStatus & I2C_TRANS_START_INT_ST,
                     intStatus & I2C_ACK_ERR_INT_ST,
                     intStatus & I2C_TIME_OUT_INT_ST,
                     intStatus & I2C_TRANS_COMPLETE_INT_ST,
                     intStatus & I2C_ARBITRATION_LOST_INT_ST,
                     intStatus & I2C_MASTER_TRAN_COMP_INT_ST,
                     intStatus & I2C_TXFIFO_EMPTY_INT_ST);

    // Track results & interrupts to disable that are no longer needed
    RaftRetCode rsltCode = RAFT_BUS_PENDING;
    uint32_t interruptsToDisable = 0;

    // Check which interrupt has occurred
    if (intStatus & I2C_TIME_OUT_INT_ST)
    {
        rsltCode = RAFT_BUS_HW_TIME_OUT;
    }
    else if (intStatus & I2C_ACK_ERR_INT_ST)
    {
        _accessNackDetected = true;
    }
    else if (intStatus & I2C_ARBITRATION_LOST_INT_ST)
    {
        rsltCode = RAFT_BUS_ARB_LOST;
    }
    else if (intStatus & I2C_TRANS_COMPLETE_INT_ST)
    {
        // Set flag indicating successful completion
        rsltCode = _accessNackDetected ? RAFT_BUS_ACK_ERROR : RAFT_OK;
    }
    if (rsltCode != RAFT_BUS_PENDING)
    {
#ifdef DEBUG_ISR_USING_GPIO_NUM
        gpio_set_level((gpio_num_t)DEBUG_ISR_USING_GPIO_NUM, 1);
        gpio_set_level((gpio_num_t)DEBUG_ISR_USING_GPIO_NUM, 1);
        gpio_set_level((gpio_num_t)DEBUG_ISR_USING_GPIO_NUM, 0);
#endif

        // Disable and clear interrupts
        I2C_DEVICE.int_ena.val = 0;
        I2C_DEVICE.int_clr.val = _interruptClearFlags;

        // Set flag indicating successful completion
        if (_accessResultCode == RAFT_BUS_PENDING)
            _accessResultCode = rsltCode;
        return;
    }

    // Check for Tx FIFO needing to be refilled
    if (intStatus & I2C_TXFIFO_EMPTY_INT_ST)
    {
        // Refill the FIFO if possible
        interruptsToDisable |= fillTxFifo();
    }

    // Check for Rx FIFO needing to be emptied
    if (intStatus & I2C_RXFIFO_FULL_INT_ST)
    {
        // Empty the FIFO
        interruptsToDisable |= emptyRxFifo();
    }

    // Disable and clear interrupts
    I2C_DEVICE.int_ena.val = 0;
    I2C_DEVICE.int_clr.val = _interruptClearFlags;

    // Remove enables on interrupts no longer wanted
    _interruptEnFlags &= ~interruptsToDisable;

    // Restore interrupt enables
    I2C_DEVICE.int_ena.val = _interruptEnFlags;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Fill Tx FIFO and return bit mask of interrupts to disable
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

uint32_t RaftI2CCentral::fillTxFifo()
{
#if defined(CONFIG_IDF_TARGET_ESP32S3) || defined(CONFIG_IDF_TARGET_ESP32C3) || defined(CONFIG_IDF_TARGET_ESP32C6)
    // Check if we need to add the start address+RW to the Tx FIFO
    if (_startAddrPlusRWRequired)
    {
        I2C_DEVICE.I2C_DATA_FIFO_DATA_REG.I2C_DATA_FIFO_READ_DATA_REG = _startAddrPlusRW;
        _startAddrPlusRWRequired = false;
    }

    // Check write buffer valid
    if (!_writeBufStartPtr)
        return TX_FIFO_NEARING_EMPTY_INT;

    // Fill fifo with data to write
    uint32_t remainingBytes = _writeBufLen - _writeBufPos;
    uint32_t toSend = I2C_ENGINE_FIFO_SIZE - I2C_DEVICE.I2C_STATUS_REGISTER_NAME.I2C_STATUS_REGISTER_TX_FIFO_CNT_NAME;
    if (toSend > remainingBytes)
        toSend = remainingBytes;
    for (uint32_t i = 0; i < toSend; i++)
    {
        I2C_DEVICE.I2C_DATA_FIFO_DATA_REG.I2C_DATA_FIFO_READ_DATA_REG = _writeBufStartPtr[_writeBufPos];
        _writeBufPos = _writeBufPos + 1;
    }

    // If we have finished all data to write AND
    // if we have a restart address to send AND
    // there is space in the FIFO then add the restart address
    if ((_writeBufPos == _writeBufLen) && _restartAddrPlusRWRequired &&
        (I2C_DEVICE.I2C_STATUS_REGISTER_NAME.I2C_STATUS_REGISTER_TX_FIFO_CNT_NAME < I2C_ENGINE_FIFO_SIZE))
    {
        I2C_DEVICE.I2C_DATA_FIFO_DATA_REG.I2C_DATA_FIFO_READ_DATA_REG = _restartAddrPlusRW;
        _restartAddrPlusRWRequired = false;
    }

#else
    // Check if we need to add the start address+RW to the Tx FIFO
    if (_startAddrPlusRWRequired)
    {
        I2C_DEVICE.fifo_data.data = _startAddrPlusRW;
        _startAddrPlusRWRequired = false;
    }

    // Check write buffer valid
    if (!_writeBufStartPtr)
        return TX_FIFO_NEARING_EMPTY_INT;

    // Fill fifo with data to write
    uint32_t remainingBytes = _writeBufLen - _writeBufPos;
    uint32_t toSend = I2C_ENGINE_FIFO_SIZE - I2C_DEVICE.status_reg.tx_fifo_cnt;
    if (toSend > remainingBytes)
        toSend = remainingBytes;
    for (uint32_t i = 0; i < toSend; i++)
    {
        I2C_DEVICE.fifo_data.data = _writeBufStartPtr[_writeBufPos];
        _writeBufPos = _writeBufPos + 1;
    }

    // If we have finished all data to write AND
    // if we have a restart address to send AND
    // there is space in the FIFO then add the restart address
    if ((_writeBufPos == _writeBufLen) && _restartAddrPlusRWRequired &&
        (I2C_DEVICE.status_reg.tx_fifo_cnt < I2C_ENGINE_FIFO_SIZE))
    {
        I2C_DEVICE.fifo_data.data = _restartAddrPlusRW;
        _restartAddrPlusRWRequired = false;
    }
#endif

    // Return flag indicating if Tx FIFO nearing empty interrupt should be disabled
    return ((_writeBufPos < _writeBufLen) || _restartAddrPlusRWRequired) ? 0 : TX_FIFO_NEARING_EMPTY_INT;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Empty Rx FIFO and return bit mask of interrupts to disable
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

uint32_t RaftI2CCentral::emptyRxFifo()
{
    // Check valid
    if (!_readBufStartPtr)
        return RX_FIFO_NEARING_FULL_INT;

    // Start critical section for access to I2C FIFO
    portENTER_CRITICAL_ISR(&_i2cAccessMutex);

    // Empty received data from the Rx FIFO
#if defined(CONFIG_IDF_TARGET_ESP32S3) || defined(CONFIG_IDF_TARGET_ESP32C3) || defined(CONFIG_IDF_TARGET_ESP32C6)
    uint32_t toGet = I2C_DEVICE.I2C_STATUS_REGISTER_NAME.I2C_STATUS_REGISTER_RX_FIFO_CNT_NAME;
    for (uint32_t i = 0; i < toGet; i++)
    {
        if (_readBufPos >= _readBufMaxLen)
            break;
            
        typeof(I2C_DEVICE.I2C_DATA_FIFO_DATA_REG) dataReg;
        dataReg.val = I2C_DEVICE.I2C_DATA_FIFO_DATA_REG.val;
        _readBufStartPtr[_readBufPos] = dataReg.I2C_DATA_FIFO_READ_DATA_REG;
        _readBufPos = _readBufPos + 1;
    }
#else
    uint32_t toGet = I2C_DEVICE.I2C_STATUS_REGISTER_NAME.rx_fifo_cnt;
    for (uint32_t i = 0; i < toGet; i++)
    {
        if (_readBufPos >= _readBufMaxLen)
            break;
        _readBufStartPtr[_readBufPos] = I2C_DEVICE.fifo_data.val;
        _readBufPos = _readBufPos + 1;
    }
#endif

    // Exit ctitical section
    portEXIT_CRITICAL_ISR(&_i2cAccessMutex);
    return (_readBufPos >= _readBufMaxLen) ? RX_FIFO_NEARING_FULL_INT : 0;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// De-initialise the I2C hardware
// Frees pins and interrupts
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void RaftI2CCentral::deinit()
{
    // Check for ISR
    if (_i2cISRHandle)
        esp_intr_free(_i2cISRHandle);
    _i2cISRHandle = nullptr;

    // Check initialised and release pins if so
    if (_isInitialised)
    {
        if (_pinSDA >= 0)
            gpio_reset_pin((gpio_num_t)_pinSDA);
        if (_pinSCL >= 0)
            gpio_reset_pin((gpio_num_t)_pinSCL);
    }
    _isInitialised = false;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Set default timeout
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void RaftI2CCentral::setDefaultTimeout()
{
#if defined(CONFIG_IDF_TARGET_ESP32S3) || defined(CONFIG_IDF_TARGET_ESP32C3) || defined(CONFIG_IDF_TARGET_ESP32C6)
    I2C_DEVICE.I2C_TIMEOUT_REG_NAME.time_out_value = 16;
    I2C_DEVICE.I2C_TIMEOUT_REG_NAME.time_out_en = 1;
#else
    // Timout default
    I2C_DEVICE.I2C_TIMEOUT_REG_NAME.tout = 0x8000;
#endif

}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Debugging functions used to format debug data
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

String RaftI2CCentral::debugMainStatusStr(const char *prefix, uint32_t statusFlags)
{
    const char *SCLStates[] = {
        "Idle", "Start", "NegEdge", "Low", "PosEdge", "High", "Stop", "Unknown"};
    const char *MainStates[] = {
        "Idle", "AddrShift", "ACKAddr", "RxData", "RxData", "SendACK", "WaitACK", "Unknown"};
    const char *FlagNames[] = {
        "AckValue",
        "SlaveRw",
        "TimeOut",
        "ArbLost",
        "BusBusy",
        "SlaveAddr",
        "ByteTrans"};
    char outStr[150];
    snprintf(outStr, sizeof(outStr), "%sSCLState %s MainState %s TxFifoCount %lu RxFifoCount %lu",
             prefix,
             SCLStates[(statusFlags >> I2C_SCL_STATE_LAST_S) & I2C_SCL_STATE_LAST_V],
             MainStates[(statusFlags >> I2C_SCL_MAIN_STATE_LAST_S) & I2C_SCL_MAIN_STATE_LAST_V],
             (unsigned long)(statusFlags >> I2C_TXFIFO_CNT_S) & I2C_TXFIFO_CNT_V,
             (unsigned long)(statusFlags >> I2C_RXFIFO_CNT_S) & I2C_RXFIFO_CNT_V);
    static const uint32_t numFlags = sizeof(FlagNames) / sizeof(FlagNames[0]);
    uint32_t maskBit = 1u << (numFlags - 1);
    for (uint32_t i = 0; i < numFlags; i++)
    {
        if (statusFlags & maskBit)
        {
            strlcat(outStr, " ", sizeof(outStr));
            uint32_t bitIdx = numFlags - 1 - i;
            strlcat(outStr, FlagNames[bitIdx], sizeof(outStr));
        }
        maskBit = maskBit >> 1;
    }
    return outStr;
}

String RaftI2CCentral::debugFIFOStatusStr(const char *prefix, uint32_t statusFlags)
{
    char outStr[60];
    snprintf(outStr, sizeof(outStr), "%sTxFIFOStart %lu TxFIFOEnd %lu RxFIFOStart %lu RxFIFOEnd %lu",
             prefix,
             (unsigned long)(statusFlags >> 10) & 0x1f,
             (unsigned long)(statusFlags >> 15) & 0x1f,
             (unsigned long)(statusFlags >> 0) & 0x1f,
             (unsigned long)(statusFlags >> 5) & 0x1f);
    return outStr;
}

String RaftI2CCentral::debugINTFlagStr(const char *prefix, uint32_t statusFlags)
{
#if defined(CONFIG_IDF_TARGET_ESP32S3) || defined(CONFIG_IDF_TARGET_ESP32C3) || defined(CONFIG_IDF_TARGET_ESP32C6)
    const char *intStrs[] = {
        "RxFIFOFull",
        "TxFIFOEmpty",
        "RxFIFOOvf",
        "EndDetect",
        "ByteTransComp",
        "ArbLost",
        "MasterTransComp",
        "TransComp",
        "TimeOut",
        "TransStart",
        "NACK",
        "TxFIFOOvf",
        "RxFIFOOvf",
        "SCLStuck",
        "SCLStuckMain",
        "StartDetect",
        "SlaveStretch",
        "GenCall"
    };
#else
    const char *intStrs[] = {
        "RxFIFOFull",
        "TxFIFOEmpty",
        "RxFIFOOvf",
        "EndDetect",
        "ByteTransComp",
        "ArbLost",
        "MasterTransComp",
        "TransComplete",
        "TimeOut",
        "TransStart",
        "ACKError",
        "SendEmpty",
        "RxFull",
    };
#endif
    char outStr[200];
    snprintf(outStr, sizeof(outStr), "%s", prefix);
    uint32_t maskBit = 1u << 31;
    bool sepNeeded = false;
    for (uint32_t i = 0; i < 32; i++)
    {
        if (statusFlags & maskBit)
        {
            if (sepNeeded)
                strlcat(outStr, " ", sizeof(outStr));
            sepNeeded = true;
            uint32_t bitIdx = 31 - i;
            if (bitIdx > (sizeof(intStrs) / sizeof(intStrs[0])))
            {
                int outStrLen = strnlen(outStr, sizeof(outStr));
                snprintf(outStr + outStrLen, sizeof(outStr) - outStrLen, "UnknownBit%lu", (unsigned long)bitIdx);
            }
            else
            {
                strlcat(outStr, intStrs[bitIdx], sizeof(outStr));
            }
        }
        maskBit = maskBit >> 1;
    }
    return outStr;
}

void RaftI2CCentral::debugShowAllRegs(const char *prefix, i2c_dev_t* pDev)
{
    // Debug dump all I2C registers
    LOG_I(MODULE_PREFIX, "%sRegs %p", prefix, pDev);
    uint32_t* pRegs = (uint32_t*)pDev;
    uint32_t numRegs = sizeof(i2c_dev_t) / sizeof(uint32_t);
    String regsStr = "";
    uint32_t startReg = 0;
    for (uint32_t i = 0; i < numRegs; i++)
    {
        char regStr[20];
        snprintf(regStr, sizeof(regStr), "%08lx ", (unsigned long)pRegs[i]);
        regsStr += regStr;
        if ((i % 8 == 7) || (i == numRegs - 1))
        {
            LOG_I(MODULE_PREFIX, "%sRegs %3d..%3d %s", prefix, startReg, i, regsStr.c_str());
            regsStr = "";
            startReg = i + 1;
        }

    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Debugging function used to log I2C status information
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void RaftI2CCentral::debugShowStatus(const char *prefix, uint32_t addr)
{
    LOG_I(MODULE_PREFIX, "%s addr 0x%02x INTRAW (0x%08x) %s MAIN (0x%08x) %s FIFO (0x%08x) %s Stats %s",
          prefix, addr,
          I2C_DEVICE.int_raw.val, debugINTFlagStr("", I2C_DEVICE.int_raw.val).c_str(),
          I2C_DEVICE.I2C_STATUS_REGISTER_NAME.val, debugMainStatusStr("", I2C_DEVICE.I2C_STATUS_REGISTER_NAME.val).c_str(),
          I2C_DEVICE.fifo_st.val, debugFIFOStatusStr("", I2C_DEVICE.fifo_st.val).c_str(),
          _i2cStats.debugStr().c_str());
    String cmdRegsStr;
    Raft::getHexStrFromUint32(const_cast<uint32_t *>(&(I2C_DEVICE.I2C_COMMAND_0_REGISTER_NAME.val)),
                               I2C_ENGINE_CMD_QUEUE_SIZE, cmdRegsStr, " ");
    LOG_I(MODULE_PREFIX, "%s Cmds %s", prefix, cmdRegsStr.c_str());
    // String memStr;
    // Raft::getHexStrFromUint32(const_cast<uint32_t *>(&(I2C_DEVICE.I2C_TX_FIFO_RAM_ADDR_0_NAME)),
    //                            I2C_ENGINE_FIFO_SIZE, memStr, " ");
    // LOG_I(MODULE_PREFIX, "___TxFifo[0] %s", memStr.c_str());

}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// DebugI2CISRElem
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#ifdef DEBUG_RAFT_I2C_CENTRAL_ISR

RaftI2CCentral::DebugI2CISRElem::DebugI2CISRElem()
{
    clear();
}

void RaftI2CCentral::DebugI2CISRElem::clear()
{
    _micros = 0;
    _msg[0] = 0;
    _intStatus = 0;
}

void RaftI2CCentral::DebugI2CISRElem::set(const char* msg, uint32_t intStatus, 
                        uint32_t mainStatus, uint32_t txFifoStatus)
{
    _micros = micros();
    _intStatus = intStatus;
    _mainStatus = mainStatus;
    _txFifoStatus = txFifoStatus;
    strlcpy(_msg, msg, sizeof(_msg));
}

String RaftI2CCentral::DebugI2CISRElem::toStr()
{
    char outStr[300];
    snprintf(outStr, sizeof(outStr), "%lld %s INT (%08lx) %s MAIN (%08lx) %s FIFO (%08lx) %s", 
                _micros, _msg,
                _intStatus, debugINTFlagStr("", _intStatus).c_str(),
                _mainStatus, debugMainStatusStr("", _mainStatus).c_str(), 
                _txFifoStatus, debugFIFOStatusStr("", _txFifoStatus).c_str()
    );
    return outStr;
}

RaftI2CCentral::DebugI2CISR::DebugI2CISR()
{
    clear();
}

void RaftI2CCentral::DebugI2CISR::clear()
{
    _i2cISRDebugCount = 0;
}

void RaftI2CCentral::DebugI2CISR::debugISRAdd(const char* msg, uint32_t intStatus, 
                uint32_t mainStatus, uint32_t txFifoStatus)
{
    if (_i2cISRDebugCount >= DEBUG_RAFT_I2C_CENTRAL_ISR_MAX)
        return;
    _i2cISRDebugElems[_i2cISRDebugCount].set(msg, intStatus, mainStatus, txFifoStatus);
    _i2cISRDebugCount++;
}

uint32_t RaftI2CCentral::DebugI2CISR::getCount()
{
    return _i2cISRDebugCount;
}
RaftI2CCentral::DebugI2CISRElem& RaftI2CCentral::DebugI2CISR::getElem(uint32_t i)
{
    return _i2cISRDebugElems[i];
}

#endif