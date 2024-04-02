/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// I2C Bus Scanner
//
// Rob Dobson 2020-2024
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <vector>
#include "RaftJson.h"
#include "BusI2CConsts.h"
#include "BusStatusMgr.h"
#include "BusExtenderMgr.h"
#include "RaftI2CCentralIF.h"
#include "BusI2CRequestRec.h"
#include "DeviceIdentMgr.h"

// #define DEBUG_DISABLE_INITIAL_FAST_SCAN

class BusScanner {

public:
    BusScanner(BusStatusMgr& busStatusMgr, BusExtenderMgr& BusExtenderMgr,
                DeviceIdentMgr deviceIdentMgr, BusI2CReqSyncFn busI2CReqSyncFn);
    ~BusScanner();
    void setup(const RaftJsonIF& config);
    void service();
    void requestScan(bool enableSlowScan, bool requestFastScan);

    // Service called from I2C task
    void taskService();

    // Scan period
    static const uint32_t I2C_BUS_SCAN_DEFAULT_PERIOD_MS = 10;

    // Max fast scanning without yielding
    static const uint32_t I2C_BUS_SCAN_FAST_MAX_UNYIELD_MS = 10;

private:
    // Scanning
    uint32_t _busScanCurAddr = I2C_BUS_EXTENDER_BASE;
    uint32_t _busScanLastMs = 0;
    uint32_t _busScanPeriodMs = I2C_BUS_SCAN_DEFAULT_PERIOD_MS;

    // Scan boost - used to increase the rate of scanning on some addresses
    uint16_t _scanBoostCount = 0;
    std::vector<uint8_t> _scanBoostAddresses;
    static const uint32_t SCAN_BOOST_FACTOR = 10;
    uint8_t _scanBoostCurAddrIdx = 0;

    // Enable slow scanning initially
    bool _slowScanEnabled = true;

    // Set bus scanning so enough fast scans are done initially to
    // detect online/offline status of all bus elements
    uint32_t _fastScanPendingCount = BusStatusMgr::I2C_ADDR_RESP_COUNT_FAIL_MAX+1;

    // Bus extender address scanning count
    uint32_t _busExtenderAddrScanCount = BusStatusMgr::I2C_ADDR_RESP_COUNT_FAIL_MAX+1;

    // Status manager
    BusStatusMgr& _busStatusMgr;

    // Bus extender manager
    BusExtenderMgr& _busExtenderMgr;

    // Device ident manager
    DeviceIdentMgr _deviceIdentMgr;

    // Bus i2c request function (synchronous)
    BusI2CReqSyncFn _busI2CReqSyncFn = nullptr;

    // Helper to scan next address
    void scanNextAddress();
    void discoverAddressElems(uint8_t addr);
    RaftI2CCentralIF::AccessResultCode scanOneAddress(uint32_t addr);
    void scanElemSlots(uint32_t addr);
    void updateBusElemState(uint32_t addr, uint32_t slotPlus1, RaftI2CCentralIF::AccessResultCode accessResult);
};
