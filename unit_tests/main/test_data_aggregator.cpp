/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Data aggregator test
//
// Rob Dobson 2024
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "unity.h"
#include "unity_test_runner.h"

#include "PollDataAggregator.h"

// static const char* MODULE_PREFIX = "test_i2c_data_agg";

TEST_CASE("Test PollDataAggregator Initialization", "[PollDataAggregator]") 
{
    PollDataAggregator aggregator(10, 3);
    std::vector<uint8_t> data = {1, 2, 3};
    TEST_ASSERT_TRUE(aggregator.put(12345, data));
}

TEST_CASE("Test PollDataAggregator Put and Get", "[PollDataAggregator]") 
{
    PollDataAggregator aggregator(10, 4);
    std::vector<uint8_t> data = {1, 2, 3, 4};
    TEST_ASSERT_TRUE(aggregator.put(12345, data));
    std::vector<uint8_t> dataOut;
    TEST_ASSERT_TRUE(aggregator.get(dataOut));
    TEST_ASSERT_TRUE(data == dataOut);
}

TEST_CASE("Test PollDataAggregator Put and Get Wrap", "[PollDataAggregator]") 
{
    PollDataAggregator aggregator(3, 3);
    std::vector<uint8_t> data1 = {1, 2, 3};
    std::vector<uint8_t> data2 = {4, 5, 6};
    std::vector<uint8_t> data3 = {7, 8, 9};
    std::vector<uint8_t> data4 = {10, 11, 12};
    uint32_t timeVal = 12345;
    TEST_ASSERT_TRUE(aggregator.put(timeVal++, data1));
    TEST_ASSERT_TRUE(aggregator.put(timeVal++, data2));
    TEST_ASSERT_TRUE(aggregator.put(timeVal++, data3));
    std::vector<uint8_t> dataOut;
    TEST_ASSERT_TRUE(aggregator.get(dataOut));
    TEST_ASSERT_TRUE(data1 == dataOut);
    TEST_ASSERT_TRUE(aggregator.put(timeVal++, data4));
    TEST_ASSERT_TRUE(aggregator.get(dataOut));
    TEST_ASSERT_TRUE(data2 == dataOut);
    TEST_ASSERT_TRUE(aggregator.get(dataOut));
    TEST_ASSERT_TRUE(data3 == dataOut);
    TEST_ASSERT_TRUE(aggregator.get(dataOut));
    TEST_ASSERT_TRUE(data4 == dataOut);
}

TEST_CASE("Test PollDataAggregator Put and Get Empty", "[PollDataAggregator]") 
{
    PollDataAggregator aggregator(10, 3);
    std::vector<uint8_t> dataOut;
    TEST_ASSERT_FALSE(aggregator.get(dataOut));
}

TEST_CASE("Test PollDataAggregator Put and Get Full", "[PollDataAggregator]") 
{
    PollDataAggregator aggregator(3,3);
    std::vector<uint8_t> data1 = {1, 2, 3};
    std::vector<uint8_t> data2 = {4, 5, 6};
    std::vector<uint8_t> data3 = {7, 8, 9};
    std::vector<uint8_t> data4 = {10, 11, 12};
    uint32_t timeVal = 12345;
    TEST_ASSERT_TRUE(aggregator.put(timeVal++, data1));
    TEST_ASSERT_TRUE(aggregator.put(timeVal++, data2));
    TEST_ASSERT_TRUE(aggregator.put(timeVal++, data3));
    TEST_ASSERT_TRUE(aggregator.put(timeVal++, data4));
    TEST_ASSERT_TRUE(aggregator.count() == 3);
    std::vector<uint8_t> dataOut;
    TEST_ASSERT_TRUE(aggregator.get(dataOut));
    TEST_ASSERT_TRUE(data2 == dataOut);
    TEST_ASSERT_TRUE(aggregator.count() == 2);
    TEST_ASSERT_TRUE(aggregator.get(dataOut));
    TEST_ASSERT_TRUE(data3 == dataOut);
    TEST_ASSERT_TRUE(aggregator.count() == 1);
    TEST_ASSERT_TRUE(aggregator.get(dataOut));
    TEST_ASSERT_TRUE(data4 == dataOut);
    TEST_ASSERT_TRUE(aggregator.count() == 0);
}

