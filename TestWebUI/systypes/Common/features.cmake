# Set the target Espressif chip
set(IDF_TARGET "esp32")

# System version
add_compile_definitions(SYSTEM_VERSION="1.0.0")

# Raft components
set(RAFT_COMPONENTS
    # TODO -replace
    # RaftSysMods@main
    # RaftWebServer@main
)

# File system
set(FS_TYPE "littlefs")
set(FS_IMAGE_PATH "../Common/FSImage")

# Web UI

# Uncomment the "set" line below if you want to use the web UI
# This assumes an app is built using npm run build
# it also assumes that the web app is built into a folder called "dist" in the UI_SOURCE_PATH
set(UI_SOURCE_PATH "../Common/WebUI")

# Uncomment the following line if you do NOT want to gzip the web UI
# set(WEB_UI_GEN_FLAGS ${WEB_UI_GEN_FLAGS} --nogzip)

# Uncomment the following line to include a source map for the web UI - this will increase the size of the web UI
# set(WEB_UI_GEN_FLAGS ${WEB_UI_GEN_FLAGS} --incmap)

# TODO - remove
include(FetchContent)
FetchContent_Declare(
    raftwebserver
    SOURCE_DIR ${CMAKE_SOURCE_DIR}/../../RaftWebServer
)
FetchContent_Populate(raftwebserver)
set(EXTRA_COMPONENT_DIRS ${EXTRA_COMPONENT_DIRS} ${raftwebserver_SOURCE_DIR})
set(ADDED_PROJECT_DEPENDENCIES ${ADDED_PROJECT_DEPENDENCIES} raftwebserver)

FetchContent_Declare(
    raftsysmods
    SOURCE_DIR ${CMAKE_SOURCE_DIR}/../../RaftSysMods
)
FetchContent_Populate(raftsysmods)
set(EXTRA_COMPONENT_DIRS ${EXTRA_COMPONENT_DIRS} ${raftsysmods_SOURCE_DIR})
set(ADDED_PROJECT_DEPENDENCIES ${ADDED_PROJECT_DEPENDENCIES} raftsysmods)

