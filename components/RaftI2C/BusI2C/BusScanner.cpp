/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// I2C Bus Scanner
//
// Rob Dobson 2020-2024
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "RaftUtils.h"
#include "BusScanner.h"
#include "BusI2CRequestRec.h"

static const char* MODULE_PREFIX = "BusScanner";

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Constructor and destructor
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

BusScanner::BusScanner(BusStatusMgr& busStatusMgr, BusI2CRequestFn busI2CRequestFn) :
    _busStatusMgr(busStatusMgr),
    _busI2CRequestFn(busI2CRequestFn)
{
    _busScanLastMs = millis();
#ifdef DEBUG_DISABLE_INITIAL_FAST_SCAN
    _fastScanPendingCount = 0;
#endif
}

BusScanner::~BusScanner()
{
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Setup
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void BusScanner::setup(const RaftJsonIF& config)
{
    // Scan boost
    std::vector<String> scanBoostAddrStrs;
    config.getArrayElems("scanBoost", scanBoostAddrStrs);
    _scanBoostAddresses.resize(scanBoostAddrStrs.size());
    for (uint32_t i = 0; i < scanBoostAddrStrs.size(); i++)
    {
        _scanBoostAddresses[i] = strtoul(scanBoostAddrStrs[i].c_str(), nullptr, 0);
        LOG_I(MODULE_PREFIX, "setup scanBoost %02x", _scanBoostAddresses[i]);
    }
    _scanBoostCount = 0;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Service
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void BusScanner::service()
{
    // Perform fast scans of the bus if requested
    while (_fastScanPendingCount > 0)
        scanNextAddress(true);

    // Perform regular scans at I2C_BUS_SCAN_PERIOD_MS intervals
    if (_slowScanEnabled)
    {
        if (Raft::isTimeout(millis(), _busScanLastMs, I2C_BUS_SCAN_PERIOD_MS))
            scanNextAddress(false);
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Scan a single address
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void BusScanner::scanNextAddress(bool isFastScan)
{
    // Scan the addresses specified in scan-boost more frequently than normal
    uint8_t scanAddress = _busScanCurAddr;
    _scanBoostCount++;
    if ((_scanBoostCount >= SCAN_BOOST_FACTOR) && (_fastScanPendingCount == 0))
    {
        // Check if the scan addr index is beyond the end of the array
        if (_scanBoostCurAddrIdx >= _scanBoostAddresses.size())
        {
            // Reset the scan address index
            _scanBoostCurAddrIdx = 0;
            _scanBoostCount = 0;
        }
        else
        {
            scanAddress = _scanBoostAddresses[_scanBoostCurAddrIdx++];
        }
    }

    // TODO - scanning with slot = 0
    // Scan the address
    RaftI2CAddrAndSlot addrAndSlot(scanAddress, 0);
    BusI2CRequestRec reqRec(isFastScan ? BUS_REQ_TYPE_FAST_SCAN : BUS_REQ_TYPE_SLOW_SCAN, 
                addrAndSlot,
                0, 0, 
                nullptr, 
                0, 0, 
                nullptr, 
                this);
    _busI2CRequestFn(&reqRec, 0);

    // Bump address if next in regular scan sequence
    if (scanAddress == _busScanCurAddr)
    {
        _busScanCurAddr++;
        if (_busScanCurAddr > I2C_BUS_ADDRESS_MAX)
        {
            _busScanCurAddr = I2C_BUS_ADDRESS_MIN;
            if (_fastScanPendingCount > 0)
                _fastScanPendingCount--;
        }
    }
    _busScanLastMs = millis();
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Request bus scan
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void BusScanner::requestScan(bool enableSlowScan, bool requestFastScan)
{
    // Perform fast scans to detect online/offline status
    if (requestFastScan)
        _fastScanPendingCount = BusStatusMgr::I2C_ADDR_RESP_COUNT_FAIL_MAX;
    _slowScanEnabled = enableSlowScan;
}
