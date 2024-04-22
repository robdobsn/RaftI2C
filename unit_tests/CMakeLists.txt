# unit test project
cmake_minimum_required(VERSION 3.16)
include(FetchContent)

# TODO - restore

# # Fetch the RaftCore library
# FetchContent_Declare(
#     raftcore
#     GIT_REPOSITORY https://github.com/robdobsn/RaftCore.git
#     GIT_TAG        main
# )

# Fetch the RaftCore library
FetchContent_Declare(
    raftcore
    SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../RaftCore
)
FetchContent_Populate(raftcore)

set(ADDED_PROJECT_DEPENDENCIES ${ADDED_PROJECT_DEPENDENCIES} "raftcore")
set(EXTRA_COMPONENT_DIRS ${EXTRA_COMPONENT_DIRS} ${raftcore_SOURCE_DIR})

# Include the library root folder which contains the CMakelists.txt file
list(APPEND EXTRA_COMPONENT_DIRS "..")

# Include the Raft CMake
include(${raftcore_SOURCE_DIR}/scripts/RaftProject.cmake)

# Project
project(unittests)
