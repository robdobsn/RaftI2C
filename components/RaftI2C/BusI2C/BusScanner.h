#pragma once

#include <vector>
#include <functional>
#include "RaftJson.h"
#include "BusI2CConsts.h"
#include "BusStatusMgr.h"
#include "RaftI2CCentralIF.h"
#include "BusI2CRequestRec.h"

// Callback to send i2c message
typedef std::function<RaftI2CCentralIF::AccessResultCode(BusI2CRequestRec* pReqRec, uint32_t pollListIdx)> BusI2CRequestFn;

// #define DEBUG_DISABLE_INITIAL_FAST_SCAN

class BusScanner {

public:
    BusScanner(BusStatusMgr& busStatusMgr, BusI2CRequestFn busI2CRequestFn);
    ~BusScanner();
    void setup(const RaftJsonIF& config);
    void service();
    void requestScan(bool enableSlowScan, bool requestFastScan);

    // Scan period
    static const uint32_t I2C_BUS_SCAN_PERIOD_MS = 10;

private:
    // Scanning
    uint32_t _busScanCurAddr = I2C_BUS_ADDRESS_MIN;
    uint32_t _busScanLastMs = 0;

    // Scan boost - used to increase the rate of scanning on some addresses
    uint16_t _scanBoostCount = 0;
    std::vector<uint8_t> _scanBoostAddresses;
    static const uint32_t SCAN_BOOST_FACTOR = 10;
    uint8_t _scanBoostCurAddrIdx = 0;

    // Enable slow scanning initially
    bool _slowScanEnabled = true;

    // Set bus scanning so enough fast scans are done initially to
    // detect online/offline status of all bus elements
    uint32_t _fastScanPendingCount = BusStatusMgr::I2C_ADDR_RESP_COUNT_FAIL_MAX;

    // Status manager
    BusStatusMgr& _busStatusMgr;

    // Bus i2c request function
    BusI2CRequestFn _busI2CRequestFn;

    // Helper to scan next address
    void scanNextAddress(bool isFastScan);
};
