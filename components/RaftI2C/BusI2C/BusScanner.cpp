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
    _busScanCurAddr = I2C_BUS_EXTENDER_BASE;
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
    while (_fastScanPendingCount > 0)
    {
        scanNextAddress();
        busExtendersInit();
        vTaskDelay(1);
    }

    // Perform regular scans at _busScanPeriodMs intervals
    if (_slowScanEnabled)
    {
        if ((_busScanPeriodMs == 0) || (Raft::isTimeout(millis(), _busScanLastMs, _busScanPeriodMs)))
            scanNextAddress();
    }

    // Initialise bus extenders if necessary
    busExtendersInit();
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Initialize bus extenders
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void BusScanner::busExtendersInit()
{
    // Check if any bus extenders need to be initialized
    uint32_t extenderAddr = 0;
    bool initRequired = _busStatusMgr.getBusExtenderAddrRequiringInit(extenderAddr);
    if (initRequired)
    {
        if (busExtenderSetChannels(extenderAddr, BusStatusMgr::I2C_BUS_EXTENDER_ALL_CHANS_ON) == 
                            RaftI2CCentralIF::ACCESS_RESULT_OK)
        {
            // Set the bus extender as initialised
            _busStatusMgr.setBusExtenderAsInitialised(extenderAddr);
        }
    }
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
        if ((rslt != RaftI2CCentralIF::ACCESS_RESULT_OK) || (_busStatusMgr.getBusExtenderCount() == 0) ||
                                BusStatusMgr::isBusExtender(addr))
        {
            _busStatusMgr.handleBusElemStateChanges(RaftI2CAddrAndSlot(addr, 0), rslt == RaftI2CCentralIF::ACCESS_RESULT_OK);
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
    return _busI2CRequestFn(&reqRec, 0);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Scan element slots
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void BusScanner::scanElemSlots(uint32_t addr)
{
    // Disable all bus extender channels initially
    busExtendersSetAllChannels(false);

    // Check if the address still gets a response
    if (scanOneAddress(addr) == RaftI2CCentralIF::ACCESS_RESULT_OK)
    {
        // Report slot 0 as an element on the main bus has to have a unique address
        _busStatusMgr.handleBusElemStateChanges(RaftI2CAddrAndSlot(addr, 0), true);
        // Restore channels
        busExtendersSetAllChannels(true);
        return;
    }

    // Get bus extender addresses
    std::vector<uint32_t> busExtenderAddrs;
    _busStatusMgr.getBusExtenderAddrList(busExtenderAddrs);

    // Iterate through bus extenders
    int extenderCount = busExtenderAddrs.size();
    for (auto& busExtenderAddr : busExtenderAddrs)
    {
        // Iterate through extender slots
        for (uint32_t slotIdx = 0; slotIdx < BusStatusMgr::I2C_BUS_EXTENDER_SLOT_COUNT; slotIdx++)
        {
            // Calculate the slot
            uint32_t slotPlus1 = (busExtenderAddr - I2C_BUS_EXTENDER_BASE) * BusStatusMgr::I2C_BUS_EXTENDER_SLOT_COUNT + slotIdx + 1;

            // Setup extender channel
            if (busExtenderSetChannels(busExtenderAddr, 0x01 << slotIdx) != RaftI2CCentralIF::ACCESS_RESULT_OK)
                break;
            
            // Access the device address again to test if it still reponds
            RaftI2CCentralIF::AccessResultCode rslt = scanOneAddress(addr);

            // Report result for each slot
            _busStatusMgr.handleBusElemStateChanges(RaftI2CAddrAndSlot(addr, slotPlus1), rslt == RaftI2CCentralIF::ACCESS_RESULT_OK);
        }

        // Disable all channels (if necessary)
        extenderCount--;
        if (extenderCount > 0)
        {
            busExtenderSetChannels(busExtenderAddr, BusStatusMgr::I2C_BUS_EXTENDER_ALL_CHANS_OFF);
        }
    }

    // Re-enable all channels
    busExtendersSetAllChannels(true);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Set bus extender channels (all off or all on)
// Pass addr 0 for all extenders
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

RaftI2CCentralIF::AccessResultCode BusScanner::busExtenderSetChannels(uint32_t addr, uint32_t channelMask)
{
    // Initialise bus extender
    RaftI2CAddrAndSlot addrAndSlot(addr, 0);
    uint8_t writeData[1] = { uint8_t(channelMask) };
    BusI2CRequestRec reqRec(BUS_REQ_TYPE_STD, 
                addrAndSlot,
                0, sizeof(writeData),
                writeData,
                0, 0, 
                nullptr, 
                this);
    return _busI2CRequestFn(&reqRec, 0);
}

void BusScanner::busExtendersSetAllChannels(bool allOn)
{
    // Get bus extender addresses
    std::vector<uint32_t> busExtenderAddrs;
    _busStatusMgr.getBusExtenderAddrList(busExtenderAddrs);

    // Set all bus extender channels
    for (auto& busExtenderAddr : busExtenderAddrs)
    {
        busExtenderSetChannels(busExtenderAddr, allOn ? 
                    BusStatusMgr::I2C_BUS_EXTENDER_ALL_CHANS_ON : 
                    BusStatusMgr::I2C_BUS_EXTENDER_ALL_CHANS_OFF);
    }
}