TEST_CASE("Test PollDataAggregator Put and Get Full Wrap", "[PollDataAggregator]") 
{
    PollDataAggregator aggregator(3,3);
    std::vector<uint8_t> data1 = {1, 2, 3};
    std::vector<uint8_t> data2 = {4, 5, 6};
    std::vector<uint8_t> data3 = {7, 8, 9};
    std::vector<uint8_t> data4 = {10, 11, 12};
    std::vector<uint8_t> data5 = {13, 14, 15};
    std::vector<uint8_t> data6 = {19, 20, 21};
    uint32_t timeVal = 12345;
    TEST_ASSERT_TRUE(aggregator.put(timeVal++, data1));
    TEST_ASSERT_TRUE(aggregator.put(timeVal++, data2));
    TEST_ASSERT_TRUE(aggregator.put(timeVal++, data3));
    TEST_ASSERT_TRUE(aggregator.put(timeVal++, data4));
    TEST_ASSERT_TRUE(aggregator.put(timeVal++, data5));
    std::vector<uint8_t> dataOut;
    TEST_ASSERT_TRUE(aggregator.get(dataOut));
    TEST_ASSERT_TRUE(data3 == dataOut);
    TEST_ASSERT_TRUE(aggregator.put(timeVal++, data6));
    TEST_ASSERT_TRUE(aggregator.get(dataOut));
    TEST_ASSERT_TRUE(data4 == dataOut);
    TEST_ASSERT_TRUE(aggregator.get(dataOut));
    TEST_ASSERT_TRUE(data5 == dataOut);
    TEST_ASSERT_TRUE(aggregator.get(dataOut));
    TEST_ASSERT_TRUE(data6 == dataOut);
    TEST_ASSERT_FALSE(aggregator.get(dataOut));
}

TEST_CASE("Test PollDataAggregator Put and Get Multiple", "[PollDataAggregator]") 
{
    PollDataAggregator aggregator(4,4);
    std::vector<uint8_t> data1 = {1, 2, 3, 4};
    std::vector<uint8_t> data2 = {5, 6, 7, 8};
    std::vector<uint8_t> data3 = {9, 10, 11, 12};
    std::vector<uint8_t> data4 = {13, 14, 15, 16};
    std::vector<uint8_t> data5 = {17, 18, 19, 20};
    std::vector<uint8_t> data6 = {21, 22, 23, 24};
    std::vector<uint8_t> data7 = {25, 26, 27, 28};
    uint32_t timeVal = 12345;
    TEST_ASSERT_TRUE(aggregator.put(timeVal++, data1));
    TEST_ASSERT_TRUE(aggregator.put(timeVal++, data2));
    TEST_ASSERT_TRUE(aggregator.put(timeVal++, data3));
    TEST_ASSERT_TRUE(aggregator.put(timeVal++, data4));
    TEST_ASSERT_TRUE(aggregator.put(timeVal++, data5));
    TEST_ASSERT_TRUE(aggregator.put(timeVal++, data6));
    std::vector<uint8_t> dataOut;
    uint32_t elemSize = 0;
    std::vector<uint8_t> dataTest3And4 = data3;
    dataTest3And4.insert(dataTest3And4.end(), data4.begin(), data4.end());
    TEST_ASSERT_TRUE(aggregator.get(dataOut, elemSize, 2) == 2);
    TEST_ASSERT_TRUE(dataTest3And4 == dataOut);
    TEST_ASSERT_TRUE(elemSize == 4);
    TEST_ASSERT_TRUE(aggregator.put(timeVal++, data7));
    std::vector<uint8_t> dataTest5to7 = data5;
    dataTest5to7.insert(dataTest5to7.end(), data6.begin(), data6.end());
    dataTest5to7.insert(dataTest5to7.end(), data7.begin(), data7.end());
    TEST_ASSERT_TRUE(aggregator.get(dataOut, elemSize, 5) == 3);
    TEST_ASSERT_TRUE(dataTest5to7 == dataOut);
    TEST_ASSERT_FALSE(aggregator.get(dataOut));
}

