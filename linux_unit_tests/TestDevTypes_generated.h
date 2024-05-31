#pragma once
#include <stdint.h>
using namespace Raft;

struct poll_VCNL4040 {
    uint32_t timeMs;
    uint16_t prox;
    float als;
    float white;
};

struct poll_VL6180 {
    uint32_t timeMs;
    bool valid;
    float dist;
};

struct poll_MAX30101 {
    uint32_t timeMs;
    uint32_t Red;
    uint32_t IR;
};

struct poll_ADXL313 {
    uint32_t timeMs;
    float x;
    float y;
    float z;
};

struct poll_AHT20 {
    uint32_t timeMs;
    uint8_t status;
    float humidity;
    float temperature;
};

struct poll_MCP9808 {
    uint32_t timeMs;
    float temperature;
};

struct poll_LPS25 {
    uint32_t timeMs;
    uint8_t status;
    float pressure;
    float temperature;
};

struct poll_CAP1203 {
    uint32_t timeMs;
    bool A;
    bool B;
    bool C;
    uint16_t status;
};

struct poll_Qwiic_Button {
    uint32_t timeMs;
    bool press;
};

struct poll_Qwiic_LED_Stick {
    uint32_t timeMs;
};

static BusI2CDevTypeRecord baseDevTypeRecords[] =
{
    {
        R"(VCNL4040)",
        R"(0x60)",
        R"(0x0c=0b100001100000XXXX)",
        R"(0x041007=&0x030e08=&0x000000=)",
        R"({"c":"0x08=r2&0x09=r2&0x0a=r2","i":200,"s":10})",
        6,
        R"({"name":"VCNL4040","desc":"Prox&ALS","manu":"Vishay","type":"VCNL4040","resp":{"b":6,"a":[{"n":"prox","t":"<H","u":"","r":[0,65535],"d":0,"f":"5d","o":"uint16"},{"n":"als","t":"<H","u":"lux","r":[0,65535],"d":10,"f":"5.2f","o":"float"},{"n":"white","t":"<H","u":"lux","r":[0,65535],"d":10,"f":"5.2f","o":"float"}]}})",
        [](const uint8_t* pBufIn, uint32_t bufLen, void* pStructOut, uint32_t structOutSize, 
                        uint16_t maxRecCount, RaftBusDeviceDecodeState& decodeState) -> uint32_t {
            struct poll_VCNL4040* pStruct = (struct poll_VCNL4040*) pStructOut;
            struct poll_VCNL4040* pOut = pStruct;
            const uint8_t* pBufEnd = pBufIn + bufLen;

            // Iterate through records
            uint32_t numRecs = 0;
            while (numRecs < maxRecCount) {

                // Calculate record start
                const uint8_t* pBuf = pBufIn + 8 * numRecs;
                if (pBuf + 8 > pBufEnd) break;

                // Extract timestamp
                uint64_t timestampUs = getBEUint16AndInc(pBuf, pBufEnd) * DevicePollingInfo::POLL_RESULT_RESOLUTION_US;
                if (timestampUs < decodeState.lastReportTimestampUs) {
                    decodeState.reportTimestampOffsetUs += DevicePollingInfo::POLL_RESULT_WRAP_VALUE * DevicePollingInfo::POLL_RESULT_RESOLUTION_US;
                }
                decodeState.lastReportTimestampUs = timestampUs;
                timestampUs += decodeState.reportTimestampOffsetUs;
                pOut->timeMs = timestampUs / 1000;

                // Extract attributes
                {
                    if (pBuf + sizeof(uint16_t) > pBufEnd) break;
                    int32_t __prox = getLEUint16AndInc(pBuf, pBufEnd);
                    pOut->prox = __prox;
                }
                {
                    if (pBuf + sizeof(uint16_t) > pBufEnd) break;
                    int32_t __als = getLEUint16AndInc(pBuf, pBufEnd);
                    pOut->als = __als;
                    pOut->als /= 10;
                }
                {
                    if (pBuf + sizeof(uint16_t) > pBufEnd) break;
                    int32_t __white = getLEUint16AndInc(pBuf, pBufEnd);
                    pOut->white = __white;
                    pOut->white /= 10;
                }

                // Complete loop
                pOut++;
                numRecs++;
            }
            return pOut - pStruct;
        }
    },
    {
        R"(VL6180)",
        R"(0x29)",
        R"(0x0000=0b10110100)",
        R"(0x020701&0x020801&0x009600&0x0097fd&0x00e301&0x00e403&0x00e502&0x00e601&0x00e703&0x00f502&0x00d905&0x00dbce&0x00dc03&0x00ddf8&0x009f00&0x00a33c&0x00b700&0x00bb3c&0x00b209&0x00ca09&0x019801&0x01b017&0x01ad00&0x00ff05&0x010005&0x019905&0x01a61b&0x01ac3e&0x01a71f&0x003000&0x001110&0x010a30&0x003f42&0x0031ff&0x004000&0x004163&0x002e01&0x001b09&0x003e31&0x001424&0x003801)",
        R"({"c":"0x004f=r1&0x0062=r1&0x001507&0x001801","i":200,"s":10})",
        2,
        R"({"name":"VL6180","desc":"ToF","manu":"ST","type":"VL6180","resp":{"b":2,"a":[{"n":"valid","t":"B","u":"","r":[0,1],"m":"0x04","s":2,"f":"b","o":"bool","vs":false},{"n":"dist","t":"B","u":"mm","r":[0,255],"f":"3d","o":"float","vft":"valid"}]}})",
        [](const uint8_t* pBufIn, uint32_t bufLen, void* pStructOut, uint32_t structOutSize, 
                        uint16_t maxRecCount, RaftBusDeviceDecodeState& decodeState) -> uint32_t {
            struct poll_VL6180* pStruct = (struct poll_VL6180*) pStructOut;
            struct poll_VL6180* pOut = pStruct;
            const uint8_t* pBufEnd = pBufIn + bufLen;

            // Iterate through records
            uint32_t numRecs = 0;
            while (numRecs < maxRecCount) {

                // Calculate record start
                const uint8_t* pBuf = pBufIn + 4 * numRecs;
                if (pBuf + 4 > pBufEnd) break;

                // Extract timestamp
                uint64_t timestampUs = getBEUint16AndInc(pBuf, pBufEnd) * DevicePollingInfo::POLL_RESULT_RESOLUTION_US;
                if (timestampUs < decodeState.lastReportTimestampUs) {
                    decodeState.reportTimestampOffsetUs += DevicePollingInfo::POLL_RESULT_WRAP_VALUE * DevicePollingInfo::POLL_RESULT_RESOLUTION_US;
                }
                decodeState.lastReportTimestampUs = timestampUs;
                timestampUs += decodeState.reportTimestampOffsetUs;
                pOut->timeMs = timestampUs / 1000;

                // Extract attributes
                {
                    if (pBuf + sizeof(uint8_t) > pBufEnd) break;
                    int16_t __valid = getInt8AndInc(pBuf, pBufEnd);
                    __valid &= 0x4;
                    __valid <<= 2;
                    pOut->valid = __valid;
                }
                {
                    if (pBuf + sizeof(uint8_t) > pBufEnd) break;
                    int16_t __dist = getInt8AndInc(pBuf, pBufEnd);
                    pOut->dist = __dist;
                }

                // Complete loop
                pOut++;
                numRecs++;
            }
            return pOut - pStruct;
        }
    },
    {
        R"(MAX30101)",
        R"(0x57)",
        R"(0xff=0x15)",
        R"(0x0940&0x00=r1&0x085f&0x0903&0x0a27&0x0c4040&0x11=0x21)",
        R"({"c":"0x04=r51","i":200,"s":5})",
        51,
        R"({"name":"MAX30101","desc":"Prox&ALS","manu":"Vishay","type":"MAX30101","resp":{"b":51,"a":[{"n":"Red","t":">I","u":"","r":[0,16777215],"f":"6d","o":"uint32"},{"n":"IR","t":">I","u":"","r":[0,16777215],"f":"6d","o":"uint32"}],"c":{"n":"max30101_fifo","c":"int N=(buf[0]+32-buf[2])%32;int k=3;int i=0;while(i<N){out.Red=(buf[k]<<16)|(buf[k+1]<<8)|buf[k+2];out.IR=(buf[k+3]<<16)|(buf[k+4]<<8)|buf[k+5];k+=6;i++;next;}"},"us":40000}})",
        [](const uint8_t* pBufIn, uint32_t bufLen, void* pStructOut, uint32_t structOutSize, 
                        uint16_t maxRecCount, RaftBusDeviceDecodeState& decodeState) -> uint32_t {
            struct poll_MAX30101* pStruct = (struct poll_MAX30101*) pStructOut;
            struct poll_MAX30101* pOut = pStruct;
            const uint8_t* pBufEnd = pBufIn + bufLen;

            // Iterate through records
            uint32_t pollRecIdx = 0;
            while (pOut < pStruct + maxRecCount) {

                // Calculate record start and check size
                const uint8_t* pBuf = pBufIn + 53 * pollRecIdx;
                if (pBuf + 53 > pBufEnd) break;

                // Extract timestamp
                uint64_t timestampUs = getBEUint16AndInc(pBuf, pBufEnd) * DevicePollingInfo::POLL_RESULT_RESOLUTION_US;
                if (timestampUs < decodeState.lastReportTimestampUs) {
                    decodeState.reportTimestampOffsetUs += DevicePollingInfo::POLL_RESULT_WRAP_VALUE * DevicePollingInfo::POLL_RESULT_RESOLUTION_US;
                }
                decodeState.lastReportTimestampUs = timestampUs;
                timestampUs += decodeState.reportTimestampOffsetUs;
                pOut->timeMs = timestampUs / 1000;

                // Custom function
                const uint8_t* buf = pBuf;
                int N=(buf[0]+32-buf[2])%32;
                int k=3;
                int i=0;
                while (i<N) {
                    pOut->Red=(buf[k]<<16)|(buf[k+1]<<8)|buf[k+2];
                    pOut->IR=(buf[k+3]<<16)|(buf[k+4]<<8)|buf[k+5];
                    k+=6;
                    i++;
                    if (++pOut >= pStruct + maxRecCount) break;
                    timestampUs += 40000;
                    pOut->timeMs = timestampUs / 1000;;
                }
                
                // Complete loop
                pollRecIdx++;
            }
            return pOut - pStruct;
        }
    },
    {
        R"(ADXL313)",
        R"(0x1d,0x53)",
        R"(0x00=0b1010110100011101)",
        R"(0x2d0c=;0x310a=)",
        R"({"c":"0x32=r6","i":100,"s":10})",
        6,
        R"({"name":"ADXL313","desc":"3-Axis Accel","manu":"Analog Devices","type":"ADXL313","resp":{"b":6,"a":[{"n":"x","t":"<h","u":"g","r":[-4.0,4.0],"d":1024,"f":".2f","o":"float"},{"n":"y","t":"<h","u":"g","r":[-4.0,4.0],"d":1024,"f":".2f","o":"float"},{"n":"z","t":"<h","u":"g","r":[-4.0,4.0],"d":1024,"f":".2f","o":"float"}]}})",
        [](const uint8_t* pBufIn, uint32_t bufLen, void* pStructOut, uint32_t structOutSize, 
                        uint16_t maxRecCount, RaftBusDeviceDecodeState& decodeState) -> uint32_t {
            struct poll_ADXL313* pStruct = (struct poll_ADXL313*) pStructOut;
            struct poll_ADXL313* pOut = pStruct;
            const uint8_t* pBufEnd = pBufIn + bufLen;

            // Iterate through records
            uint32_t numRecs = 0;
            while (numRecs < maxRecCount) {

                // Calculate record start
                const uint8_t* pBuf = pBufIn + 8 * numRecs;
                if (pBuf + 8 > pBufEnd) break;

                // Extract timestamp
                uint64_t timestampUs = getBEUint16AndInc(pBuf, pBufEnd) * DevicePollingInfo::POLL_RESULT_RESOLUTION_US;
                if (timestampUs < decodeState.lastReportTimestampUs) {
                    decodeState.reportTimestampOffsetUs += DevicePollingInfo::POLL_RESULT_WRAP_VALUE * DevicePollingInfo::POLL_RESULT_RESOLUTION_US;
                }
                decodeState.lastReportTimestampUs = timestampUs;
                timestampUs += decodeState.reportTimestampOffsetUs;
                pOut->timeMs = timestampUs / 1000;

                // Extract attributes
                {
                    if (pBuf + sizeof(int16_t) > pBufEnd) break;
                    int32_t __x = getLEInt16AndInc(pBuf, pBufEnd);
                    pOut->x = __x;
                    pOut->x /= 1024;
                }
                {
                    if (pBuf + sizeof(int16_t) > pBufEnd) break;
                    int32_t __y = getLEInt16AndInc(pBuf, pBufEnd);
                    pOut->y = __y;
                    pOut->y /= 1024;
                }
                {
                    if (pBuf + sizeof(int16_t) > pBufEnd) break;
                    int32_t __z = getLEInt16AndInc(pBuf, pBufEnd);
                    pOut->z = __z;
                    pOut->z /= 1024;
                }

                // Complete loop
                pOut++;
                numRecs++;
            }
            return pOut - pStruct;
        }
    },
    {
        R"(AHT20)",
        R"(0x38)",
        R"()",
        R"(0xbe0800=)",
        R"({"c":"=r6&0xac3300=","i":5000,"s":2})",
        6,
        R"({"name":"AHT20","desc":"Temp&Humid","manu":"Asair","type":"AHT20","resp":{"b":6,"a":[{"n":"status","t":"B","u":"","f":"02x","o":"uint8","vs":false},{"n":"humidity","t":">I","u":"%","r":[0,100],"m":"0xfffff000","s":12,"d":10485.76,"f":"3.1f","o":"float"},{"n":"temperature","at":2,"t":">I","u":"&deg;C","r":[-40,80],"m":"0x000fffff","d":5242.88,"a":-50,"f":"3.2f","o":"float"}]}})",
        [](const uint8_t* pBufIn, uint32_t bufLen, void* pStructOut, uint32_t structOutSize, 
                        uint16_t maxRecCount, RaftBusDeviceDecodeState& decodeState) -> uint32_t {
            struct poll_AHT20* pStruct = (struct poll_AHT20*) pStructOut;
            struct poll_AHT20* pOut = pStruct;
            const uint8_t* pBufEnd = pBufIn + bufLen;

            // Iterate through records
            uint32_t numRecs = 0;
            while (numRecs < maxRecCount) {

                // Calculate record start
                const uint8_t* pBuf = pBufIn + 8 * numRecs;
                if (pBuf + 8 > pBufEnd) break;

                // Extract timestamp
                uint64_t timestampUs = getBEUint16AndInc(pBuf, pBufEnd) * DevicePollingInfo::POLL_RESULT_RESOLUTION_US;
                if (timestampUs < decodeState.lastReportTimestampUs) {
                    decodeState.reportTimestampOffsetUs += DevicePollingInfo::POLL_RESULT_WRAP_VALUE * DevicePollingInfo::POLL_RESULT_RESOLUTION_US;
                }
                decodeState.lastReportTimestampUs = timestampUs;
                timestampUs += decodeState.reportTimestampOffsetUs;
                pOut->timeMs = timestampUs / 1000;
                const uint8_t* pAttrStart = pBuf;

                // Extract attributes
                {
                    if (pBuf + sizeof(uint8_t) > pBufEnd) break;
                    int16_t __status = getInt8AndInc(pBuf, pBufEnd);
                    pOut->status = __status;
                }
                {
                    if (pBuf + sizeof(uint32_t) > pBufEnd) break;
                    int64_t __humidity = getBEUint32AndInc(pBuf, pBufEnd);
                    __humidity &= 0xfffff000;
                    __humidity <<= 12;
                    pOut->humidity = __humidity;
                    pOut->humidity /= 10485.76;
                }
                {
                    const uint8_t* pAbsPos = pAttrStart + 2;
                    if (pAbsPos + sizeof(uint32_t) > pBufEnd) break;
                    int64_t __temperature = getBEUint32AndInc(pAbsPos, pBufEnd);
                    __temperature &= 0xfffff;
                    pOut->temperature = __temperature;
                    pOut->temperature /= 5242.88;
                    pOut->temperature += -50;
                }

                // Complete loop
                pOut++;
                numRecs++;
            }
            return pOut - pStruct;
        }
    },
    {
        R"(MCP9808)",
        R"(0x18-0x1f)",
        R"(0x06=0b0000000001010100&0x07=0b00000100XXXXXXXX)",
        R"(0x010000)",
        R"({"c":"0x05=r2","i":5000,"s":2})",
        2,
        R"({"name":"MCP9808","desc":"Temp","manu":"Microchip","type":"MCP9808","resp":{"b":2,"a":[{"n":"temperature","t":">H","u":"&deg;C","r":[-40,125],"m":"0x1fff","d":16,"sb":12,"ss":4096,"f":"3.2f","o":"float"}]}})",
        [](const uint8_t* pBufIn, uint32_t bufLen, void* pStructOut, uint32_t structOutSize, 
                        uint16_t maxRecCount, RaftBusDeviceDecodeState& decodeState) -> uint32_t {
            struct poll_MCP9808* pStruct = (struct poll_MCP9808*) pStructOut;
            struct poll_MCP9808* pOut = pStruct;
            const uint8_t* pBufEnd = pBufIn + bufLen;

            // Iterate through records
            uint32_t numRecs = 0;
            while (numRecs < maxRecCount) {

                // Calculate record start
                const uint8_t* pBuf = pBufIn + 4 * numRecs;
                if (pBuf + 4 > pBufEnd) break;

                // Extract timestamp
                uint64_t timestampUs = getBEUint16AndInc(pBuf, pBufEnd) * DevicePollingInfo::POLL_RESULT_RESOLUTION_US;
                if (timestampUs < decodeState.lastReportTimestampUs) {
                    decodeState.reportTimestampOffsetUs += DevicePollingInfo::POLL_RESULT_WRAP_VALUE * DevicePollingInfo::POLL_RESULT_RESOLUTION_US;
                }
                decodeState.lastReportTimestampUs = timestampUs;
                timestampUs += decodeState.reportTimestampOffsetUs;
                pOut->timeMs = timestampUs / 1000;

                // Extract attributes
                {
                    if (pBuf + sizeof(uint16_t) > pBufEnd) break;
                    int32_t __temperature = getBEUint16AndInc(pBuf, pBufEnd);
                    __temperature &= 0x1fff;
                    if (__temperature & 0x1000) __temperature = 0x1000 - __temperature;
                    pOut->temperature = __temperature;
                    pOut->temperature /= 16;
                }

                // Complete loop
                pOut++;
                numRecs++;
            }
            return pOut - pStruct;
        }
    },
    {
        R"(LPS25)",
        R"(0x5D)",
        R"(0x0f=0b10111101)",
        R"(0x2104=&0x20C0=)",
        R"({"c":"0xA7=r6","i":1000,"s":2})",
        6,
        R"({"name":"LPS25","desc":"Pressure","manu":"ST","type":"LPS25","resp":{"b":6,"a":[{"n":"status","t":"B","r":[0,255],"m":"0xff","f":"02x","o":"uint8","vs":false},{"n":"pressure","at":0,"t":"<I","u":"hPa","r":[260,1260],"s":8,"d":4096,"f":"4.2f","o":"float"},{"n":"temperature","at":4,"t":"<h","u":"&deg;C","r":[-30,105],"d":480,"a":42.5,"f":"3.2f","o":"float"}]}})",
        [](const uint8_t* pBufIn, uint32_t bufLen, void* pStructOut, uint32_t structOutSize, 
                        uint16_t maxRecCount, RaftBusDeviceDecodeState& decodeState) -> uint32_t {
            struct poll_LPS25* pStruct = (struct poll_LPS25*) pStructOut;
            struct poll_LPS25* pOut = pStruct;
            const uint8_t* pBufEnd = pBufIn + bufLen;

            // Iterate through records
            uint32_t numRecs = 0;
            while (numRecs < maxRecCount) {

                // Calculate record start
                const uint8_t* pBuf = pBufIn + 8 * numRecs;
                if (pBuf + 8 > pBufEnd) break;

                // Extract timestamp
                uint64_t timestampUs = getBEUint16AndInc(pBuf, pBufEnd) * DevicePollingInfo::POLL_RESULT_RESOLUTION_US;
                if (timestampUs < decodeState.lastReportTimestampUs) {
                    decodeState.reportTimestampOffsetUs += DevicePollingInfo::POLL_RESULT_WRAP_VALUE * DevicePollingInfo::POLL_RESULT_RESOLUTION_US;
                }
                decodeState.lastReportTimestampUs = timestampUs;
                timestampUs += decodeState.reportTimestampOffsetUs;
                pOut->timeMs = timestampUs / 1000;
                const uint8_t* pAttrStart = pBuf;

                // Extract attributes
                {
                    if (pBuf + sizeof(uint8_t) > pBufEnd) break;
                    int16_t __status = getInt8AndInc(pBuf, pBufEnd);
                    __status &= 0xff;
                    pOut->status = __status;
                }
                {
                    const uint8_t* pAbsPos = pAttrStart + 0;
                    if (pAbsPos + sizeof(uint32_t) > pBufEnd) break;
                    int64_t __pressure = getLEUint32AndInc(pAbsPos, pBufEnd);
                    __pressure <<= 8;
                    pOut->pressure = __pressure;
                    pOut->pressure /= 4096;
                }
                {
                    const uint8_t* pAbsPos = pAttrStart + 4;
                    if (pAbsPos + sizeof(int16_t) > pBufEnd) break;
                    int32_t __temperature = getLEInt16AndInc(pAbsPos, pBufEnd);
                    pOut->temperature = __temperature;
                    pOut->temperature /= 480;
                    pOut->temperature += 42.5;
                }

                // Complete loop
                pOut++;
                numRecs++;
            }
            return pOut - pStruct;
        }
    },
    {
        R"(CAP1203)",
        R"(0x28)",
        R"(0xfd=0b01101101)",
        R"(0x0000&0x1f06)",
        R"({"c":"0x02=r2&0x0000","i":100,"s":10})",
        2,
        R"({"name":"CAP1203","desc":"Capacitive Touch x 3","manu":"Sparkfun","type":"CAP1203","resp":{"b":2,"a":[{"n":"A","at":1,"t":"B","r":[0,1],"m":"0x01","s":0,"f":"b","o":"bool"},{"n":"B","at":1,"t":"B","r":[0,1],"m":"0x02","s":1,"f":"b","o":"bool"},{"n":"C","at":1,"t":"B","r":[0,1],"m":"0x04","s":2,"f":"b","o":"bool"},{"n":"status","t":">H","vs":false,"f":"04x","o":"uint16"}]}})",
        [](const uint8_t* pBufIn, uint32_t bufLen, void* pStructOut, uint32_t structOutSize, 
                        uint16_t maxRecCount, RaftBusDeviceDecodeState& decodeState) -> uint32_t {
            struct poll_CAP1203* pStruct = (struct poll_CAP1203*) pStructOut;
            struct poll_CAP1203* pOut = pStruct;
            const uint8_t* pBufEnd = pBufIn + bufLen;

            // Iterate through records
            uint32_t numRecs = 0;
            while (numRecs < maxRecCount) {

                // Calculate record start
                const uint8_t* pBuf = pBufIn + 4 * numRecs;
                if (pBuf + 4 > pBufEnd) break;

                // Extract timestamp
                uint64_t timestampUs = getBEUint16AndInc(pBuf, pBufEnd) * DevicePollingInfo::POLL_RESULT_RESOLUTION_US;
                if (timestampUs < decodeState.lastReportTimestampUs) {
                    decodeState.reportTimestampOffsetUs += DevicePollingInfo::POLL_RESULT_WRAP_VALUE * DevicePollingInfo::POLL_RESULT_RESOLUTION_US;
                }
                decodeState.lastReportTimestampUs = timestampUs;
                timestampUs += decodeState.reportTimestampOffsetUs;
                pOut->timeMs = timestampUs / 1000;
                const uint8_t* pAttrStart = pBuf;

                // Extract attributes
                {
                    const uint8_t* pAbsPos = pAttrStart + 1;
                    if (pAbsPos + sizeof(uint8_t) > pBufEnd) break;
                    int16_t __A = getInt8AndInc(pAbsPos, pBufEnd);
                    __A &= 0x1;
                    pOut->A = __A;
                }
                {
                    const uint8_t* pAbsPos = pAttrStart + 1;
                    if (pAbsPos + sizeof(uint8_t) > pBufEnd) break;
                    int16_t __B = getInt8AndInc(pAbsPos, pBufEnd);
                    __B &= 0x2;
                    __B <<= 1;
                    pOut->B = __B;
                }
                {
                    const uint8_t* pAbsPos = pAttrStart + 1;
                    if (pAbsPos + sizeof(uint8_t) > pBufEnd) break;
                    int16_t __C = getInt8AndInc(pAbsPos, pBufEnd);
                    __C &= 0x4;
                    __C <<= 2;
                    pOut->C = __C;
                }
                {
                    if (pBuf + sizeof(uint16_t) > pBufEnd) break;
                    int32_t __status = getBEUint16AndInc(pBuf, pBufEnd);
                    pOut->status = __status;
                }

                // Complete loop
                pOut++;
                numRecs++;
            }
            return pOut - pStruct;
        }
    },
    {
        R"(QwiicButton)",
        R"(0x6f)",
        R"(0x00=0b01011101)",
        R"()",
        R"({"c":"0x03=r1","i":100,"s":10})",
        1,
        R"({"name":"Qwiic Button","desc":"Button","manu":"Sparkfun","type":"QwiicButton","resp":{"b":1,"a":[{"n":"press","t":"B","u":"","r":[0,1],"m":"0x04","s":2,"f":"b","o":"bool"}]},"actions":[{"n":"brightness","t":"B","w":"19","r":[0,255]},{"n":"granularity","t":"B","w":"1a","r":[1,255]},{"n":"cycle time","t":"<H","w":"1b","r":[0,65535]},{"n":"off time","t":"<H","w":"1d","r":[0,65535]}]})",
        [](const uint8_t* pBufIn, uint32_t bufLen, void* pStructOut, uint32_t structOutSize, 
                        uint16_t maxRecCount, RaftBusDeviceDecodeState& decodeState) -> uint32_t {
            struct poll_Qwiic_Button* pStruct = (struct poll_Qwiic_Button*) pStructOut;
            struct poll_Qwiic_Button* pOut = pStruct;
            const uint8_t* pBufEnd = pBufIn + bufLen;

            // Iterate through records
            uint32_t numRecs = 0;
            while (numRecs < maxRecCount) {

                // Calculate record start
                const uint8_t* pBuf = pBufIn + 3 * numRecs;
                if (pBuf + 3 > pBufEnd) break;

                // Extract timestamp
                uint64_t timestampUs = getBEUint16AndInc(pBuf, pBufEnd) * DevicePollingInfo::POLL_RESULT_RESOLUTION_US;
                if (timestampUs < decodeState.lastReportTimestampUs) {
                    decodeState.reportTimestampOffsetUs += DevicePollingInfo::POLL_RESULT_WRAP_VALUE * DevicePollingInfo::POLL_RESULT_RESOLUTION_US;
                }
                decodeState.lastReportTimestampUs = timestampUs;
                timestampUs += decodeState.reportTimestampOffsetUs;
                pOut->timeMs = timestampUs / 1000;

                // Extract attributes
                {
                    if (pBuf + sizeof(uint8_t) > pBufEnd) break;
                    int16_t __press = getInt8AndInc(pBuf, pBufEnd);
                    __press &= 0x4;
                    __press <<= 2;
                    pOut->press = __press;
                }

                // Complete loop
                pOut++;
                numRecs++;
            }
            return pOut - pStruct;
        }
    },
    {
        R"(QwiicLEDStick)",
        R"(0x23)",
        R"()",
        R"()",
        R"({})",
        0,
        R"({"name":"Qwiic LED Stick","desc":"LEDs","manu":"Sparkfun","type":"QwiicLEDStick","actions":[{"n":"pixels","t":"BBBB","w":"71","f":"LEDPIX","NX":10,"NY":1,"concat":false,"r":[0,255]},{"n":"brightness","t":"B","w":"76","r":[0,255],"d":50},{"n":"off","w":"78"}]})",
        [](const uint8_t* pBufIn, uint32_t bufLen, void* pStructOut, uint32_t structOutSize, 
                        uint16_t maxRecCount, RaftBusDeviceDecodeState& decodeState) -> uint32_t {
            return 0;
        }
    },
};

