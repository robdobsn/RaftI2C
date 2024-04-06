/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Bus Request Rec
//
// Rob Dobson 2020
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "BusRequestInfo.h"
#include "BusI2CConsts.h"
#include "RaftI2CCentralIF.h"
#include <functional>

// Request record
class BusI2CRequestRec
{
public:
    BusI2CRequestRec()
    {
        clear();
    }
    void clear()
    {
        _addrAndSlot.clear();
        _cmdId = 0;
        _readReqLen = 0;
        _busReqCallback = nullptr;
        _pCallbackData = nullptr;
        _busReqType = BUS_REQ_TYPE_STD;
        _pollFreqHz = 1;
        _barAccessForMsAfterSend = 0;
    };
    BusI2CRequestRec(BusReqType busReqType, RaftI2CAddrAndSlot addrAndSlot, uint32_t cmdId, uint32_t writeDataLen, 
                const uint8_t* pWriteData, uint32_t readReqLen, const uint8_t* pReadDataMask, uint32_t barAccessForMsAfterSend,
                BusRequestCallbackType busReqCallback, void* pCallbackData)
    {
        clear();
        _busReqType = busReqType;
        _addrAndSlot = addrAndSlot;
        _cmdId = cmdId;
        writeDataLen = (writeDataLen <= REQUEST_BUFFER_MAX_BYTES) ? writeDataLen : REQUEST_BUFFER_MAX_BYTES;
        if (writeDataLen > 0)
            _reqBuf.assign(pWriteData, pWriteData+writeDataLen);
        else
            _reqBuf.clear();
        _readReqLen = readReqLen;
        if (pReadDataMask)
            _readDataMask.assign(pReadDataMask, pReadDataMask+readReqLen);
        _pCallbackData = pCallbackData;
        _busReqCallback = busReqCallback;
        _pollFreqHz = 1;
        _barAccessForMsAfterSend = barAccessForMsAfterSend;
    }
    BusI2CRequestRec(BusRequestInfo& reqInfo)
    {
        set(reqInfo);
    }
    void set(BusRequestInfo& reqInfo)
    {
        clear();
        _readReqLen = reqInfo.getReadReqLen();
        _reqBuf.assign(reqInfo.getWriteData(), reqInfo.getWriteData()+reqInfo.getWriteDataLen());
        _addrAndSlot = RaftI2CAddrAndSlot::fromCompositeAddrAndSlot(reqInfo.getAddressUint32());
        _cmdId = reqInfo.getCmdId();
        _pCallbackData = reqInfo.getCallbackParam();
        _busReqCallback = reqInfo.getCallback();
        _busReqType = reqInfo.getBusReqType();
        _pollFreqHz = reqInfo.getPollFreqHz();
        _barAccessForMsAfterSend = reqInfo.getBarAccessForMsAfterSend();
        _readDataMask.clear();
    }
    uint32_t getReadReqLen() const
    {
        return _readReqLen;
    }
    uint32_t getWriteDataLen() const
    {
        return _reqBuf.size();
    }
    const uint8_t* getWriteData() const
    {
        return _reqBuf.data();
    }
    BusRequestCallbackType getCallback() const
    {
        return _busReqCallback;
    }

    float getPollFreqHz() const
    {
        return _pollFreqHz;
    }
    void* getCallbackParam() const
    {
        return _pCallbackData; 
    }
    bool isPolling() const
    {
        return _busReqType == BUS_REQ_TYPE_POLL;
    }
    bool isFWUpdate() const
    {
        return _busReqType == BUS_REQ_TYPE_FW_UPDATE;
    }
    bool isFastScan() const
    {
        return _busReqType == BUS_REQ_TYPE_FAST_SCAN;
    }
    bool isSlowScan() const
    {
        return _busReqType == BUS_REQ_TYPE_SLOW_SCAN;
    }
    bool shouldSendIfPaused() const
    {
        return _busReqType == BUS_REQ_TYPE_SEND_IF_PAUSED;
    }
    bool isScan() const
    {
        return (_busReqType == BUS_REQ_TYPE_FAST_SCAN) || (_busReqType == BUS_REQ_TYPE_SLOW_SCAN);
    }
    uint32_t getReqType() const
    {
        return _busReqType;
    }
    RaftI2CAddrAndSlot getAddrAndSlot() const
    {
        return _addrAndSlot;
    }
    uint32_t getCmdId() const
    {
        return _cmdId;
    }
    uint32_t getBarAccessForMsAfterSend() const
    {
        return _barAccessForMsAfterSend;
    }
    RaftI2CAddrAndSlot _addrAndSlot;
    uint32_t _cmdId = 0;
    uint32_t _readReqLen = 0;
    void* _pCallbackData = nullptr;
    BusRequestCallbackType _busReqCallback = nullptr;
    BusReqType _busReqType = BUS_REQ_TYPE_STD;

    // TODO - decide if this is needed - or maybe should be ms interval
    float _pollFreqHz = 0;
    uint32_t _barAccessForMsAfterSend = 0;

    // Request buffer
    static const uint32_t REQUEST_BUFFER_MAX_BYTES = 1000;
    std::vector<uint8_t> _reqBuf;

    // Read data mask
    // TODO - apply mask if present
    std::vector<uint8_t> _readDataMask;
};

// Callback to send i2c message (async)
typedef std::function<RaftI2CCentralIF::AccessResultCode(const BusI2CRequestRec* pReqRec, uint32_t pollListIdx)> BusI2CReqAsyncFn;

// Callback to send i2c message (sync)
typedef std::function<RaftI2CCentralIF::AccessResultCode(const BusI2CRequestRec* pReqRec, std::vector<uint8_t>* pReadData)> BusI2CReqSyncFn;