TEST_CASE("Test PollDataAggregator Resize", "[PollDataAggregator]") 
{
    PollDataAggregator aggregator(3, 3);

    // Fill buffer
    std::vector<uint8_t> data1 = {1, 2, 3};
    std::vector<uint8_t> data2 = {4, 5, 6};
    std::vector<uint8_t> data3 = {7, 8, 9};
    uint32_t timeVal = 12345;
    TEST_ASSERT_TRUE(aggregator.put(timeVal++, data1));
    TEST_ASSERT_TRUE(aggregator.put(timeVal++, data2));
    TEST_ASSERT_TRUE(aggregator.put(timeVal++, data3));
    TEST_ASSERT_TRUE(aggregator.count() == 3);

    // Resize to larger — should clear existing data
    TEST_ASSERT_TRUE(aggregator.resize(5));
    TEST_ASSERT_TRUE(aggregator.count() == 0);

    // Verify we can now store 5 items
    std::vector<uint8_t> data4 = {10, 11, 12};
    std::vector<uint8_t> data5 = {13, 14, 15};
    std::vector<uint8_t> data6 = {16, 17, 18};
    std::vector<uint8_t> data7 = {19, 20, 21};
    std::vector<uint8_t> data8 = {22, 23, 24};
    TEST_ASSERT_TRUE(aggregator.put(timeVal++, data4));
    TEST_ASSERT_TRUE(aggregator.put(timeVal++, data5));
    TEST_ASSERT_TRUE(aggregator.put(timeVal++, data6));
    TEST_ASSERT_TRUE(aggregator.put(timeVal++, data7));
    TEST_ASSERT_TRUE(aggregator.put(timeVal++, data8));
    TEST_ASSERT_TRUE(aggregator.count() == 5);

    // Read back and verify FIFO order
    std::vector<uint8_t> dataOut;
    TEST_ASSERT_TRUE(aggregator.get(dataOut));
    TEST_ASSERT_TRUE(data4 == dataOut);
    TEST_ASSERT_TRUE(aggregator.get(dataOut));
    TEST_ASSERT_TRUE(data5 == dataOut);

    // Resize to smaller — should clear again
    TEST_ASSERT_TRUE(aggregator.resize(2));
    TEST_ASSERT_TRUE(aggregator.count() == 0);

    // Verify new capacity of 2
    TEST_ASSERT_TRUE(aggregator.put(timeVal++, data1));
    TEST_ASSERT_TRUE(aggregator.put(timeVal++, data2));
    TEST_ASSERT_TRUE(aggregator.put(timeVal++, data3));  // overwrites data1
    TEST_ASSERT_TRUE(aggregator.count() == 2);
    TEST_ASSERT_TRUE(aggregator.get(dataOut));
    TEST_ASSERT_TRUE(data2 == dataOut);
    TEST_ASSERT_TRUE(aggregator.get(dataOut));
    TEST_ASSERT_TRUE(data3 == dataOut);
    TEST_ASSERT_FALSE(aggregator.get(dataOut));
}

TEST_CASE("Test PollDataAggregator getLatestValue Basic", "[PollDataAggregator]")
{
    PollDataAggregator aggregator(5, 3);

    // Put a single value
    std::vector<uint8_t> data1 = {10, 20, 30};
    uint64_t putTime = 100000;
    TEST_ASSERT_TRUE(aggregator.put(putTime, data1));

    // getLatestValue should return it and report new
    uint64_t dataTimeUs = 0;
    std::vector<uint8_t> dataOut;
    TEST_ASSERT_TRUE(aggregator.getLatestValue(dataTimeUs, dataOut));
    TEST_ASSERT_TRUE(data1 == dataOut);
    TEST_ASSERT_TRUE(putTime == dataTimeUs);

    // Second call should return data but report not-new
    TEST_ASSERT_FALSE(aggregator.getLatestValue(dataTimeUs, dataOut));
    TEST_ASSERT_TRUE(data1 == dataOut);
    TEST_ASSERT_TRUE(putTime == dataTimeUs);
}

TEST_CASE("Test PollDataAggregator getLatestValue Empty", "[PollDataAggregator]")
{
    PollDataAggregator aggregator(5, 3);

    // getLatestValue on empty aggregator should return false
    uint64_t dataTimeUs = 0;
    std::vector<uint8_t> dataOut;
    TEST_ASSERT_FALSE(aggregator.getLatestValue(dataTimeUs, dataOut));
}

