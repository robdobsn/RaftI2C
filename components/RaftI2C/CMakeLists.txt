idf_component_register(
                SRCS 
                  "I2CCentral/RaftI2CCentral.cpp"
                INCLUDE_DIRS
                  "I2CCentral"
                REQUIRES
                  raftcore-src
                )
