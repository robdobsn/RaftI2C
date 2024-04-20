
# Resister the component
idf_component_register(
    NAME
      RaftI2C
    SRCS 
      "components/RaftI2C/BusI2C/BusAccessor.cpp"
      "components/RaftI2C/BusI2C/BusExtenderMgr.cpp"
      "components/RaftI2C/BusI2C/BusI2C.cpp"
      "components/RaftI2C/BusI2C/BusI2CAddrStatus.cpp"
      "components/RaftI2C/BusI2C/BusI2CESPIDF.cpp"
      "components/RaftI2C/BusI2C/BusI2CScheduler.cpp"
      "components/RaftI2C/BusI2C/BusScanner.cpp"
      "components/RaftI2C/BusI2C/BusStatusMgr.cpp"
      "components/RaftI2C/BusI2C/BusStuckHandler.cpp"
      "components/RaftI2C/BusI2C/DeviceIdentMgr.cpp"
      "components/RaftI2C/BusI2C/DevicePollingMgr.cpp"
      "components/RaftI2C/BusI2C/DeviceStatus.cpp"
      "components/RaftI2C/BusI2C/DeviceTypeRecords.cpp"
      "components/RaftI2C/I2CCentral/RaftI2CCentral.cpp"
      "components/RaftI2C/I2CCentral/ESPIDF5I2C/ESPIDF5I2CCentral.cpp"
    INCLUDE_DIRS
      "components/RaftI2C/I2CCentral"
      "components/RaftI2C/I2CCentral/ESPIDF5I2C"
      "components/RaftI2C/BusI2C"
      "${CMAKE_BINARY_DIR}"
    REQUIRES
      RaftCore
      driver
    )

# Find Python
find_package(Python3 REQUIRED)

# Device type record paths
set(JSON_FILE "${CMAKE_CURRENT_SOURCE_DIR}/DeviceTypeRecords/DeviceTypeRecords.json")
set(GENERATED_HEADER "${CMAKE_BINARY_DIR}/DeviceTypeRecords_generated.h")

# Custom command to generate device type records header file from JSON
add_custom_command(
    OUTPUT ${GENERATED_HEADER}
    COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/scripts/ProcessDevTypeJsonToC.py" "${JSON_FILE}" "${GENERATED_HEADER}"
    DEPENDS ${JSON_FILE}
    COMMENT "---------------------- Generating Device Type Records header from JSON ---------------------------"
)

add_custom_target(generate_dev_ident_header DEPENDS ${GENERATED_HEADER})

add_dependencies(${COMPONENT_LIB} generate_dev_ident_header)
