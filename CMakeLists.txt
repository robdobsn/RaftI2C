idf_component_register(
                SRCS 
                  "components/RaftI2C/I2CCentral/RaftI2CCentral.cpp"
                INCLUDE_DIRS
                  "components/RaftI2C/I2CCentral"
                REQUIRES
                  raftcore-src
                )
