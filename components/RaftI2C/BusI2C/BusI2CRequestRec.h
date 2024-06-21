/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Bus Request Rec
//
// Rob Dobson 2020
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "BusRequestInfo.h"
#include "RaftI2CCentralIF.h"
#include <functional>

// Callback to send i2c message (async)
typedef std::function<RaftI2CCentralIF::AccessResultCode(const BusRequestInfo* pReqRec, uint32_t pollListIdx)> BusI2CReqAsyncFn;

// Callback to send i2c message (sync)
typedef std::function<RaftI2CCentralIF::AccessResultCode(const BusRequestInfo* pReqRec, std::vector<uint8_t>* pReadData)> BusI2CReqSyncFn;