static const uint32_t BASE_DEV_INDEX_BY_ARRAY_MIN_ADDR = 0;
static const uint32_t BASE_DEV_INDEX_BY_ARRAY_MAX_ADDR = 0x77;

static const uint8_t baseDevTypeCountByAddr[] =
{
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,2,1,1,0,0,0,1,0,0,0,0,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,1,0,0,0,0,0,1,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
};

static uint16_t baseDevTypeIndexByAddr_0x18[] = {5};
static uint16_t baseDevTypeIndexByAddr_0x19[] = {5};
static uint16_t baseDevTypeIndexByAddr_0x1a[] = {5};
static uint16_t baseDevTypeIndexByAddr_0x1b[] = {5};
static uint16_t baseDevTypeIndexByAddr_0x1c[] = {5};
static uint16_t baseDevTypeIndexByAddr_0x1d[] = {3, 5};
static uint16_t baseDevTypeIndexByAddr_0x1e[] = {5};
static uint16_t baseDevTypeIndexByAddr_0x1f[] = {5};
static uint16_t baseDevTypeIndexByAddr_0x23[] = {9};
static uint16_t baseDevTypeIndexByAddr_0x28[] = {7};
static uint16_t baseDevTypeIndexByAddr_0x29[] = {1};
static uint16_t baseDevTypeIndexByAddr_0x38[] = {4};
static uint16_t baseDevTypeIndexByAddr_0x53[] = {3};
static uint16_t baseDevTypeIndexByAddr_0x57[] = {2};
static uint16_t baseDevTypeIndexByAddr_0x5d[] = {6};
static uint16_t baseDevTypeIndexByAddr_0x60[] = {0};
static uint16_t baseDevTypeIndexByAddr_0x6f[] = {8};

