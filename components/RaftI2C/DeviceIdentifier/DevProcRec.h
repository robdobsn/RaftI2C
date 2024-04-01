
#pragma once

#include "RaftUtils.h"
#include "BusI2CConsts.h"
#include "DevInfoRec.h"

class DevProcRec
{
public:
    enum HwDevProcState
    {
        HW_DEV_PROC_STATE_DETECTING,
        HW_DEV_PROC_STATE_DETECTED,
        HW_DEV_PROC_STATE_UNKNOWN_DEVICE_TYPE
    };

    DevProcRec(RaftI2CAddrAndSlot addrAndSlot) : addrAndSlot(addrAndSlot)
    {
    }

    RaftI2CAddrAndSlot addrAndSlot;
    HwDevProcState state = HW_DEV_PROC_STATE_DETECTING;
    uint32_t lastStateChangeMs = 0;
    uint32_t curDevInfoRecordIdx = 0;
    DevInfoRec devInfoRec;
};
