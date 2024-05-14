# Compiler
CC = g++

# Compiler flags
CFLAGS = -Wall -std=c++20 -lc -g -DRAFT_CORE

# Include paths
INCLUDES = -I../unit_tests/main -I../components/RaftI2C/BusI2C -I../components/RaftI2C/I2CCentral -I./RaftCore/components/core/Utils -I./RaftCore/components/core/ArduinoUtils -I./RaftCore/components/core/Bus -I.

# Source files
SOURCES = main.cpp utils.cpp ./RaftCore/components/core/Utils/RaftUtils.cpp ./RaftCore/components/core/ArduinoUtils/ArduinoWString.cpp

# Output binary
OUTPUT = linux_unit_tests

# RaftCore
REPO_URL=https://github.com/robdobsn/RaftCore
DEST_DIR=./RaftCore
# GIT_TAG=v1.0  # Optional: Specify a tag if needed

# Ensure raft_core is executed every time by making it .PHONY
.PHONY: all raft_core test_dev_types_generated clean

all: $(OUTPUT)

raft_core:
	python3 ../scripts/FetchGitRepo.py $(REPO_URL) $(DEST_DIR) $(GIT_TAG)

test_dev_types_generated:
	python3 "../scripts/ProcessDevTypeJsonToC.py" "TestDevTypeRecs.json" "TestDevTypes_generated.h"

# Ensure raft_core is run whenever OUTPUT is built
$(OUTPUT): $(SOURCES) raft_core test_dev_types_generated
	$(CC) $(CFLAGS) $(INCLUDES) -o $(OUTPUT) $(SOURCES)

clean:
	rm -f $(OUTPUT)
