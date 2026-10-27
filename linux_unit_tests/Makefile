# RaftI2C host-side tests: decoding with the records generated from RaftCore's catalogue (known values), and
# BusStatusMgr's delivery of poll results to data subscribers
#
# Built against RaftCore: by default the RaftCore checkout next to this one (../../RaftCore, as in a Raft
# development folder); RAFTCORE=<path> for another. "make raft_core" fetches RaftCore from GitHub into ./RaftCore
# (then make RAFTCORE=./RaftCore).
#
#   make check      build and run the tests (exit status non-zero if any fail)
#
# The device type records are generated from RaftCore's catalogue by RaftCore's own scripts, as a firmware build does.

CC = g++
RAFTCORE ?= ../../RaftCore
CORE = $(RAFTCORE)/components/core

CFLAGS = -Wall -std=c++20 -g -ffp-contract=off -DRAFT_CORE

# Header dependencies (-MMD), so a change to a RaftCore or RaftI2C header rebuilds what includes it - an object
# built against an older layout of a class (e.g. DeviceTypeRecord) otherwise links in and fails at runtime
DEPFLAGS = -MMD -MP

GEN_DIR = gen
CATALOGUE = $(GEN_DIR)/DeviceTypeRecords.json
GEN_HEADER = $(GEN_DIR)/DeviceTypeRecords_generated.h

I2C = ../components/RaftI2C

INCLUDES = -I. -I$(GEN_DIR) -I$(RAFTCORE)/linux_unit_tests -I$(RAFTCORE)/unit_tests/main \
  -I$(CORE)/ArduinoUtils -I$(CORE)/RaftJson -I$(CORE)/Utils -I$(CORE)/Bus -I$(CORE)/DeviceTypes \
  -I$(CORE)/RaftDevice -I$(CORE)/DebugGlobals -I$(CORE)/NamedValueProvider -I$(CORE)/SysMod \
  -I$(CORE)/DeviceManager -I$(CORE)/RestAPIEndpoints -I$(CORE)/SupervisorStats -I$(CORE)/ThreadSafeQueue \
  -I$(CORE)/RingBuffer -I$(CORE)/MiniHDLC -I$(CORE)/SysManager -I$(CORE)/SysTypes -I$(CORE)/APICommon \
  -I$(CORE)/FileSystem -I$(I2C)/BusI2C -I$(I2C)/I2CCentral

# BusStatusMgr's FreeRTOS calls, on the host
TEST_FLAGS = -include freertos_host.h

SOURCES = main.cpp test_bus_status.cpp $(I2C)/BusI2C/BusStatusMgr.cpp \
  $(CORE)/Bus/DeviceStatus.cpp $(CORE)/Bus/BusAddrRecord.cpp \
  $(CORE)/DeviceTypes/DeviceTypeRecords.cpp $(CORE)/DeviceTypes/DeviceTypeGrammar.cpp \
  $(CORE)/DeviceTypes/DeviceTypeRuntimeDecoder.cpp $(CORE)/DeviceTypes/DeviceTypeCodeInterpreter.cpp \
  $(CORE)/DeviceTypes/DeviceTypeDecodeVerifier.cpp \
  $(CORE)/Utils/RaftUtils.cpp $(CORE)/Utils/PlatformUtils.cpp $(CORE)/Utils/RaftThreading.cpp \
  $(CORE)/ArduinoUtils/ArduinoWString.cpp $(CORE)/ArduinoUtils/ArduinoTime.cpp

OBJDIR = obj
OBJECTS = $(patsubst %.cpp,$(OBJDIR)/%.o,$(notdir $(SOURCES)))
vpath %.cpp . $(I2C)/BusI2C $(CORE)/Bus $(CORE)/DeviceTypes $(CORE)/Utils $(CORE)/ArduinoUtils

OUTPUT = linux_unit_tests

REPO_URL = https://github.com/robdobsn/RaftCore
DEST_DIR = ./RaftCore

.PHONY: all check clean raft_core

all: $(OUTPUT)

check: $(OUTPUT)
	./$(OUTPUT)

raft_core:
	@echo "Fetching RaftCore repository..."
	python3 ../scripts/FetchGitRepo.py $(REPO_URL) $(DEST_DIR) $(GIT_TAG)

$(RAFTCORE)/scripts/AssembleDeviceTypes.py:
	@echo "RaftCore not found at $(RAFTCORE): set RAFTCORE=<path>, or make raft_core then make RAFTCORE=./RaftCore"
	@false

# RaftCore's catalogue as one file, and the records generated from it
$(CATALOGUE): $(RAFTCORE)/scripts/AssembleDeviceTypes.py $(wildcard $(RAFTCORE)/devtypes/records/*.json)
	mkdir -p $(GEN_DIR)
	python3 $(RAFTCORE)/scripts/AssembleDeviceTypes.py --raftcore $(RAFTCORE) --all --out $@

$(GEN_HEADER): $(CATALOGUE) $(RAFTCORE)/scripts/ProcessDevTypeJsonToC.py $(RAFTCORE)/scripts/DecodeGenerator.py
	python3 $(RAFTCORE)/scripts/ProcessDevTypeJsonToC.py $(CATALOGUE) $(GEN_HEADER) \
		$(GEN_DIR)/DevicePollRecords_generated.h > $(GEN_DIR)/generator.log

$(OBJDIR)/main.o $(OBJDIR)/DeviceTypeRecords.o: $(GEN_HEADER)

$(OBJDIR)/%.o: %.cpp
	@mkdir -p $(OBJDIR)
	$(CC) $(CFLAGS) $(DEPFLAGS) $(TEST_FLAGS) $(INCLUDES) -c $< -o $@

$(OUTPUT): $(OBJECTS)
	$(CC) $(CFLAGS) -o $(OUTPUT) $(OBJECTS)

clean:
	rm -rf $(OUTPUT) $(OBJDIR) $(GEN_DIR)

-include $(OBJECTS:.o=.d)
