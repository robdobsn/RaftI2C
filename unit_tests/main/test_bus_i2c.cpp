/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Unit tests of RaftJson value extraction
//
// Rob Dobson 2017-2023
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "unity.h"
#include "unity_test_runner.h"

#include "BusI2C.h"

static const char* MODULE_PREFIX = "TestRaftI2C";

TEST_CASE("test_rafti2c_", "[rafti2c_]")
{
    printf("test_rafti2c_ %s\n", MODULE_PREFIX);
    TEST_ASSERT_MESSAGE(true == true, "testinfo");
}