static uint16_t* baseDevTypeIndexByAddr[] =
{
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    baseDevTypeIndexByAddr_0x18,
    baseDevTypeIndexByAddr_0x19,
    baseDevTypeIndexByAddr_0x1a,
    baseDevTypeIndexByAddr_0x1b,
    baseDevTypeIndexByAddr_0x1c,
    baseDevTypeIndexByAddr_0x1d,
    baseDevTypeIndexByAddr_0x1e,
    baseDevTypeIndexByAddr_0x1f,
    nullptr,
    nullptr,
    nullptr,
    baseDevTypeIndexByAddr_0x23,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    baseDevTypeIndexByAddr_0x28,
    baseDevTypeIndexByAddr_0x29,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    baseDevTypeIndexByAddr_0x38,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    baseDevTypeIndexByAddr_0x53,
    nullptr,
    nullptr,
    nullptr,
    baseDevTypeIndexByAddr_0x57,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    baseDevTypeIndexByAddr_0x5d,
    nullptr,
    nullptr,
    baseDevTypeIndexByAddr_0x60,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    baseDevTypeIndexByAddr_0x6f,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
};


static const uint8_t scanPriority0[] =
{
    0x18,0x1d,0x23,0x28,0x29,0x38,0x57,0x5d,0x60,0x6f,
};


