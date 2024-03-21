# Raft Project
cmake_minimum_required(VERSION 3.16)
include(FetchContent)

# Fetch the RaftCore library
FetchContent_Declare(
    raftcore
    GIT_REPOSITORY https://github.com/robdobsn/RaftCore.git
    GIT_TAG        main
)
FetchContent_Populate(raftcore)

# Include the Raft CMake
include(${raftcore_SOURCE_DIR}/scripts/RaftProject.cmake)

# Define the project dependencies
project(${_build_config_name} DEPENDS ${ADDED_PROJECT_DEPENDENCIES})

# Generate File System image
include(${raftcore_SOURCE_DIR}/scripts/RaftGenFSImage.cmake)
