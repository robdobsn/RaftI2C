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
                uint8_t* pWriteData, uint32_t readReqLen, uint32_t barAccessForMsAfterSend,
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
    }
    uint32_t getReadReqLen()
    {
        return _readReqLen;
    }
    uint32_t getWriteDataLen()
    {
        return _reqBuf.size();
    }
    uint8_t* getWriteData()
    {
        return _reqBuf.data();
    }
    BusRequestCallbackType getCallback()
    {
        return _busReqCallback;
    }

    float getPollFreqHz()
    {
        return _pollFreqHz;
    }
    void* getCallbackParam()
    {
        return _pCallbackData; 
    }
    bool isPolling()
    {
        return _busReqType == BUS_REQ_TYPE_POLL;
    }
    bool isFWUpdate()
    {
        return _busReqType == BUS_REQ_TYPE_FW_UPDATE;
    }
    bool isFastScan()
    {
        return _busReqType == BUS_REQ_TYPE_FAST_SCAN;
    }
    bool isSlowScan()
    {
        return _busReqType == BUS_REQ_TYPE_SLOW_SCAN;
    }
    bool shouldSendIfPaused()
    {
        return _busReqType == BUS_REQ_TYPE_SEND_IF_PAUSED;
    }
    bool isScan()
    {
        return (_busReqType == BUS_REQ_TYPE_FAST_SCAN) || (_busReqType == BUS_REQ_TYPE_SLOW_SCAN);
    }
    uint32_t getReqType()
    {
        return _busReqType;
    }
    RaftI2CAddrAndSlot getAddrAndSlot()
    {
        return _addrAndSlot;
    }
    uint32_t getCmdId()
    {
        return _cmdId;
    }
    uint32_t getBarAccessForMsAfterSend()
    {
        return _barAccessForMsAfterSend;
    }
    RaftI2CAddrAndSlot _addrAndSlot;
    uint32_t _cmdId = 0;
    uint32_t _readReqLen = 0;
    void* _pCallbackData = nullptr;
    BusRequestCallbackType _busReqCallback = nullptr;
    BusReqType _busReqType = BUS_REQ_TYPE_STD;
    float _pollFreqHz = 0;
    uint32_t _barAccessForMsAfterSend = 0;

    // Request buffer
    static const uint32_t REQUEST_BUFFER_MAX_BYTES = 120;
    std::vector<uint8_t> _reqBuf;
};

// Callback to send i2c message (async)
typedef std::function<RaftI2CCentralIF::AccessResultCode(BusI2CRequestRec* pReqRec, uint32_t pollListIdx)> BusI2CReqAsyncFn;

// Callback to send i2c message (sync)
typedef std::function<RaftI2CCentralIF::AccessResultCode(BusI2CRequestRec* pReqRec, std::vector<uint8_t>* pReadData)> BusI2CReqSyncFn;
