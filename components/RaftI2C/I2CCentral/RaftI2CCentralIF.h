/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// RaftI2CCentralIF
// I2C Interface
//
// Rob Dobson 2021
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "RaftCore.h"
#include "esp_attr.h"

class RaftI2CCentralIF
{
public:
    RaftI2CCentralIF()
    {
    }

    virtual ~RaftI2CCentralIF()
    {
    }

    // Init/de-init
    virtual bool init(uint8_t i2cPort, uint16_t pinSDA, uint16_t pinSCL, uint32_t busFrequency, 
                uint32_t busFilteringLevel = DEFAULT_BUS_FILTER_LEVEL) = 0;

    virtual void deinit() = 0;

    // Busy
    virtual bool isBusy() = 0;

    // Access the bus
    virtual RaftRetCode access(uint32_t address, const uint8_t* pWriteBuf, uint32_t numToWrite,
                    uint8_t* pReadBuf, uint32_t numToRead, uint32_t& numRead) = 0;

    // Check if bus operating ok
    virtual bool isOperatingOk() const = 0;

    // Debugging
    class I2CStats
    {
    public:
        I2CStats()
        {
            clear();
        }
        void FUNCTION_DECORATOR_IRAM_ATTR clear()
        {
            isrCount = 0;
            startCount = 0;
            nackCount = 0;
            engineTimeOutCount = 0;
            softwareTimeOutCount = 0;
            transCompleteCount = 0;
            masterTransCompleteCount = 0;
            arbitrationLostCount = 0;
            txFifoEmptyCount = 0;
            incompleteTransaction = 0;
        }
        void FUNCTION_DECORATOR_IRAM_ATTR update(bool transStart,
            bool ackErr,
            bool timeOut,
            bool transComplete,
            bool arbLost,
            bool masterTranComp,
            bool txFifoEmpty)
        {
            isrCount++;
            if (transStart)
                startCount++;
            if (ackErr)
                nackCount++;
            if (timeOut)
                engineTimeOutCount++;
            if (transComplete)
                transCompleteCount++;
            if (arbLost)
                arbitrationLostCount++;
            if (masterTranComp)
                masterTransCompleteCount++;
            if (txFifoEmpty)
                txFifoEmptyCount++;
        }
        void FUNCTION_DECORATOR_IRAM_ATTR recordSoftwareTimeout()
        {
            softwareTimeOutCount++;
        }
        void FUNCTION_DECORATOR_IRAM_ATTR recordIncompleteTransaction()
        {
            incompleteTransaction++;
        }
        String debugStr()
        {
            char outStr[200];
            snprintf(outStr, sizeof(outStr), "ISRs %lu Starts %lu NAKs %lu EngTimO %lu TransComps %lu ArbLost %lu MastTransComp %lu SwTimO %lu TxFIFOmt %lu incomplete %lu", 
                            (unsigned long)isrCount, (unsigned long)startCount, (unsigned long)nackCount, (unsigned long)engineTimeOutCount, (unsigned long)transCompleteCount,
                            (unsigned long)arbitrationLostCount,  (unsigned long)masterTransCompleteCount, (unsigned long)softwareTimeOutCount, 
                            (unsigned long)txFifoEmptyCount, (unsigned long)incompleteTransaction);
            return outStr;
        }
        uint32_t isrCount;
        uint32_t startCount;
        uint32_t nackCount;
        uint32_t engineTimeOutCount;
        uint32_t transCompleteCount;
        uint32_t arbitrationLostCount;
        uint32_t softwareTimeOutCount;
        uint32_t masterTransCompleteCount;
        uint32_t txFifoEmptyCount;
        uint32_t incompleteTransaction;
    };

    // Get stats
    I2CStats getStats()
    {
        return _i2cStats;
    }

    // Consts
    static const uint32_t DEFAULT_BUS_FILTER_LEVEL = 7;
    
protected:
    // I2C stats
    I2CStats _i2cStats;

};
