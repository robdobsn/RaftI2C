# Ensure CMake supports FetchContent
cmake_minimum_required(VERSION 3.16)
include(FetchContent)

set(SUPPORTED_TARGETS esp32)

# Fetch the RaftCore library
FetchContent_Declare(
  raftcore
  GIT_REPOSITORY https://github.com/robdobsn/RaftCore.git
  GIT_TAG        v0.1.0
)

# Populate first so we have a folder that we can add in EXTRA_COMPONENT_DIRS
message(STATUS "Fetching dependencies...")
FetchContent_Populate(raftcore)

# Set EXTRA_COMPONENT_DIRS
set(EXTRA_COMPONENT_DIRS ${EXTRA_COMPONENT_DIRS} ${raftcore_SOURCE_DIR})
FetchContent_MakeAvailable(raftcore)

# Debug
message("RaftCore_SOURCE_DIR = " ${rdutils_SOURCE_DIR}/components)

# Extra components including test ones
set(EXTRA_COMPONENT_DIRS ${rdutils_SOURCE_DIR}/components "../components/I2C")

# Set the components to include the tests for.
# This can be overriden from CMake cache:
# - when invoking CMake directly: cmake -D TEST_COMPONENTS="xxxxx" ..
# - when using idf.py: idf.py -T xxxxx build
set(TEST_COMPONENTS "I2C" CACHE STRING "Components to test")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(unittests)
