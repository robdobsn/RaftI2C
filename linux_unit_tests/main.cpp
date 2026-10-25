/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// RaftI2C host-side tests: decoding poll responses with the decoders generated from RaftCore's catalogue, against
// known values. And BusStatusMgr's delivery of poll results to data subscribers (test_bus_status.cpp)
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include <stdio.h>
#include <string.h>
#include "RaftUtils.h"
#include "DeviceTypeRecord.h"
#include "DevicePollingInfo.h"
#include "DeviceTypeRecords_generated.h"

#define TEST_ASSERT(cond, msg) if (!(cond)) { printf("TEST_ASSERT failed %s\n", msg); failCount++; }

const DeviceTypeRecord* getDevTypeRecord(const char* pDevTypeStr)
{
    // Iterate baseDevTypeRecords to find the record with the matching devType
    for (uint32_t i = 0; i < sizeof(baseDevTypeRecords) / sizeof(baseDevTypeRecords[0]); i++)
    {
        if (strcmp(baseDevTypeRecords[i].deviceType, pDevTypeStr) == 0)
            return &baseDevTypeRecords[i];
    }
    return nullptr;
}

bool isApprox(float a, float b, float tol = 0.0001f)
{
    return (a - b) < tol && (b - a) < tol;
}

int testBusStatus();
int testReIdentHookDevices();
int testHookDeclined();
int testGetIdentState();

int main()
{
    int failCount = 0;

    // Test VCNL4040 decode
    {
        // Device type
        const char* pDevTypeStr = "VCNL4040";
        RaftBusDeviceDecodeState decodeState;

        // Poll response
        uint8_t pollResp[50];
        Raft::setBEUInt16(pollResp, 0, 1234);
        Raft::setLEUInt16(pollResp, 2, 5678);
        Raft::setLEUInt16(pollResp, 4, 9012);
        Raft::setLEUInt16(pollResp, 6, 3456);

        // Get the device type record
        const DeviceTypeRecord* pDevTypeRecord = getDevTypeRecord(pDevTypeStr);
        if (pDevTypeRecord)
        {
            // Decode the poll response
            poll_VCNL4040 pollRespStruct[100];
            uint32_t recCount = pDevTypeRecord->pollResultDecodeFn(pollResp, 8, pollRespStruct, sizeof(pollRespStruct[0]), 1, decodeState);
            TEST_ASSERT(recCount == 1, "VCNL4040 decode failed");
            // The poll timestamp is in units of POLL_RESULT_RESOLUTION_US
            TEST_ASSERT(pollRespStruct[0].timeMs == 1234 * DevicePollingInfo::POLL_RESULT_RESOLUTION_US / 1000, "VCNL4040 timeMs decode failed");
            TEST_ASSERT(pollRespStruct[0].prox == 5678, "VCNL4040 prox decode failed");
            // printf("VCNL4040 als %f\n", pollRespStruct.als);
            TEST_ASSERT(isApprox(pollRespStruct[0].als, 9012.0/10), "VCNL4040 als decode failed");
            // printf("VCNL4040 white %f\n", pollRespStruct.white);
            TEST_ASSERT(pollRespStruct[0].white == 3456, "VCNL4040 white decode failed");
        }
        else
        {
            printf("%s device type record not found\n", pDevTypeStr);
            failCount++;
        }
    }

    // Test MAX30101 decode
    {
        // Device type
        const char* pDevTypeStr = "MAX30101";
        RaftBusDeviceDecodeState decodeState;

        // Poll response: FIFO_WR_PTR, OVF_COUNTER, FIFO_RD_PTR, 8 samples of 9 bytes (red, IR, green), then
        // FIFO_RD_PTR again - the samples popped are those between the two read pointers (here 5, wrapping past
        // the end of the 32-sample FIFO); the reads past them repeat the last sample, as the chip does
        uint32_t bufIdx = 0;
        uint8_t pollResp[100];
        uint8_t fifoWriteOffset = 0x02;
        uint8_t fifoReadOffset = 0x1d;
        uint16_t numReadingsInFIFO = 5;
        uint8_t fifoReadOffsetAfter = (fifoReadOffset + numReadingsInFIFO) % 32;
        bufIdx = Raft::setBytes(pollResp, bufIdx, 1234, 2, true);
        bufIdx = Raft::setBytes(pollResp, bufIdx, fifoWriteOffset, 1, true); // Write offset in FIFO
        bufIdx = Raft::setBytes(pollResp, bufIdx, 0x00, 1, true);
        bufIdx = Raft::setBytes(pollResp, bufIdx, fifoReadOffset, 1, true); // Read offset in FIFO
        for (uint32_t i = 0; i < 8; i++)
        {
            uint32_t j = i < numReadingsInFIFO ? i : numReadingsInFIFO - 1;
            bufIdx = Raft::setBytes(pollResp, bufIdx, 2345 + j*12, 3, true); // FIFO Red
            bufIdx = Raft::setBytes(pollResp, bufIdx, 5678 + j*9, 3, true); // FIFO IR data
            bufIdx = Raft::setBytes(pollResp, bufIdx, 345 + j*7, 3, true); // FIFO Green data
        }
        bufIdx = Raft::setBytes(pollResp, bufIdx, fifoReadOffsetAfter, 1, true); // Read offset after the read

        // Get the device type record
        const DeviceTypeRecord* pDevTypeRecord = getDevTypeRecord(pDevTypeStr);
        if (pDevTypeRecord)
        {
            // Decode the poll response
            poll_MAX30101 pollRespStruct[100];
            uint32_t recCount = pDevTypeRecord->pollResultDecodeFn(pollResp, bufIdx, pollRespStruct, sizeof(pollRespStruct[0]), 100, decodeState);
            TEST_ASSERT(recCount == numReadingsInFIFO, "MAX30101 decode failed recCount");
            for (uint32_t i = 0; i < recCount; i++)
            {
                // The poll timestamp, then the record's sample interval ("us": 40000) for each sample
                TEST_ASSERT(pollRespStruct[i].timeMs == 1234 * DevicePollingInfo::POLL_RESULT_RESOLUTION_US / 1000 + i*40, "MAX30101 timeMs decode failed");
                TEST_ASSERT(pollRespStruct[i].Red == 2345 + i*12, "MAX30101 Red decode failed");
                TEST_ASSERT(pollRespStruct[i].IR == 5678 + i*9, "MAX30101 IR decode failed");
                TEST_ASSERT(pollRespStruct[i].Green == 345 + i*7, "MAX30101 Green decode failed");
            }
        }
        else
        {
            printf("%s device type record not found\n", pDevTypeStr);
            failCount++;
        }
    }
    
    // BusStatusMgr
    failCount += testBusStatus();
    failCount += testReIdentHookDevices();
    failCount += testHookDeclined();
    failCount += testGetIdentState();

    // Check failCount
    if (failCount > 0)
        printf("RaftI2C linux_unit_tests FAILED %d tests\n", failCount);
    else
        printf("RaftI2C linux_unit_tests all tests passed\n");
    return failCount > 0 ? 1 : 0;
}
