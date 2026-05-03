# Raft Project
cmake_minimum_required(VERSION 3.16)

# Bootstrap using RaftBootstrap script
set(BOOTSTRAP_URL "https://github.com/robdobsn/RaftCore/releases/download/v1.37.1/RaftBootstrap.cmake")
file(DOWNLOAD ${BOOTSTRAP_URL} "${CMAKE_BINARY_DIR}/RaftBootstrap.cmake")
include("${CMAKE_BINARY_DIR}/RaftBootstrap.cmake")

# Include the RaftI2C component folder
list(APPEND EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../components/RaftI2C")

# Define the project dependencies
project(${_systype_name} DEPENDS ${ADDED_PROJECT_DEPENDENCIES})
