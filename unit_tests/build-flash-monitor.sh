FLASHPORT=${1:-COM4}
BUILD_IDF_VERS=${2:-esp-idf}
TARGET_CHIP=${3:-esp32}
FLASHBAUD=${4:-2000000}
rm -r ../unit_tests/build
rm ./sdkconfig
. $HOME/esp/${BUILD_IDF_VERS}/export.sh
idf.py set-target $TARGET_CHIP
idf.py build &&\
if uname -r | grep -q "icrosoft"
then
    echo "Running on Windows WSL"
    python.exe ../scripts/flashUsingPartitionCSV.py partitions.csv build unittests.bin $FLASHPORT $TARGET_CHIP -b$FLASHBAUD 
    # &&\
    # python.exe ../scripts/SerialMonitor.py $FLASHPORT -b 115200
else
    echo "Running on Linux"
    python3 ../scripts/flashUsingPartitionCSV.py partitions.csv build unittests.bin $FLASHPORT $TARGET_CHIP -b$FLASHBAUD 
    # &&\
    # python3 ../scripts/SerialMonitor.py $FLASHPORT -b 115200
fi