static const uint8_t scanPriority1[] =
{
    0x19,0x1a,0x1b,0x1c,0x1e,0x1f,0x53,
};


static const uint8_t scanPriority2[] =
{
    0x04,0x05,0x06,0x07,0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f,0x10,0x11,0x12,0x13,0x14,0x15,0x16,0x17,0x20,0x21,0x22,0x24,0x25,0x26,0x27,0x2a,0x2b,0x2c,0x2d,0x2e,0x2f,0x30,0x31,0x32,0x33,0x34,0x35,0x36,0x37,0x39,0x3a,0x3b,0x3c,0x3d,0x3e,0x3f,0x40,0x41,0x42,0x43,0x44,0x45,0x46,0x47,0x48,0x49,0x4a,0x4b,0x4c,0x4d,0x4e,0x4f,0x50,0x51,0x52,0x54,0x55,0x56,0x58,0x59,0x5a,0x5b,0x5c,0x5e,0x5f,0x61,0x62,0x63,0x64,0x65,0x66,0x67,0x68,0x69,0x6a,0x6b,0x6c,0x6d,0x6e,0x70,0x71,0x72,0x73,0x74,0x75,0x76,0x77,
};

static const uint8_t* scanPriorityLists[] =
{
    scanPriority0,
    scanPriority1,
    scanPriority2,
};

static const uint8_t scanPriorityListLengths[] =
{
    10,
    7,
    99,
};

static const uint8_t numScanPriorityLists = 3;
