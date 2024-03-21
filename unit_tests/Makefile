LIBRARYDIR = $(realpath $(CURDIR)/..)
WORKING_DIR = $(realpath $(CURDIR))
DOCKER_ESP_IDF ?= docker run --rm -v $(LIBRARYDIR):$(LIBRARYDIR) -w $(WORKING_DIR) espressif/idf:v5.2.1
TARGET ?= esp32
CMD ?= idf.py -B ./build build -DIDF_TARGET=$(TARGET)
PORT ?= COM3

all: buildall

buildall: Makefile $(wildcard ./*) $(wildcard components/core/*) $(wildcard components/comms/*)
	$(DOCKER_ESP_IDF) $(CMD)

clean:
	$(DOCKER_ESP_IDF) rm -rf build sdkconfig || true

linux: $(wildcard components/core/*) $(wildcard components/comms/*)
	g++ -std=c++17 -I$(LIBRARYDIR)/components/core/RaftJson -pthread -o build_linux/test_raft_core_linux main/test_app_main.cpp $(LIBRARYDIR)/components/core/RaftJson/RaftJson.cpp

flashwsl: buildall
	python.exe $(LIBRARYDIR)/scripts/flashUsingPartitionCSV.py partitions.csv build unittests.bin $(PORT) $(TARGET)
	python.exe ../scripts/SerialMonitor.py $(PORT) -g
