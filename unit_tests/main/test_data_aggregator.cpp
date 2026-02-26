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
    PollDataAggregator aggregator;
    aggregator.init(10, 3);
    std::vector<uint8_t> data = {1, 2, 3};
    TEST_ASSERT_TRUE(aggregator.put(12345, data));
}

TEST_CASE("Test PollDataAggregator Put and Get", "[PollDataAggregator]") 
{
    PollDataAggregator aggregator;
    aggregator.init(10, 4);
    std::vector<uint8_t> data = {1, 2, 3, 4};
    TEST_ASSERT_TRUE(aggregator.put(12345, data));
    std::vector<uint8_t> dataOut;
    TEST_ASSERT_TRUE(aggregator.get(dataOut));
    TEST_ASSERT_TRUE(data == dataOut);
}

TEST_CASE("Test PollDataAggregator Put and Get Wrap", "[PollDataAggregator]") 
{
    PollDataAggregator aggregator;
    aggregator.init(3, 3);
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
    PollDataAggregator aggregator;
    aggregator.init(10, 3);
    std::vector<uint8_t> dataOut;
    TEST_ASSERT_FALSE(aggregator.get(dataOut));
}

TEST_CASE("Test PollDataAggregator Put and Get Full", "[PollDataAggregator]") 
{
    PollDataAggregator aggregator;
    aggregator.init(3,3);
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
    PollDataAggregator aggregator;
    aggregator.init(3,3);
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
    PollDataAggregator aggregator;
    aggregator.init(4,4);
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
