/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// I2C Bus Extender Manager
//
// Rob Dobson 2024
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "BusI2CRequestRec.h"
#include "RaftJsonIF.h"

class BusExtenderMgr
{
public:
    // Constructor and destructor
    BusExtenderMgr(BusI2CReqSyncFn busI2CReqSyncFn);
    virtual ~BusExtenderMgr();

    // Setup
    void setup(const RaftJsonIF& config);

    // Service
    void service();

    // State change on an element (may or may not be a bus extender)
    void elemStateChange(uint32_t addr, bool elemResponding);

    // Check if address is a bus extender
    bool isBusExtender(uint8_t addr)
    {
        return (addr >= _minAddr) && (addr <= _maxAddr);
    }

    // Get count of bus extenders
    uint32_t getBusExtenderCount() const
    {
        return _busExtenderCount;
    }

    // Get min address of bus extenders
    uint32_t getMinAddr() const
    {
        return _minAddr;
    }

    // Get list of active extender addresses
    void getActiveExtenderAddrs(std::vector<uint32_t>& activeExtenderAddrs)
    {
        activeExtenderAddrs.clear();
        uint32_t addr = _minAddr;
        for (BusExtender& busExtender : _busExtenderRecs)
        {
            if (busExtender.isOnline)
                activeExtenderAddrs.push_back(addr);
            addr++;
        }
    }

    // Enable one slot on bus extender(s)
    void enableOneSlot(uint32_t slotPlus1);

    // Set channels on extender
    RaftI2CCentralIF::AccessResultCode setChannels(uint32_t addr, uint32_t channelMask);

    // Set all channels on or off
    void setAllChannels(bool allOn);

    // Bus extender slot count
    static const uint32_t I2C_BUS_EXTENDER_SLOT_COUNT = 8;

    // Bus extender channels
    static const uint32_t I2C_BUS_EXTENDER_ALL_CHANS_OFF = 0;
    static const uint32_t I2C_BUS_EXTENDER_ALL_CHANS_ON = 0xff;

private:
    // Bus access function
    BusI2CReqSyncFn _busI2CReqSyncFn;

    // Bus extender address range
    uint32_t _minAddr = I2C_BUS_EXTENDER_BASE;
    uint32_t _maxAddr = I2C_BUS_EXTENDER_BASE+I2C_BUS_EXTENDERS_MAX-1;

    // Bus extender record
    class BusExtender
    {
    public:
        bool isDetected:1 = false,
             isOnline:1 = false,
             isInitialised:1 = false;
    };

    // Bus extenders
    std::vector<BusExtender> _busExtenderRecs;

    // Number of bus extenders detected so far
    uint8_t _busExtenderCount = 0;

    // Helpers
    void initBusExtenderRecs()
    {
        _busExtenderRecs.clear();
        _busExtenderRecs.resize(_maxAddr-_minAddr+1);
        _busExtenderCount = 0;
    }
};
