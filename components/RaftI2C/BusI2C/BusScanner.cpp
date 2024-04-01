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

BusScanner::BusScanner(BusStatusMgr& busStatusMgr, BusExtenderMgr& busExtenderMgr, 
                DeviceIdentMgr deviceIdentMgr, BusI2CReqSyncFn busI2CReqSyncFn) :
    _busStatusMgr(busStatusMgr),
    _busExtenderMgr(busExtenderMgr),
    _deviceIdentMgr(deviceIdentMgr),
    _busI2CReqSyncFn(busI2CReqSyncFn)
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
    // Bus scan period
    _busScanPeriodMs = config.getLong("busScanPeriodMs", I2C_BUS_SCAN_DEFAULT_PERIOD_MS);
    LOG_I(MODULE_PREFIX, "setup busScanPeriodMs %d", _busScanPeriodMs);

    // Scan boost
    std::vector<String> scanBoostAddrStrs;
    config.getArrayElems("scanBoost", scanBoostAddrStrs);
    _scanBoostAddresses.resize(scanBoostAddrStrs.size());
    for (uint32_t i = 0; i < scanBoostAddrStrs.size(); i++)
    {
        _scanBoostAddresses[i] = strtoul(scanBoostAddrStrs[i].c_str(), nullptr, 0);
        LOG_I(MODULE_PREFIX, "setup scanBoost %02x", _scanBoostAddresses[i]);
    }

    // Scanner reset
    _scanBoostCount = 0;
    _busScanCurAddr = _busExtenderMgr.getMinAddr();
    _busScanLastMs = 0;
    _slowScanEnabled = true;
    _fastScanPendingCount = BusStatusMgr::I2C_ADDR_RESP_COUNT_FAIL_MAX+1;
    _busExtenderAddrScanCount = BusStatusMgr::I2C_ADDR_RESP_COUNT_FAIL_MAX+1;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Service
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void BusScanner::service()
{
    // Perform fast scans of the bus if requested
    uint32_t scanLastYieldtMs = millis();
    while (_fastScanPendingCount > 0)
    {
        scanNextAddress();
        _busExtenderMgr.service();
        if (Raft::isTimeout(millis(), scanLastYieldtMs, I2C_BUS_SCAN_FAST_MAX_UNYIELD_MS))
        {
            scanLastYieldtMs = millis();
            vTaskDelay(1);
        }
    }

    // Perform regular scans at _busScanPeriodMs intervals
    if (_slowScanEnabled)
    {
        if ((_busScanPeriodMs == 0) || (Raft::isTimeout(millis(), _busScanLastMs, _busScanPeriodMs)))
            scanNextAddress();
    }

    // Service bus extender manager
    _busExtenderMgr.service();
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Scan a single address
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void BusScanner::scanNextAddress()
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

    // Discover elem(s) at this address
    discoverAddressElems(scanAddress);

    // Bump address if next in regular scan sequence
    if (scanAddress == _busScanCurAddr)
    {
        _busScanCurAddr++;

        // Check if scanning for bus extenders
        if (_busExtenderAddrScanCount > 0)
        {
            if (_busScanCurAddr >= I2C_BUS_EXTENDER_BASE + I2C_BUS_EXTENDERS_MAX)
            {
                _busScanCurAddr = I2C_BUS_EXTENDER_BASE;
                _busExtenderAddrScanCount--;
            }
        }
        else
        {
            if (_busScanCurAddr > I2C_BUS_ADDRESS_MAX)
            {
                _busScanCurAddr = I2C_BUS_ADDRESS_MIN;
                if (_fastScanPendingCount > 0)
                    _fastScanPendingCount--;
            }
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

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Discover elems at specific address
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void BusScanner::discoverAddressElems(uint8_t addr)
{
    // The following assumes that any bus extenders are in all-channel-enabled mode

    // Check if this address has already been identified on an extender
    bool isAddrOnSlot = _busStatusMgr.isAddrFoundOnAnyExtender(addr);
    if (!isAddrOnSlot)
    {
        // Scan the address and if not responding or no bus extenders are present
        // then handle possible bus element state changes
        RaftI2CCentralIF::AccessResultCode rslt = scanOneAddress(addr);
        if ((rslt != RaftI2CCentralIF::ACCESS_RESULT_OK) || (_busExtenderMgr.getBusExtenderCount() == 0) ||
                                _busExtenderMgr.isBusExtender(addr))
        {
            // Update bus element state for extenders (may or may not be one)
            _busExtenderMgr.elemStateChange(addr, rslt == RaftI2CCentralIF::ACCESS_RESULT_OK);

            // Update bus element state
            updateBusElemState(addr, 0, rslt);
            return;
        }
    }

    // We have at least one element responding on this address and bus extenders present
    // so scan the bus extender slot numbers that this address responds on 
    // (if no slot responds then it is found on the main bus - slot 0)
    scanElemSlots(addr);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Scan one address
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

RaftI2CCentralIF::AccessResultCode BusScanner::scanOneAddress(uint32_t addr)
{
    RaftI2CAddrAndSlot addrAndSlot(addr, 0);
    BusI2CRequestRec reqRec(BUS_REQ_TYPE_SLOW_SCAN,
                addrAndSlot,
                0, 0, 
                nullptr, 
                0, 0, 
                nullptr, 
                this);
    return _busI2CReqSyncFn(&reqRec, nullptr);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Scan element slots
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void BusScanner::scanElemSlots(uint32_t addr)
{
    // Disable all bus extender channels initially
    _busExtenderMgr.setAllChannels(false);

    // Check if the address still gets a response
    if (scanOneAddress(addr) == RaftI2CCentralIF::ACCESS_RESULT_OK)
    {
        // Report slot 0 as an element on the main bus has to have a unique address
        updateBusElemState(addr, 0, RaftI2CCentralIF::ACCESS_RESULT_OK);
        // Restore channels
        _busExtenderMgr.setAllChannels(true);
        return;
    }

    // Get bus extender addresses
    std::vector<uint32_t> busExtenderAddrs;
    _busExtenderMgr.getActiveExtenderAddrs(busExtenderAddrs);

    // Iterate through bus extenders
    int extenderCount = busExtenderAddrs.size();
    for (auto& busExtenderAddr : busExtenderAddrs)
    {
        // Iterate through extender slots
        for (uint32_t slotIdx = 0; slotIdx < BusExtenderMgr::I2C_BUS_EXTENDER_SLOT_COUNT; slotIdx++)
        {
            // Calculate the slot
            uint32_t slotPlus1 = (busExtenderAddr - I2C_BUS_EXTENDER_BASE) * BusExtenderMgr::I2C_BUS_EXTENDER_SLOT_COUNT + slotIdx + 1;

            // Setup extender channel
            if (_busExtenderMgr.setChannels(busExtenderAddr, 0x01 << slotIdx) != RaftI2CCentralIF::ACCESS_RESULT_OK)
                break;
            
            // Access the device address again to test if it still reponds
            RaftI2CCentralIF::AccessResultCode rslt = scanOneAddress(addr);

            // Report result for each slot
            updateBusElemState(addr, slotPlus1, rslt);
        }

        // Disable all channels (if necessary)
        extenderCount--;
        if (extenderCount > 0)
        {
            _busExtenderMgr.setChannels(busExtenderAddr, BusExtenderMgr::I2C_BUS_EXTENDER_ALL_CHANS_OFF);
        }
    }

    // Re-enable all channels
    _busExtenderMgr.setAllChannels(true);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Update bus element state
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void BusScanner::updateBusElemState(uint32_t addr, uint32_t slot, RaftI2CCentralIF::AccessResultCode accessResult)
{
    // Update bus element state
    bool isOnline = false;
    bool isChange = _busStatusMgr.updateBusElemState(RaftI2CAddrAndSlot(addr, slot), 
                    accessResult == RaftI2CCentralIF::ACCESS_RESULT_OK, isOnline);
}