TEST_CASE("Test PollDataAggregator getLatestValue Multiple Puts", "[PollDataAggregator]")
{
    PollDataAggregator aggregator(5, 3);

    std::vector<uint8_t> data1 = {1, 2, 3};
    std::vector<uint8_t> data2 = {4, 5, 6};
    std::vector<uint8_t> data3 = {7, 8, 9};
    uint64_t time1 = 100000, time2 = 200000, time3 = 300000;

    TEST_ASSERT_TRUE(aggregator.put(time1, data1));
    TEST_ASSERT_TRUE(aggregator.put(time2, data2));
    TEST_ASSERT_TRUE(aggregator.put(time3, data3));

    // getLatestValue should return the most recent put
    uint64_t dataTimeUs = 0;
    std::vector<uint8_t> dataOut;
    TEST_ASSERT_TRUE(aggregator.getLatestValue(dataTimeUs, dataOut));
    TEST_ASSERT_TRUE(data3 == dataOut);
    TEST_ASSERT_TRUE(time3 == dataTimeUs);
}

TEST_CASE("Test PollDataAggregator getLatestValue Non-Destructive", "[PollDataAggregator]")
{
    PollDataAggregator aggregator(5, 3);

    std::vector<uint8_t> data1 = {1, 2, 3};
    std::vector<uint8_t> data2 = {4, 5, 6};
    uint64_t time1 = 100000, time2 = 200000;

    TEST_ASSERT_TRUE(aggregator.put(time1, data1));
    TEST_ASSERT_TRUE(aggregator.put(time2, data2));

    // getLatestValue should NOT drain the ring buffer
    uint64_t dataTimeUs = 0;
    std::vector<uint8_t> latestOut;
    TEST_ASSERT_TRUE(aggregator.getLatestValue(dataTimeUs, latestOut));
    TEST_ASSERT_TRUE(data2 == latestOut);

    // Ring buffer should still have both items available via get()
    std::vector<uint8_t> ringOut;
    TEST_ASSERT_TRUE(aggregator.get(ringOut));
    TEST_ASSERT_TRUE(data1 == ringOut);
    TEST_ASSERT_TRUE(aggregator.get(ringOut));
    TEST_ASSERT_TRUE(data2 == ringOut);
}

TEST_CASE("Test PollDataAggregator getLatestValue After Get Drain", "[PollDataAggregator]")
{
    PollDataAggregator aggregator(5, 3);

    std::vector<uint8_t> data1 = {1, 2, 3};
    uint64_t time1 = 100000;
    TEST_ASSERT_TRUE(aggregator.put(time1, data1));

    // Drain via get()
    std::vector<uint8_t> ringOut;
    TEST_ASSERT_TRUE(aggregator.get(ringOut));
    TEST_ASSERT_FALSE(aggregator.get(ringOut));

    // getLatestValue should still return the latest value (it's independent)
    uint64_t dataTimeUs = 0;
    std::vector<uint8_t> latestOut;
    // First call after put reported new; we haven't called getLatestValue yet
    TEST_ASSERT_TRUE(aggregator.getLatestValue(dataTimeUs, latestOut));
    TEST_ASSERT_TRUE(data1 == latestOut);
    TEST_ASSERT_TRUE(time1 == dataTimeUs);
}

TEST_CASE("Test PollDataAggregator getLatestValue Newness Resets On Put", "[PollDataAggregator]")
{
    PollDataAggregator aggregator(5, 3);

    std::vector<uint8_t> data1 = {1, 2, 3};
    std::vector<uint8_t> data2 = {4, 5, 6};
    uint64_t time1 = 100000, time2 = 200000;

    TEST_ASSERT_TRUE(aggregator.put(time1, data1));

    // Read latest — reports new
    uint64_t dataTimeUs = 0;
    std::vector<uint8_t> dataOut;
    TEST_ASSERT_TRUE(aggregator.getLatestValue(dataTimeUs, dataOut));

    // Read again — not new
    TEST_ASSERT_FALSE(aggregator.getLatestValue(dataTimeUs, dataOut));

    // Put new data — should reset newness
    TEST_ASSERT_TRUE(aggregator.put(time2, data2));

    // Read latest — new again
    TEST_ASSERT_TRUE(aggregator.getLatestValue(dataTimeUs, dataOut));
    TEST_ASSERT_TRUE(data2 == dataOut);
    TEST_ASSERT_TRUE(time2 == dataTimeUs);
}
