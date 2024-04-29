# Raft Project
cmake_minimum_required(VERSION 3.16)
include(FetchContent)

# Fetch the RaftCore library
FetchContent_Declare(
    raftcore
    SOURCE_DIR RaftCore    
    GIT_REPOSITORY https://github.com/robdobsn/RaftCore.git
    GIT_TAG        main
)
FetchContent_Populate(raftcore)
set(ADDED_PROJECT_DEPENDENCIES ${ADDED_PROJECT_DEPENDENCIES} "raftcore")
set(EXTRA_COMPONENT_DIRS ${EXTRA_COMPONENT_DIRS} ${raftcore_SOURCE_DIR})

# Include the library root folder which contains the CMakelists.txt file
list(APPEND EXTRA_COMPONENT_DIRS "..")

# Include the Raft CMake
include(${raftcore_SOURCE_DIR}/scripts/RaftProject.cmake)

# Define the project dependencies
project(${_build_config_name} DEPENDS ${ADDED_PROJECT_DEPENDENCIES})

# Generate File System image
include(${raftcore_SOURCE_DIR}/scripts/RaftGenFSImage.cmake)
