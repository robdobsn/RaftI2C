/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// I2C Bus Extender Manager
//
// Rob Dobson 2024
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "BusExtenderMgr.h"

// #define DEBUG_BUS_EXTENDERS

#ifdef DEBUG_BUS_EXTENDERS
static const char* MODULE_PREFIX = "BusExtenderMgr";
#endif

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Constructor
///////////////////////////////////////////////////////////////////////////////////////////////////////////////

BusExtenderMgr::BusExtenderMgr(BusI2CReqSyncFn busI2CReqSyncFn) :
    _busI2CReqSyncFn(busI2CReqSyncFn)
{
    // Init bus extender records
    initBusExtenderRecs();
}

BusExtenderMgr::~BusExtenderMgr()
{
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Setup
///////////////////////////////////////////////////////////////////////////////////////////////////////////////

void BusExtenderMgr::setup(const RaftJsonIF& config)
{
    // Get the bus extender address range
    _minAddr = config.getLong("busExtMinAddr", I2C_BUS_EXTENDER_BASE);
    _maxAddr = config.getLong("busExtMaxAddr", I2C_BUS_EXTENDER_BASE+I2C_BUS_EXTENDERS_MAX-1);

    // Create bus extender records
    initBusExtenderRecs();

#ifdef DEBUG_BUS_EXTENDERS
    LOG_I(MODULE_PREFIX, "setup minAddr 0x%02x maxAddr 0x%02x numRecs %d", _minAddr, _maxAddr, _busExtenderRecs.size());
#endif
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Service
///////////////////////////////////////////////////////////////////////////////////////////////////////////////

void BusExtenderMgr::service()
{
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Service called from I2C task
///////////////////////////////////////////////////////////////////////////////////////////////////////////////

void BusExtenderMgr::taskService()
{
    // Iterate through bus extenders
    uint32_t addr = _minAddr;
    for (BusExtender& busExtender : _busExtenderRecs)
    {
        if (busExtender.isOnline && !busExtender.isInitialised)
        {
            if (setChannels(addr, I2C_BUS_EXTENDER_ALL_CHANS_ON) == 
                                RaftI2CCentralIF::ACCESS_RESULT_OK)
            {
#ifdef DEBUG_BUS_EXTENDERS
                LOG_I(MODULE_PREFIX, "service bus extender 0x%02x initialised", addr);
#endif                
                busExtender.isInitialised = true;
            }
        }
        addr++;
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
// State change on an element
///////////////////////////////////////////////////////////////////////////////////////////////////////////////

void BusExtenderMgr::elemStateChange(uint32_t addr, bool elemResponding)
{
    // Check if this is a bus extender
    if (!isBusExtender(addr))
        return;

    // Update the bus extender record
    uint32_t elemIdx = addr-_minAddr;
    if (elemResponding)
    {
        if (!_busExtenderRecs[elemIdx].isDetected)
        {
            _busExtenderRecs[elemIdx].isInitialised = false;
            _busExtenderCount++;

            // Debug
#ifdef DEBUG_BUS_EXTENDERS
            LOG_I(MODULE_PREFIX, "elemStateChange new bus extender 0x%02x numDetectedExtenders %d", addr, _busExtenderCount);
#endif
        }
        _busExtenderRecs[elemIdx].isDetected = true;
    }
    else if (_busExtenderRecs[elemIdx].isDetected)
    {
        // Check if it has gone offline in which case set for re-init
        _busExtenderRecs[elemIdx].isInitialised = false;

        // Debug
#ifdef DEBUG_BUS_EXTENDERS
        LOG_I(MODULE_PREFIX, "elemStateChange bus extender now offline so re-init 0x%02x numDetectedExtenders %d", addr, _busExtenderCount);
#endif

    }

    // Set the online status
    _busExtenderRecs[elemIdx].isOnline = elemResponding;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Set bus extender channels using mask
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

RaftI2CCentralIF::AccessResultCode BusExtenderMgr::setChannels(uint32_t addr, uint32_t channelMask)
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
    return _busI2CReqSyncFn(&reqRec, nullptr);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Set bus extender channels (all off or all on)
// Pass addr 0 for all extenders
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void BusExtenderMgr::setAllChannels(bool allOn)
{
    // Set all bus extender channels
    uint32_t addr = _minAddr;
    for (auto& busExtender : _busExtenderRecs)
    {
        if (busExtender.isOnline)
        {
            setChannels(addr, allOn ? 
                        I2C_BUS_EXTENDER_ALL_CHANS_ON : 
                        I2C_BUS_EXTENDER_ALL_CHANS_OFF);
        }
        addr++;
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Enable a single slot on bus extender(s)
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void BusExtenderMgr::enableOneSlot(uint32_t slotPlus1)
{
    // Set all bus extender channels off except for one   
    for (uint32_t extenderIdx = 0; extenderIdx < _busExtenderRecs.size(); extenderIdx++)
    {
        if (_busExtenderRecs[extenderIdx].isOnline)
        {
            uint32_t mask = extenderIdx == (slotPlus1-1) / I2C_BUS_EXTENDER_SLOT_COUNT ? 
                            1 << ((slotPlus1-1) % I2C_BUS_EXTENDER_SLOT_COUNT) : 0;
            uint32_t addr = _minAddr + extenderIdx;
            setChannels(addr, mask);
        }
    }
}
