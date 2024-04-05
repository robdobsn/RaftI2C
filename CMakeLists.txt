
# Resister the component
idf_component_register(
                SRCS 
                  "components/RaftI2C/I2CCentral/RaftI2CCentral.cpp"
                  "components/RaftI2C/BusI2C/BusAccessor.cpp"
                  "components/RaftI2C/BusI2C/BusI2C.cpp"
                  "components/RaftI2C/BusI2C/BusI2CESPIDF.cpp"
                  "components/RaftI2C/BusI2C/BusI2CScheduler.cpp"
                  "components/RaftI2C/BusI2C/BusScanner.cpp"
                  "components/RaftI2C/BusI2C/BusStatusMgr.cpp"
                  "components/RaftI2C/BusI2C/BusExtenderMgr.cpp"
                  "components/RaftI2C/BusI2C/DeviceIdentMgr.cpp"
                  "components/RaftI2C/BusI2C/DevicePollingMgr.cpp"
                INCLUDE_DIRS
                  "components/RaftI2C/I2CCentral"
                  "components/RaftI2C/BusI2C"
                  "${CMAKE_BINARY_DIR}"
                REQUIRES
                  raftcore-src
                )

# Find Python
find_package(Python3 REQUIRED)

# # Define paths
set(JSON_FILE "${CMAKE_CURRENT_SOURCE_DIR}/DeviceIdentification/DevIdent.json")
set(GENERATED_HEADER "${CMAKE_BINARY_DIR}/DevIdentJson_generated.h")

# Custom command to generate C++ header from JSON
add_custom_command(
    OUTPUT ${GENERATED_HEADER}
    COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/scripts/ConvertJsonToC.py" "${JSON_FILE}" "${GENERATED_HEADER}"
    DEPENDS ${JSON_FILE}
    COMMENT "---------------------- Generating Device Identification C++ header from JSON ---------------------------"
)

add_custom_target(generate_dev_ident_header DEPENDS ${GENERATED_HEADER})

add_dependencies(${COMPONENT_LIB} generate_dev_ident_header)
