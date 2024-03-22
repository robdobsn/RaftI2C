idf_component_register(
        SRCS
            "test_main.cpp"
            # "test_bus_i2c.cpp"
        INCLUDE_DIRS 
            "."
        REQUIRES
            esp_system
            unity
            nvs_flash
            # RaftI2C
            # raftcore-src
        WHOLE_ARCHIVE
        )
