
# Resister the component
idf_component_register(
    NAME
      RaftI2C
    SRCS 
      "components/RaftI2C/BusI2C/BusAccessor.cpp"
      "components/RaftI2C/BusI2C/BusI2C.cpp"
      "components/RaftI2C/BusI2C/BusI2CScheduler.cpp"
      "components/RaftI2C/BusI2C/BusMultiplexers.cpp"
      "components/RaftI2C/BusI2C/BusScanner.cpp"
      "components/RaftI2C/BusI2C/BusStatusMgr.cpp"
      "components/RaftI2C/BusI2C/BusStuckHandler.cpp"
      "components/RaftI2C/BusI2C/DeviceIdentMgr.cpp"
      "components/RaftI2C/BusI2C/DevicePollingMgr.cpp"
      "components/RaftI2C/BusPowerController/BusPowerController_GPIOExp.cpp"
      "components/RaftI2C/I2CCentral/RaftI2CCentral.cpp"
      "components/RaftI2C/I2CCentral_ESPIDF/RaftI2CCentral_ESPIDF.cpp"
    INCLUDE_DIRS
      "components/RaftI2C/BusI2C"
      "components/RaftI2C/BusPowerController"
      "components/RaftI2C/I2CCentral"
      "components/RaftI2C/I2CCentral_ESPIDF"
      "${CMAKE_BINARY_DIR}"
    REQUIRES
      RaftCore
      driver
    )
