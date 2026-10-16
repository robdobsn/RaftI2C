/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Device Ident Manager
//
// Rob Dobson 2024
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Info
#define INFO_NEW_DEVICE_IDENTIFIED

// Debug
// #define DEBUG_DEVICE_IDENT_MGR
// #define DEBUG_DEVICE_IDENT_MGR_DETAIL
// #define DEBUG_HANDLE_BUS_DEVICE_INFO
// #define DEBUG_GET_DECODED_POLL_RESPONSES
// #define DEBUG_MAKE_BUS_REQUEST
// #define DEBUG_MAKE_BUS_REQUEST_VERBOSE
// #define DEBUG_CMD_RESULT_CALLBACK
// #define DEBUG_POLL_DATA_AGGREGATOR_OVERFLOW

#include "DeviceIdentMgr.h"
#include "DeviceTypeRecords.h"
#include "BusRequestInfo.h"
#include "BusRequestResult.h"
#include "RaftDevice.h"
#include "BusI2CAddrAndSlot.h"
#include "PollDataAggregator.h"
#include "Logger.h"
#include <memory>
#include <atomic>
#include <algorithm>
#include "RaftThreading.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Constructor
///////////////////////////////////////////////////////////////////////////////////////////////////////////////

DeviceIdentMgr::DeviceIdentMgr(BusStatusMgr& BusStatusMgr, BusReqSyncFn busReqSyncFn, BusReqAsyncFn busReqAsyncFn,
            BusReqEnqueueFn busReqEnqueueFn) :
    _busStatusMgr(BusStatusMgr),
    _busReqSyncFn(busReqSyncFn),
    _busReqAsyncFn(busReqAsyncFn),
    _busReqEnqueueFn(busReqEnqueueFn)
{
    // Mutex for handler registration
    RaftMutex_init(_handlerMutex);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Destructor
///////////////////////////////////////////////////////////////////////////////////////////////////////////////

DeviceIdentMgr::~DeviceIdentMgr()
{
    RaftMutex_destroy(_handlerMutex);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Setup
///////////////////////////////////////////////////////////////////////////////////////////////////////////////

void DeviceIdentMgr::setup(const RaftJsonIF& config)
{
    // Enabled
    _isEnabled = config.getBool("identEnable", true);

    // Debug
    LOG_I(MODULE_PREFIX, "setup %s", _isEnabled ? "enabled" : "disabled");
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get list of device addresses attached to the bus
/// @param pAddrList pointer to array to receive addresses
/// @param onlyAddressesWithIdentPollResponses true to only return addresses with ident poll responses    
void DeviceIdentMgr::getDeviceAddresses(std::vector<BusElemAddrType>& addresses, bool onlyAddressesWithIdentPollResponses) const
{
    // Get list of all bus element addresses
    _busStatusMgr.getBusElemAddresses(addresses, onlyAddressesWithIdentPollResponses);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Identification scheduler: identify the elements that are due (called from the bus task)
/// @param budgetUs time budget for this pass (at least one element is serviced if any is due)
/// @note Each element is identified on its own schedule (BusIdentState.h): a new element at once, then by the
///       outcome. Identification is a job that can pause: a detection or initialisation step followed by a long
///       pause ends the pass for that element, and the job resumes when the pause is over, so a sensor that needs
///       hundreds of ms after a command doesn't hold up the bus. Nothing is identified until the device type
///       records are complete (every record source - e.g. the device type file - is ready)
void DeviceIdentMgr::taskService(uint64_t budgetUs)
{
    if (!_isEnabled || !deviceTypeRecords.areRecordSourcesReady())
        return;
    uint64_t startUs = micros();
    uint32_t generation = deviceTypeRecords.getGeneration();
    uint32_t hookEpoch = _hookEpoch.load();

    // Re-identification requests (made on other tasks)
    std::vector<BusReIdentRequest> requests;
    if (_reidentRequestsPending && RaftMutex_lock(_handlerMutex, RAFT_MUTEX_WAIT_FOREVER))
    {
        requests.swap(_reidentRequests);
        _reidentRequestsPending = false;
        RaftMutex_unlock(_handlerMutex);
    }
    for (const BusReIdentRequest& request : requests)
    {
        uint32_t numMarked = _busStatusMgr.applyReIdentRequest(request);
        LOG_I(MODULE_PREFIX, "reIdentify scope %d addr %s typeIdx %d: %d device(s)", (int)request.scope,
                    BusI2CAddrAndSlot::toString(request.address).c_str(),
                    request.deviceTypeIndex == DEVICE_TYPE_INDEX_INVALID ? -1 : (int)request.deviceTypeIndex, (int)numMarked);
    }

    // Re-initialisation requests
    if (_reinitRequestsPending)
        serviceReInitRequests();

    // Jobs whose element was never handed back (it went offline) are dropped
    uint32_t nowMs = millis();
    _jobs.erase(std::remove_if(_jobs.begin(), _jobs.end(), [nowMs](const IdentJob& job) {
                return (int32_t)(nowMs - job.resumeAtMs) > (int32_t)IDENT_JOB_STALE_MS;
            }), _jobs.end());

    for (uint32_t numDone = 0; numDone < MAX_IDENT_PER_PASS; numDone++)
    {
        if ((numDone > 0) && (micros() - startUs >= budgetUs))
            break;
        BusStatusMgr::IdentWork work;
        if (!_busStatusMgr.getNextIdentWork(millis(), generation, hookEpoch, work))
            break;

        // Select the element's slot
        uint32_t slotNum = BusI2CAddrAndSlot::getSlotNum(work.address);
        if (_reselectSlotFn && (_reselectSlotFn(slotNum) != RAFT_OK))
        {
            DeviceStatus noStatus;
            _busStatusMgr.setIdentOutcome(work.address, BusIdentState::BusError, noStatus, generation, hookEpoch, 0);
            continue;
        }

        if (work.reident)
        {
            // An identified device identified again - it keeps its type (and is polled) until the result is known
            DeviceStatus deviceStatus;
            BusStatusMgr::ReIdentResult result = reIdentifyDevice(work.address, work.deviceTypeIndex, deviceStatus);
            _busStatusMgr.setReIdentOutcome(work.address, result, deviceStatus, generation, hookEpoch);
        }
        else if (work.reverify)
        {
            // Re-verification of an identified device after a poll failure it recovered from
            DetectionResult result = reverifyDevice(work.address, work.deviceTypeIndex);
            _busStatusMgr.setReverifyOutcome(work.address,
                        result == DetectionResult::Match ? BusStatusMgr::ReverifyResult::StillMatches :
                        result == DetectionResult::NoMatch ? BusStatusMgr::ReverifyResult::Mismatch :
                        BusStatusMgr::ReverifyResult::BusError);
        }
        else
        {
            // Identification (a new job, or one resuming after a pause)
            auto jobIt = std::find_if(_jobs.begin(), _jobs.end(), [&work](const IdentJob& job) { return job.address == work.address; });
            if ((jobIt != _jobs.end()) && ((int32_t)(millis() - jobIt->resumeAtMs) < 0))
            {
                // Paused (it became due early, e.g. the records changed) - not resumed until the pause is over
                if (_deselectSlotsFn && (slotNum != 0))
                    _deselectSlotsFn();
                continue;
            }
            if (jobIt == _jobs.end())
            {
                IdentJob job;
                job.address = work.address;
                job.generation = generation;
                job.hookEpoch = hookEpoch;
                job.candidates = deviceTypeRecords.getIdentCandidates(BusI2CAddrAndSlot::getI2CAddr(work.address));
                _jobs.push_back(job);
                jobIt = _jobs.end() - 1;
            }
            DeviceStatus deviceStatus;
            IdentOutcome outcome = runIdentJob(*jobIt, deviceStatus, true);
            // The outcome is for the records (and hook) the job started with - if they have changed since, the
            // element is tried again at once
            uint32_t resumeAtMs = jobIt->resumeAtMs;
            uint32_t jobGeneration = jobIt->generation;
            uint32_t jobHookEpoch = jobIt->hookEpoch;
            if (outcome != IdentOutcome::InProgress)
                _jobs.erase(jobIt);
            _busStatusMgr.setIdentOutcome(work.address, outcomeToState(outcome), deviceStatus, jobGeneration, jobHookEpoch, resumeAtMs);
        }
        if (_deselectSlotsFn && (slotNum != 0))
            _deselectSlotsFn();
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Identify device (to completion, pausing inline)
/// @param address address of device
/// @param deviceStatus (out) device status
/// @note The device's slot must be selected. The identification scheduler (taskService) is what identifies
///       devices on the bus; this is kept for callers that need an answer at once
void DeviceIdentMgr::identifyDevice(BusElemAddrType address, DeviceStatus& deviceStatus)
{
    deviceStatus.clear();
    if (!_isEnabled)
        return;
    IdentJob job;
    job.address = address;
    job.candidates = deviceTypeRecords.getIdentCandidates(BusI2CAddrAndSlot::getI2CAddr(address));
    runIdentJob(job, deviceStatus, false);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Run an identification job until it completes or (if allowed) pauses
/// @param job job (its position is kept between calls)
/// @param deviceStatus (out) device status when Identified
/// @param allowPause true to return InProgress at a long pause (resume when job.resumeAtMs is reached); false to
///        pause inline
/// @return outcome
DeviceIdentMgr::IdentOutcome DeviceIdentMgr::runIdentJob(IdentJob& job, DeviceStatus& deviceStatus, bool allowPause)
{
    deviceStatus.clear();
    BusElemAddrType address = job.address;

    // Identification has started: from now on a device may hold any device type index, so a record can no longer
    // replace a base record at its index
    deviceTypeRecords.markReady();

    // Detection: address-based identification runs FIRST. Detection is a non-destructive read/compare, so a device
    // with a matching device-type record is identified without disturbing it. Only if nothing here matches do we
    // fall back to the delegated new-device hook (which probes with a framed vendor command that can be destructive
    // to third-party sensors)
    while (job.phase == IdentJob::Phase::Detect)
    {
        if (job.candPos >= job.candidates.size())
        {
            // No record matched. A bus error (other than a NACK) ended detection: trying the identification hook on a
            // device that couldn't be read would give a wrong or harmful result
            if (job.busError)
                return IdentOutcome::BusError;
            IdentOutcome hookOutcome = runIdentHook(address, deviceStatus);
            if (hookOutcome != IdentOutcome::Identified)
                return hookOutcome;
            job.matchedIdx = deviceStatus.deviceTypeIndex;
            deviceStatus.clear();
            job.phase = IdentJob::Phase::Init;
            job.stepIdx = 0;
            break;
        }
        DeviceTypeIndexType deviceTypeIndex = job.candidates[job.candPos];
        DetectionResult detectionResult = runDetectionSteps(address, deviceTypeIndex, job.stepIdx, job.resumeAtMs, allowPause);
        if (detectionResult == DetectionResult::Paused)
            return IdentOutcome::InProgress;
        if (detectionResult == DetectionResult::BusError)
        {
            // A bus error (other than a NACK, which is how many devices reject a command meant for another type)
            // ends the attempt - it is tried again later
            const DeviceTypeRecord* pRec = deviceTypeRecords.getRecord(deviceTypeIndex);
            LOG_W(MODULE_PREFIX, "identifyDevice bus error detecting %s at %s - identification deferred",
                        pRec && pRec->deviceType ? pRec->deviceType : "?", BusI2CAddrAndSlot::toString(address).c_str());
            job.busError = true;
            job.candPos = job.candidates.size();
            continue;
        }
        if (detectionResult == DetectionResult::Match)
        {
            job.matchedIdx = deviceTypeIndex;
            job.phase = IdentJob::Phase::Init;
            job.stepIdx = 0;
            break;
        }
#ifdef DEBUG_DEVICE_IDENT_MGR_DETAIL
        LOG_I(MODULE_PREFIX, "identifyDevice CHECK FAILED typeIdx %d at %s", deviceTypeIndex, BusI2CAddrAndSlot::toString(address).c_str());
#endif
        job.candPos++;
        job.stepIdx = 0;
    }

    // Initialise the device. A NACK during initialisation is reported (and initialisation repeated once) but the
    // device is still identified - a command such as a soft reset is often NACKed. Any other bus error means the
    // device wasn't initialised, so identification is repeated later
    InitResult initResult = runInitSteps(address, job.matchedIdx, job.stepIdx, job.initNacks, job.resumeAtMs, allowPause);
    if (initResult == InitResult::Paused)
        return IdentOutcome::InProgress;
    if ((initResult == InitResult::Done) && (job.initNacks > 0) && !job.initRepeated)
    {
        job.initRepeated = true;
        job.stepIdx = 0;
        uint32_t firstNacks = job.initNacks;
        job.initNacks = 0;
        initResult = runInitSteps(address, job.matchedIdx, job.stepIdx, job.initNacks, job.resumeAtMs, allowPause);
        if (initResult == InitResult::Paused)
            return IdentOutcome::InProgress;
        if (job.initNacks > 0)
        {
            const DeviceTypeRecord* pRec = deviceTypeRecords.getRecord(job.matchedIdx);
            LOG_W(MODULE_PREFIX, "identifyDevice %s at %s: %d init step(s) NACKed (repeated: %d) - device used anyway",
                        pRec && pRec->deviceType ? pRec->deviceType : "?", BusI2CAddrAndSlot::toString(address).c_str(),
                        (int)firstNacks, (int)job.initNacks);
            _initNackCount++;
        }
    }
    if (initResult == InitResult::BusError)
    {
        LOG_W(MODULE_PREFIX, "identifyDevice bus error initialising typeIdx %d at %s - identification repeated later",
                    job.matchedIdx, BusI2CAddrAndSlot::toString(address).c_str());
        return IdentOutcome::InitFailed;
    }

    // Identified: device type, polling info and poll results store
    completeDeviceStatus(address, job.matchedIdx, deviceStatus);
#ifdef INFO_NEW_DEVICE_IDENTIFIED
    const DeviceTypeRecord* pRec = deviceTypeRecords.getRecord(job.matchedIdx);
    LOG_I(MODULE_PREFIX, "identifyDevice new device %s at address %s",
            pRec && pRec->deviceType ? pRec->deviceType : "NO NAME", BusI2CAddrAndSlot::toString(address).c_str());
#endif
    return IdentOutcome::Identified;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Offer a device that no record matched to the identification hook
/// @param address address
/// @param deviceStatus (out) device status - its deviceTypeIndex is set if Identified
/// @return Identified (the hook claimed it, with a usable type), Deferred or NoMatch
DeviceIdentMgr::IdentOutcome DeviceIdentMgr::runIdentHook(BusElemAddrType address, DeviceStatus& deviceStatus)
{
    // The handler probes with a framed vendor command that can be destructive to third-party sensors (e.g. Sensirion
    // devices interpret the probe bytes as commands and stop measuring), so it runs last - after all non-destructive
    // address-based identification. Dynamically-addressed RSAO devices are probe-only so they never match by address
    // and are handled here. The hook isn't run at an address claimed by a record whose policy is DISABLED_CLAIM.
    // The handler {fn, ctx} pair is copied out under the handler mutex (it may be registered/cleared from another
    // task) and called outside it
    uint32_t i2cAddr = BusI2CAddrAndSlot::getI2CAddr(address);
    RaftNewDeviceIdentFn newDeviceIdentFn = nullptr;
    void* pNewDeviceIdentCtx = nullptr;
    if (deviceTypeRecords.isAddrHookBlocked(i2cAddr) || !RaftMutex_lock(_handlerMutex, RAFT_MUTEX_WAIT_FOREVER))
        return IdentOutcome::NoMatch;
    // The handler is only offered addresses it handles
    newDeviceIdentFn = isAddrInHookRangesLocked(i2cAddr) ? _newDeviceIdentFn : nullptr;
    pNewDeviceIdentCtx = _newDeviceIdentCtx;
    if (newDeviceIdentFn)
    {
        _handlerCallingTask = (void*)xTaskGetCurrentTaskHandle();
        _newDeviceIdentCallCount++;
        _newDeviceIdentInProgress = true;
    }
    RaftMutex_unlock(_handlerMutex);
    if (!newDeviceIdentFn)
        return IdentOutcome::NoMatch;

    RaftDeviceIdentVerdict verdict = newDeviceIdentFn(address, deviceStatus, pNewDeviceIdentCtx);
    _newDeviceIdentInProgress = false;
    _hookCallCount++;

    // The handler may have performed bus transactions (e.g. a synchronous probe) that reset the multiplexer slot
    // selection, which initialisation relies on - so re-select it
    if (_reselectSlotFn)
        _reselectSlotFn(BusI2CAddrAndSlot::getSlotNum(address));

    if (verdict == RaftDeviceIdentVerdict::Handled)
    {
        // The handler set deviceStatus.deviceTypeIndex; the device status is completed from that index using the
        // standard device type records, so the handler stays device-agnostic. A type that is disabled isn't used,
        // whoever identified it
        DeviceTypeIndexType deviceTypeIndex = deviceStatus.deviceTypeIndex;
        const DeviceTypeRecord* pRec = deviceTypeRecords.getRecord(deviceTypeIndex);
        deviceStatus.clear();
        if (!pRec)
        {
            LOG_W(MODULE_PREFIX, "identifyDevice handler claimed address %s but device-type index %d invalid",
                    BusI2CAddrAndSlot::toString(address).c_str(), deviceTypeIndex);
            return IdentOutcome::NoMatch;
        }
        if (deviceTypeRecords.getPolicy(deviceTypeIndex) != DeviceTypeRecord::POLICY_ENABLED)
        {
            LOG_W(MODULE_PREFIX, "identifyDevice handler claimed address %s as %s, which is disabled - not used",
                    BusI2CAddrAndSlot::toString(address).c_str(), pRec->deviceType ? pRec->deviceType : "?");
            return IdentOutcome::NoMatch;
        }
#ifdef INFO_NEW_DEVICE_IDENTIFIED
        LOG_I(MODULE_PREFIX, "identifyDevice handler claimed address %s as %s (typeIdx %d)",
                BusI2CAddrAndSlot::toString(address).c_str(), pRec->deviceType ? pRec->deviceType : "NO NAME", deviceTypeIndex);
#endif
        deviceStatus.deviceTypeIndex = deviceTypeIndex;
        return IdentOutcome::Identified;
    }
    deviceStatus.clear();
    if (verdict == RaftDeviceIdentVerdict::Deferred)
    {
        // Not ready to identify yet: asked again after the Deferred interval
#ifdef DEBUG_DEVICE_IDENT_MGR
        LOG_I(MODULE_PREFIX, "identifyDevice handler DEFERRED address %s", BusI2CAddrAndSlot::toString(address).c_str());
#endif
        return IdentOutcome::Deferred;
    }
#ifdef DEBUG_DEVICE_IDENT_MGR
    LOG_I(MODULE_PREFIX, "identifyDevice handler NOT MINE address %s", BusI2CAddrAndSlot::toString(address).c_str());
#endif
    return IdentOutcome::NoMatch;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Re-verify an identified device after a poll failure it recovered from: run its type's detection again;
///        re-initialise it if it still matches
/// @param address address
/// @param deviceTypeIndex the device's type
/// @return Match (re-initialised), NoMatch (it isn't that type now - it is identified again) or BusError (tried again
///         later)
/// @note A probe-only type (identified by a hook) isn't re-verified, nor one whose detection has a long pause (the
///       device is being polled); these are treated as a match without re-initialising
DeviceIdentMgr::DetectionResult DeviceIdentMgr::reverifyDevice(BusElemAddrType address, DeviceTypeIndexType deviceTypeIndex)
{
    const DeviceTypeRecord* pRec = deviceTypeRecords.getRecord(deviceTypeIndex);
    if (!pRec)
        return DetectionResult::NoMatch;
    std::vector<DeviceTypeRecords::DeviceDetectionRec> detectionRecs;
    if (pRec->isProbeOnly() || !deviceTypeRecords.getDetectionRecs(deviceTypeIndex, detectionRecs))
        return DetectionResult::Match;
    for (const auto& detectionRec : detectionRecs)
        if (detectionRec.pauseAfterSendMs >= LONG_PAUSE_MS)
            return DetectionResult::Match;
    _reverifyCount++;
    uint32_t stepIdx = 0;
    uint32_t resumeAtMs = 0;
    DetectionResult result = runDetectionSteps(address, deviceTypeIndex, stepIdx, resumeAtMs, false);
    if (result == DetectionResult::Match)
    {
        uint32_t initNacks = 0;
        stepIdx = 0;
        if (runInitSteps(address, deviceTypeIndex, stepIdx, initNacks, resumeAtMs, false) == InitResult::BusError)
            result = DetectionResult::BusError;
    }
    else if (result == DetectionResult::NoMatch)
    {
        _reverifyMismatchCount++;
    }
    LOG_I(MODULE_PREFIX, "reverify %s at %s after a poll failure: %s", pRec->deviceType ? pRec->deviceType : "?",
                BusI2CAddrAndSlot::toString(address).c_str(),
                result == DetectionResult::Match ? "still matches, re-initialised" :
                result == DetectionResult::NoMatch ? "no longer matches - identifying again" : "bus error - tried again later");
    return result;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Identify an identified device again (it is polled meanwhile, so pauses are made inline)
/// @param address address
/// @param currentTypeIndex the type it is identified as
/// @param deviceStatus (out) the new device status (NewType)
/// @return SameType (not initialised again), NewType (initialised), NoMatch or TryAgain (bus error or deferred)
BusStatusMgr::ReIdentResult DeviceIdentMgr::reIdentifyDevice(BusElemAddrType address, DeviceTypeIndexType currentTypeIndex,
            DeviceStatus& deviceStatus)
{
    deviceStatus.clear();
    DeviceTypeIndexType matchedIdx = DEVICE_TYPE_INDEX_INVALID;
    std::vector<DeviceTypeIndexType> candidates = deviceTypeRecords.getIdentCandidates(BusI2CAddrAndSlot::getI2CAddr(address));
    for (DeviceTypeIndexType deviceTypeIndex : candidates)
    {
        uint32_t stepIdx = 0;
        uint32_t resumeAtMs = 0;
        DetectionResult result = runDetectionSteps(address, deviceTypeIndex, stepIdx, resumeAtMs, false);
        if (result == DetectionResult::BusError)
            return BusStatusMgr::ReIdentResult::TryAgain;
        if (result == DetectionResult::Match)
        {
            matchedIdx = deviceTypeIndex;
            break;
        }
    }
    if (matchedIdx == DEVICE_TYPE_INDEX_INVALID)
    {
        IdentOutcome hookOutcome = runIdentHook(address, deviceStatus);
        if (hookOutcome == IdentOutcome::Deferred)
            return BusStatusMgr::ReIdentResult::TryAgain;
        if (hookOutcome != IdentOutcome::Identified)
            return BusStatusMgr::ReIdentResult::NoMatch;
        matchedIdx = deviceStatus.deviceTypeIndex;
        deviceStatus.clear();
    }
    if (matchedIdx == currentTypeIndex)
        return BusStatusMgr::ReIdentResult::SameType;

    // Another type: initialise it
    uint32_t stepIdx = 0;
    uint32_t initNacks = 0;
    uint32_t resumeAtMs = 0;
    if (runInitSteps(address, matchedIdx, stepIdx, initNacks, resumeAtMs, false) == InitResult::BusError)
        return BusStatusMgr::ReIdentResult::TryAgain;
    completeDeviceStatus(address, matchedIdx, deviceStatus);
    const DeviceTypeRecord* pRec = deviceTypeRecords.getRecord(matchedIdx);
    LOG_I(MODULE_PREFIX, "reIdentify %s now %s (typeIdx %d, was %d)", BusI2CAddrAndSlot::toString(address).c_str(),
                pRec && pRec->deviceType ? pRec->deviceType : "?", matchedIdx, currentTypeIndex);
    return BusStatusMgr::ReIdentResult::NewType;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Run an identified device's initialisation again (carried out on the bus task)
bool DeviceIdentMgr::requestReInit(BusElemAddrType address, std::function<void(bool ok)> doneFn)
{
    if (_busStatusMgr.getDeviceTypeIndexByAddr(address) == DEVICE_TYPE_INDEX_INVALID)
        return false;
    if (!RaftMutex_lock(_handlerMutex, RAFT_MUTEX_WAIT_FOREVER))
        return false;
    bool accepted = _reinitRequests.size() < MAX_REIDENT_REQUESTS;
    if (accepted)
        _reinitRequests.push_back({address, doneFn});
    _reinitRequestsPending = true;
    RaftMutex_unlock(_handlerMutex);
    return accepted;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Do the re-initialisations requested (bus task). A device that is re-initialised is reported as Identified
///        (with the same type) so that settings made since it was first initialised are applied again
void DeviceIdentMgr::serviceReInitRequests()
{
    std::vector<ReInitRequest> requests;
    if (!RaftMutex_lock(_handlerMutex, RAFT_MUTEX_WAIT_FOREVER))
        return;
    requests.swap(_reinitRequests);
    _reinitRequestsPending = false;
    RaftMutex_unlock(_handlerMutex);
    for (const ReInitRequest& request : requests)
    {
        bool ok = false;
        DeviceTypeIndexType deviceTypeIndex = _busStatusMgr.getDeviceTypeIndexByAddr(request.address);
        uint32_t slotNum = BusI2CAddrAndSlot::getSlotNum(request.address);
        if ((deviceTypeIndex != DEVICE_TYPE_INDEX_INVALID) && (!_reselectSlotFn || (_reselectSlotFn(slotNum) == RAFT_OK)))
        {
            uint32_t stepIdx = 0, initNacks = 0, resumeAtMs = 0;
            ok = runInitSteps(request.address, deviceTypeIndex, stepIdx, initNacks, resumeAtMs, false) != InitResult::BusError;
            if (ok)
                _busStatusMgr.setReverifyOutcome(request.address, BusStatusMgr::ReverifyResult::StillMatches);
            if (_deselectSlotsFn && (slotNum != 0))
                _deselectSlotsFn();
        }
        LOG_I(MODULE_PREFIX, "reInit %s typeIdx %d: %s", BusI2CAddrAndSlot::toString(request.address).c_str(),
                    deviceTypeIndex == DEVICE_TYPE_INDEX_INVALID ? -1 : (int)deviceTypeIndex, ok ? "done" : "failed");
        if (request.doneFn)
            request.doneFn(ok);
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Identification information for a device
String DeviceIdentMgr::getIdentInfoJson(BusElemAddrType address) const
{
    String stateFields = _busStatusMgr.getIdentStateJsonFields(address);
    if (stateFields.length() == 0)
        return "{}";
    uint32_t i2cAddr = BusI2CAddrAndSlot::getI2CAddr(address);
    String candidates = "[";
    for (DeviceTypeIndexType idx : deviceTypeRecords.getIdentCandidates(i2cAddr))
    {
        const DeviceTypeRecord* pRec = deviceTypeRecords.getRecord(idx);
        String cand = "{\"dtIdx\":" + String((int)idx);
        RaftJson::appendStringField(cand, "key", pRec && pRec->deviceType ? pRec->deviceType : "");
        cand += "}";
        candidates += String(candidates.length() > 1 ? "," : "") + cand;
    }
    candidates += "]";
    bool hookOffered = isAddrClaimedByHook(i2cAddr) && !deviceTypeRecords.isAddrHookBlocked(i2cAddr);
    return "{" + stateFields + ",\"candidates\":" + candidates + ",\"hook\":" + (hookOffered ? "true" : "false") + "}";
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Fill in a device status for a type: the type, polling info and poll results store
void DeviceIdentMgr::completeDeviceStatus(BusElemAddrType address, DeviceTypeIndexType deviceTypeIndex, DeviceStatus& deviceStatus)
{
    deviceStatus.deviceTypeIndex = deviceTypeIndex;
    deviceTypeRecords.getPollInfo(address, deviceTypeIndex, deviceStatus.deviceIdentPolling);
    auto pDataAggregator = std::make_shared<PollDataAggregator>(
            deviceStatus.deviceIdentPolling.numPollResultsToStore,
            deviceStatus.deviceIdentPolling.pollResultSizeIncTimestamp);
    deviceStatus.setAndOwnPollDataAggregator(pDataAggregator);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Ask for devices to be identified again (carried out on the bus task)
bool DeviceIdentMgr::requestReIdentify(const BusReIdentRequest& request)
{
    if (!RaftMutex_lock(_handlerMutex, RAFT_MUTEX_WAIT_FOREVER))
        return false;
    bool accepted = _reidentRequests.size() < MAX_REIDENT_REQUESTS;
    if (accepted)
        _reidentRequests.push_back(request);
    _reidentRequestsPending = true;
    RaftMutex_unlock(_handlerMutex);
    return accepted;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Check device type match (communicates with the device to check its type)
/// @param address address
/// @param deviceTypeIndex device type index
/// @return Match, NoMatch (including the device NACKing a step), or BusError (any other bus failure)
DeviceIdentMgr::DetectionResult DeviceIdentMgr::checkDeviceTypeMatch(BusElemAddrType address, DeviceTypeIndexType deviceTypeIndex)
{
    uint32_t stepIdx = 0;
    uint32_t resumeAtMs = 0;
    return runDetectionSteps(address, deviceTypeIndex, stepIdx, resumeAtMs, false);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Run a type's detection steps from stepIdx
/// @param address address
/// @param deviceTypeIndex device type index
/// @param stepIdx (in/out) next step
/// @param resumeAtMs (out) when to resume (if Paused)
/// @param allowPause true to return Paused after a step followed by a long pause
/// @return Match, NoMatch (including the device NACKing a step), BusError (any other bus failure) or Paused
DeviceIdentMgr::DetectionResult DeviceIdentMgr::runDetectionSteps(BusElemAddrType address, DeviceTypeIndexType deviceTypeIndex,
            uint32_t& stepIdx, uint32_t& resumeAtMs, bool allowPause)
{
    // Get the detection records - a record with an invalid detection value never matches
    std::vector<DeviceTypeRecords::DeviceDetectionRec> detectionRecs;
    if (!deviceTypeRecords.getDetectionRecs(deviceTypeIndex, detectionRecs))
        return DetectionResult::NoMatch;

    // Each step must match; detection stops at the first that doesn't
    for (; stepIdx < detectionRecs.size(); )
    {
        const auto& detectionRec = detectionRecs[stepIdx];

        // Write, then read the bytes to check (none for a step that only writes or pauses)
        BusRequestInfo reqRec(BUS_REQ_TYPE_FAST_SCAN,
                address,
                0,
                detectionRec.writeData.size(),
                detectionRec.writeData.data(),
                detectionRec.readLen,
                detectionRec.pauseAfterSendMs,
                nullptr,
                this);
        std::vector<uint8_t> readData;
        RaftRetCode rslt = _busReqSyncFn != nullptr ? _busReqSyncFn(&reqRec, &readData) : RAFT_BUS_NOT_INIT;
        uint32_t sentMs = millis();

#ifdef DEBUG_DEVICE_IDENT_MGR
        String writeStr;
        Raft::getHexStrFromBytes(detectionRec.writeData.data(), detectionRec.writeData.size(), writeStr);
        String readDataStr;
        Raft::getHexStrFromBytes(readData.data(), readData.size(), readDataStr);
        LOG_I(MODULE_PREFIX, "checkDeviceTypeMatch %s addr %s writeData %s rslt %d readData %s readSize %d pauseAfterMs %d",
                    rslt == RAFT_OK ? "OK" : "BUS ACCESS FAILED",
                    BusI2CAddrAndSlot::toString(address).c_str(),
                    writeStr.c_str(), rslt,
                    readDataStr.c_str(), readData.size(),
                    detectionRec.pauseAfterSendMs);
#endif

        // A NACK means the device rejected this step (not this type); anything else is a bus problem
        if (rslt == RAFT_BUS_ACK_ERROR)
            return DetectionResult::NoMatch;
        if (rslt != RAFT_OK)
            return DetectionResult::BusError;

        // Any one check matching is a match (a step with no checks only writes or pauses)
        bool checkValueMatch = detectionRec.checkValues.size() == 0;
        for (const auto& checkValue : detectionRec.checkValues)
        {
            if (readData.size() != checkValue.second.size())
                continue;
            bool checkByteMatch = true;
            for (uint32_t i = 0; i < readData.size(); i++)
            {
                if ((readData[i] & checkValue.first[i]) != checkValue.second[i])
                {
                    checkByteMatch = false;
                    break;
                }
            }
            if (checkByteMatch)
            {
                checkValueMatch = true;
                break;
            }
        }

        // Each 2-byte word must be followed by a valid CRC
        if (checkValueMatch && detectionRec.crc8w)
            checkValueMatch = DeviceTypeGrammar::checkCrc8Words(readData.data(), readData.size());

#ifdef DEBUG_DEVICE_IDENT_MGR
        LOG_I(MODULE_PREFIX, "checkDeviceTypeMatch address %s %s",
                    BusI2CAddrAndSlot::toString(address).c_str(),
                    checkValueMatch ? "MATCH" : "NO MATCH");
#endif
        if (!checkValueMatch)
            return DetectionResult::NoMatch;

        // Pause (the address is barred for that time): a long pause ends this part of the job
        stepIdx++;
        if (detectionRec.pauseAfterSendMs > 0)
        {
            if (allowPause && (detectionRec.pauseAfterSendMs >= LONG_PAUSE_MS))
            {
                resumeAtMs = sentMs + detectionRec.pauseAfterSendMs;
                return DetectionResult::Paused;
            }
            pauseUntil(sentMs + detectionRec.pauseAfterSendMs);
        }
    }
    return DetectionResult::Match;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Process device initialisation
/// @param address address
/// @param deviceTypeIndex device type index
/// @return true if every step succeeded (a NACKed step is still reported as success, as before)
bool DeviceIdentMgr::processDeviceInit(BusElemAddrType address, DeviceTypeIndexType deviceTypeIndex)
{
    uint32_t stepIdx = 0;
    uint32_t initNacks = 0;
    uint32_t resumeAtMs = 0;
    return runInitSteps(address, deviceTypeIndex, stepIdx, initNacks, resumeAtMs, false) == InitResult::Done;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Run a type's initialisation steps from stepIdx
/// @param address address
/// @param deviceTypeIndex device type index
/// @param stepIdx (in/out) next step
/// @param initNacks (in/out) number of steps NACKed
/// @param resumeAtMs (out) when to resume (if Paused)
/// @param allowPause true to return Paused after a step followed by a long pause
/// @return Done (NACKed steps are counted in initNacks), Paused or BusError (a bus error other than a NACK)
DeviceIdentMgr::InitResult DeviceIdentMgr::runInitSteps(BusElemAddrType address, DeviceTypeIndexType deviceTypeIndex,
            uint32_t& stepIdx, uint32_t& initNacks, uint32_t& resumeAtMs, bool allowPause)
{
    std::vector<BusRequestInfo> initBusRequests;
    deviceTypeRecords.getInitBusRequests(address, deviceTypeIndex, initBusRequests);

#ifdef DEBUG_DEVICE_IDENT_MGR
    LOG_I(MODULE_PREFIX, "processDeviceInit address %s numInitBusRequests %d from step %d",
                BusI2CAddrAndSlot::toString(address).c_str(), initBusRequests.size(), (int)stepIdx);
#endif

    for (; stepIdx < initBusRequests.size(); )
    {
        const BusRequestInfo& initBusRequest = initBusRequests[stepIdx];
        std::vector<uint8_t> readData;
        BusRequestInfo reqRec(initBusRequest);
        RaftRetCode rslt = _busReqSyncFn != nullptr ? _busReqSyncFn(&reqRec, &readData) : RAFT_BUS_NOT_INIT;
        uint32_t sentMs = millis();
        if (rslt == RAFT_BUS_ACK_ERROR)
            initNacks++;
        else if (rslt != RAFT_OK)
            return InitResult::BusError;

        // Pause after the step (the address is barred for that time): a long pause ends this part of the job
        stepIdx++;
        uint32_t pauseMs = initBusRequest.getBarAccessForMsAfterSend();
        if (pauseMs > 0)
        {
            if (allowPause && (pauseMs >= LONG_PAUSE_MS))
            {
                resumeAtMs = sentMs + pauseMs;
                return InitResult::Paused;
            }
            pauseUntil(sentMs + pauseMs);
        }
    }
    return InitResult::Done;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Pause until a time (inline)
void DeviceIdentMgr::pauseUntil(uint32_t untilMs)
{
    while ((int32_t)(millis() - untilMs) < 0)
        delay(1);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Identification state for an outcome
BusIdentState DeviceIdentMgr::outcomeToState(IdentOutcome outcome)
{
    switch (outcome)
    {
        case IdentOutcome::Identified: return BusIdentState::Identified;
        case IdentOutcome::NoMatch: return BusIdentState::NoMatch;
        case IdentOutcome::Deferred: return BusIdentState::Deferred;
        case IdentOutcome::BusError: return BusIdentState::BusError;
        case IdentOutcome::InitFailed: return BusIdentState::InitFailed;
        case IdentOutcome::InProgress: return BusIdentState::Pending;
    }
    return BusIdentState::Pending;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Format device poll responses to JSON
///////////////////////////////////////////////////////////////////////////////////////////////////////////////

String DeviceIdentMgr::deviceStatusToJson(BusElemAddrType address, DeviceOnlineState onlineState, uint16_t deviceTypeIndex, 
                const std::vector<uint8_t>& devicePollResponseData, uint32_t responseSize) const
{
    // Get the poll response JSON using DeviceOnlineState directly
    return deviceTypeRecords.deviceStatusToJson(address, onlineState, deviceTypeIndex, devicePollResponseData);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get JSON for device type info
/// @param address Address of element
/// @param includePlugAndPlayInfo true to include plug and play information
/// @param deviceTypeIndex (out) device type index
/// @return JSON string
String DeviceIdentMgr::getDevTypeInfoJsonByAddr(BusElemAddrType address, bool includePlugAndPlayInfo, DeviceTypeIndexType& deviceTypeIndex) const
{
    // Get device type index
    deviceTypeIndex = _busStatusMgr.getDeviceTypeIndexByAddr(address);
    if (deviceTypeIndex == DEVICE_TYPE_INDEX_INVALID)
        return "{}";

    // Get device type info
    return deviceTypeRecords.getDevTypeInfoJsonByTypeIdx(deviceTypeIndex, includePlugAndPlayInfo);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get JSON for device type info
/// @param deviceTypeKey device type key
/// @param includePlugAndPlayInfo true to include plug and play information
/// @param deviceTypeIndex (out) device type index
/// @return JSON string
String DeviceIdentMgr::getDevTypeInfoJsonByTypeName(const String& deviceTypeKey, bool includePlugAndPlayInfo, DeviceTypeIndexType& deviceTypeIndex) const
{
    // Get device type info
    return deviceTypeRecords.getDevTypeInfoJsonByTypeName(deviceTypeKey, includePlugAndPlayInfo, deviceTypeIndex);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get device type info JSON by device type index
/// @param deviceTypeIndex device type index
/// @param includePlugAndPlayInfo include plug and play info
/// @return JSON string
String DeviceIdentMgr::getDevTypeInfoJsonByTypeIdx(DeviceTypeIndexType deviceTypeIndex, bool includePlugAndPlayInfo) const
{
    // Get device type info
    return deviceTypeRecords.getDevTypeInfoJsonByTypeIdx(deviceTypeIndex, includePlugAndPlayInfo);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get queued device data in JSON format
/// @return JSON doc
String DeviceIdentMgr::getQueuedDeviceDataJson()
{
    // Return string
    String jsonStr;

    // Get list of all bus element addresses
    std::vector<BusElemAddrType> addresses;
    _busStatusMgr.getBusElemAddresses(addresses, false);
    for (auto address : addresses)
    {
        // Get bus status for each address
        DeviceOnlineState onlineState = DeviceOnlineState::OFFLINE;
        uint16_t deviceTypeIndex = 0;
        std::vector<uint8_t> devicePollResponseData;
        uint32_t responseSize = 0;
        _busStatusMgr.getBusElemPollResponses(address, onlineState, deviceTypeIndex, devicePollResponseData, responseSize, 0);

        // Skip unidentified devices
        if (deviceTypeIndex == DEVICE_TYPE_INDEX_INVALID)
            continue;

        // Skip unidentified devices
        if (deviceTypeIndex == DEVICE_TYPE_INDEX_INVALID)
            continue;

        // Use device identity manager to convert to JSON
        String jsonData = deviceStatusToJson(address, 
                        onlineState, deviceTypeIndex, devicePollResponseData, responseSize);
        if (jsonData.length() > 0)
        {
            jsonStr += (jsonStr.length() == 0 ? "{" : ",") + jsonData;
        }
    }

    // Add pending deletion notices (devices that have been removed)
    std::vector<BusStatusMgr::DeletionNotice> deletions;
    _busStatusMgr.getPendingDeletions(deletions);
    for (const auto& deletion : deletions)
    {
        // Generate deletion notice with empty data and PENDING_DELETION state
        std::vector<uint8_t> emptyData;
        String jsonData = deviceStatusToJson(deletion.address, 
                        DeviceOnlineState::PENDING_DELETION, deletion.deviceTypeIndex, emptyData, 0);
        if (jsonData.length() > 0)
        {
            jsonStr += (jsonStr.length() == 0 ? "{" : ",") + jsonData;
        }
    }

    return jsonStr.length() == 0 ? "{}" : jsonStr + "}";
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get queued device data in binary format
/// @param connMode connection mode (inc bus number)
/// @return Binary data vector
std::vector<uint8_t> DeviceIdentMgr::getQueuedDeviceDataBinary(uint32_t connMode)
{
    // Return buffer
    std::vector<uint8_t> binData;

    // Get list of all bus element addresses
    std::vector<BusElemAddrType> addresses;
    _busStatusMgr.getBusElemAddresses(addresses, false);
    for (auto address : addresses)
    {
        // Get bus status for each address with per-sample lengths
        DeviceOnlineState onlineState = DeviceOnlineState::OFFLINE;
        uint16_t deviceTypeIndex = 0;
        std::vector<uint8_t> sampleData;
        std::vector<uint16_t> sampleLengths;
        uint32_t numSamples = _busStatusMgr.getBusElemPollResponsesWithLengths(
            address, onlineState, deviceTypeIndex,
            sampleData, sampleLengths, 0);

        // Skip unidentified devices or devices with no data
        if (deviceTypeIndex == DEVICE_TYPE_INDEX_INVALID || numSamples == 0)
            continue;

        // Build length-prefixed sample payloads (samples over 255 bytes use the long-sample
        // escape). A record's length is 16 bits, so a run of samples that would overflow one
        // record continues in another record for the same device (with its own sequence number)
        std::vector<uint8_t> payload;
        uint32_t offset = 0;
        for (uint32_t i = 0; i < numSamples; i++) {
            uint16_t len = sampleLengths[i];
            if ((payload.size() > 0) &&
                (payload.size() + RaftDevice::lengthPrefixedSampleSize(len) > RaftDevice::DEVBIN_MAX_RECORD_PAYLOAD_LEN))
            {
                uint8_t seqNum = _busStatusMgr.getAndIncrementDeviceSeqCounter(address);
                RaftDevice::genBinaryDeviceRecord(binData, connMode, address, deviceTypeIndex, onlineState, seqNum, payload);
                payload.clear();
            }
            RaftDevice::appendLengthPrefixedSample(payload, sampleData.data() + offset, len);
            offset += len;
        }

        // Generate binary device record with pre-formatted length-prefixed payload
        if (payload.size() > 0)
        {
            uint8_t seqNum = _busStatusMgr.getAndIncrementDeviceSeqCounter(address);
            RaftDevice::genBinaryDeviceRecord(binData, connMode, address, deviceTypeIndex, onlineState, seqNum, payload);
        }
    }

    // Add pending deletion notices (devices that have been removed)
    std::vector<BusStatusMgr::DeletionNotice> deletions;
    _busStatusMgr.getPendingDeletions(deletions);
    for (const auto& deletion : deletions)
    {
        // Generate deletion notice with empty data and PENDING_DELETION state
        std::vector<uint8_t> emptyData;
        RaftDevice::genBinaryDeviceRecord(binData, connMode, deletion.address, deletion.deviceTypeIndex, 
                        DeviceOnlineState::PENDING_DELETION, 0, emptyData);
    }

    // Return binary data
    return binData;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get decoded poll responses
/// @param address address of device to get data from
/// @param pStructOut pointer to structure (or array of structures) to receive decoded data
/// @param structOutSize size of structure (in bytes) to receive decoded data
/// @param maxRecCount maximum number of records to decode
/// @param decodeState decode state for this device
/// @return number of records decoded
/// @note the pStructOut should generally point to structures of the correct type for the device data and the
///       decodeState should be maintained between calls for the same device
uint32_t DeviceIdentMgr::getDecodedPollResponses(BusElemAddrType address, 
                void* pStructOut, uint32_t structOutSize, 
                uint16_t maxRecCount, RaftBusDeviceDecodeState& decodeState) const
{
    // Get poll result for each address
    DeviceOnlineState onlineState = DeviceOnlineState::OFFLINE;
    uint16_t deviceTypeIndex = 0;
    std::vector<uint8_t> devicePollResponseData;
    uint32_t responseSize = 0;
    _busStatusMgr.getBusElemPollResponses(address, onlineState, deviceTypeIndex, devicePollResponseData, responseSize, 0);

#ifdef DEBUG_GET_DECODED_POLL_RESPONSES
    LOG_I(MODULE_PREFIX, "getDecodedPollResponses address %s onlineState %s deviceTypeIndex %d responseSize %d",
                BusI2CAddrAndSlot::toString(address).c_str(),
                BusAddrStatus::getOnlineStateStr(onlineState), deviceTypeIndex, responseSize);
#endif

    // Decode the poll response
    return decodePollResponses(deviceTypeIndex, devicePollResponseData.data(), responseSize, 
                pStructOut, structOutSize, 
                maxRecCount, decodeState);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get latest decoded poll response (non-destructive peek at most recent value)
/// @param address address of device to get data from
/// @param pStructOut pointer to structure to receive decoded data
/// @param structOutSize size of structure (in bytes) to receive decoded data
/// @param decodeState decode state for this device
/// @return true if a value was successfully decoded
bool DeviceIdentMgr::getLatestDecodedPollResponse(BusElemAddrType address,
                void* pStructOut, uint32_t structOutSize,
                RaftBusDeviceDecodeState& decodeState) const
{
    return decodeLatestPollResponse(address, pStructOut, structOutSize, decodeState, false);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get the most recent decoded poll response whether or not it has been read before
/// @param address address of device to get data from
/// @param pStructOut pointer to structure to receive decoded data
/// @param structOutSize size of structure (in bytes) to receive decoded data
/// @param decodeState decode state for this device
/// @return true if a value was successfully decoded
bool DeviceIdentMgr::peekLatestDecodedPollResponse(BusElemAddrType address,
                void* pStructOut, uint32_t structOutSize,
                RaftBusDeviceDecodeState& decodeState) const
{
    return decodeLatestPollResponse(address, pStructOut, structOutSize, decodeState, true);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Decode the latest poll response (does not drain the stored results)
/// @param peek true for the latest response regardless, false for one new since the last call
/// @return true if a value was successfully decoded
bool DeviceIdentMgr::decodeLatestPollResponse(BusElemAddrType address,
                void* pStructOut, uint32_t structOutSize,
                RaftBusDeviceDecodeState& decodeState, bool peek) const
{
    DeviceOnlineState onlineState = DeviceOnlineState::OFFLINE;
    uint16_t deviceTypeIndex = 0;
    uint64_t dataTimeUs = 0;
    std::vector<uint8_t> latestData;
    if (!_busStatusMgr.getBusElemLatestPollResponse(address, onlineState, deviceTypeIndex, dataTimeUs, latestData, peek))
        return false;

    if (latestData.empty())
        return false;

    // Decode single response
    uint32_t numDecoded = decodePollResponses(deviceTypeIndex, latestData.data(), latestData.size(),
                pStructOut, structOutSize, 1, decodeState);
    return numDecoded > 0;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Get debug JSON
/// @return JSON string
String DeviceIdentMgr::getDebugJSON(bool includeBraces) const
{
    return _busStatusMgr.getDebugJSON(includeBraces);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Decode one or more poll responses for a device
/// @param deviceTypeIndex index of device type
/// @param pPollBuf buffer containing poll responses
/// @param pollBufLen length of poll response buffer
/// @param pStructOut pointer to structure (or array of structures) to receive decoded data
/// @param structOutSize size of structure (in bytes) to receive decoded data (includes timestamp)
/// @param maxRecCount maximum number of records to decode
/// @return number of records decoded
uint32_t DeviceIdentMgr::decodePollResponses(uint16_t deviceTypeIndex, 
            const uint8_t* pPollBuf, uint32_t pollBufLen, 
            void* pStructOut, uint32_t structOutSize, 
            uint16_t maxRecCount, RaftBusDeviceDecodeState& decodeState) const
{
    // Decode function of the device type (no copy of the record)
    DeviceTypeRecordDecodeFn decodeFn = deviceTypeRecords.getPollDecodeFn(deviceTypeIndex);
    if (!decodeFn)
        return 0;
    return decodeFn(pPollBuf, pollBufLen, pStructOut, structOutSize, maxRecCount, decodeState);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Send command to device on bus
/// @param cmdJSON Command JSON string
/// @param respMsg (out) response message from the device
/// @return Result code
/// @note The JSON string should include:
///       - "hexWr": hex string of data to write to the device
///       - "numToRd": number of bytes to read from the device (optional)
RaftRetCode DeviceIdentMgr::sendCmdToDevice(RaftDeviceID deviceID, const char* cmdJSON, String* respMsg)
{
    // Create element request
    HWElemReq hwElemReq = cmdJSONToHWElemReq(cmdJSON);

    // Form bus request
    BusRequestInfo busReqInfo("", deviceID.getAddress());

    // The completion callback for a (non-poll) request is delivered from BusAccessor::loop() which runs on
    // the main task. So if this function is itself called on the main task (which is the case for API
    // handlers) the callback cannot be delivered while waiting here - waiting would simply stall the main
    // loop for the full timeout and then report failure even though the command is sent by the bus task.
    // Hence only wait for completion when called from a task other than the main task.
    bool waitForCompletion = (_busReqEnqueueFn != nullptr) && !RaftThread_isMainTask();

    // Use a shared completion block so the request callback can safely outlive
    // this stack frame even if we time out waiting for the bus worker.
    struct CmdCompletion
    {
        SemaphoreHandle_t sem = nullptr;
        std::atomic<RaftRetCode> rslt{RAFT_BUS_PENDING};
        CmdCompletion() { sem = xSemaphoreCreateBinary(); }
        ~CmdCompletion() { if (sem) vSemaphoreDelete(sem); }
    };
    std::shared_ptr<CmdCompletion> completion;
    if (waitForCompletion)
    {
        completion = std::make_shared<CmdCompletion>();
        busReqInfo.set(BUS_REQ_TYPE_STD, hwElemReq, 0,
                [completion](void* /*pCallbackData*/, BusRequestResult& reqResult)
                    {
                        if (completion)
                        {
                            // Write the result before signalling
                            completion->rslt = reqResult.getResult();
                            if (completion->sem)
                                xSemaphoreGive(completion->sem);
                        }
                    },
                nullptr);
    }
    else
    {
        busReqInfo.set(BUS_REQ_TYPE_STD, hwElemReq, 0, nullptr, nullptr);
    }

    // Make the request - route via the bus worker queue so that all
    // bus access (including mux switching) happens on the worker task and is
    // serialized with polling/scanning. If the request cannot be queued (queue full or
    // queue busy) then fail with a busy result - there is NO fallback to performing the
    // transaction inline on the calling task. The direct async path is only used if no
    // enqueue function has been provided (e.g. legacy/unit tests).
    RaftRetCode rslt = RAFT_INVALID_DATA;
    bool cmdQueuedOnly = false;
    if (_busReqEnqueueFn != nullptr)
    {
        if (!_busReqEnqueueFn(busReqInfo))
        {
            rslt = RAFT_BUSY;
        }
        else if (!waitForCompletion)
        {
            // Queued for the bus worker - result is not known at this point
            rslt = RAFT_OK;
            cmdQueuedOnly = true;
        }
        else
        {
            // Wait for the worker to process the request and the main task to fire our callback
            const TickType_t waitTicks = pdMS_TO_TICKS(500);
            if (completion->sem && xSemaphoreTake(completion->sem, waitTicks) == pdTRUE)
            {
                rslt = completion->rslt;
            }
            else
            {
                rslt = RAFT_BUS_SW_TIME_OUT;
            }
        }
    }
    else if (_busReqAsyncFn != nullptr)
    {
        rslt = _busReqAsyncFn(&busReqInfo, 0);
    }
    else
    {
        rslt = RAFT_BUS_NOT_INIT;
    }

    if (respMsg)
    {
        *respMsg = (rslt == RAFT_OK) ? (cmdQueuedOnly ? "Command queued" : "Command sent")
                                     : ((rslt == RAFT_BUSY) ? "Bus request queue busy"
                                     : (_busReqEnqueueFn || _busReqAsyncFn ? "Failed to send command"
                                                                            : "Bus not initialised"));
    }

#ifdef DEBUG_MAKE_BUS_REQUEST_VERBOSE
    String outStr;
    Raft::getHexStrFromBytes(hwElemReq._writeData.data(),
                hwElemReq._writeData.size() > 16 ? 16 : hwElemReq._writeData.size(),
                outStr);
    LOG_I(MODULE_PREFIX, "sendCmdToDevice resp %s deviceId %s len %d data %s ...",
                    respMsg ? respMsg->c_str() : String(Raft::getRetCodeStr(rslt)).c_str(),
                    deviceID.toString().c_str(),
                    hwElemReq._writeData.size(),
                    outStr.c_str());
#endif

    return rslt;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Send command to device on bus and get the result later (does not wait)
/// @param deviceID ID of the device
/// @param cmdJSON Command JSON string (as for sendCmdToDevice())
/// @param resultFn function called with the result (on the main task) - only called if RAFT_OK is returned
/// @return RAFT_OK if queued, RAFT_BUSY if the bus request queue is full, RAFT_NOT_IMPLEMENTED if there is
///         no bus request queue
RaftRetCode DeviceIdentMgr::sendCmdToDeviceAsync(RaftDeviceID deviceID, const char* cmdJSON, RaftBusCmdResultFn resultFn)
{
    // Requests are only sent via the bus worker queue - the result callback for a (non-poll) request is then 
    // delivered from BusAccessor::loop() on the main task and is made for every outcome (including bus errors)
    if (_busReqEnqueueFn == nullptr)
        return RAFT_NOT_IMPLEMENTED;

    // Form bus request
    HWElemReq hwElemReq = cmdJSONToHWElemReq(cmdJSON);
    BusRequestInfo busReqInfo("", deviceID.getAddress());
    busReqInfo.set(BUS_REQ_TYPE_STD, hwElemReq, 0,
            [resultFn](void* /*pCallbackData*/, BusRequestResult& reqResult)
                {
                    if (resultFn)
                        resultFn(reqResult);
                },
            nullptr);

    // Queue the request
    return _busReqEnqueueFn(busReqInfo) ? RAFT_OK : RAFT_BUSY;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Form a bus element request from a command JSON string
/// @param cmdJSON Command JSON string with "hexWr" (hex data to write) and optional "numToRd" (bytes to read)
/// @return element request
HWElemReq DeviceIdentMgr::cmdJSONToHWElemReq(const char* cmdJSON)
{
    // Get command and parameters from JSON
    RaftJson cmdJsonObj(cmdJSON);
    String hexWriteData = cmdJsonObj.getString("hexWr", "");
    int numBytesToRead = cmdJsonObj.getLong("numToRd", 0);

    // Convert hex write data to binary
    uint32_t numBytesToWrite = hexWriteData.length() / 2;
    std::vector<uint8_t> writeVec;
    writeVec.resize(numBytesToWrite);
    uint32_t writeBytesLen = Raft::getBytesFromHexStr(hexWriteData.c_str(), writeVec.data(), numBytesToWrite);
    writeVec.resize(writeBytesLen);

    // Create element request
    static const uint32_t CMDID_CMDRAW = 100;
    return HWElemReq{writeVec, numBytesToRead, CMDID_CMDRAW, "cmdraw", 0};
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Register a handler invoked for newly-detected devices that no device type record matched
/// @param newDeviceIdentFn handler function (nullptr to clear)
/// @param pCtx opaque context passed to the handler
void DeviceIdentMgr::registerNewDeviceIdentHandler(RaftNewDeviceIdentFn newDeviceIdentFn, void* pCtx)
{
    // Store {fn, ctx} as one unit under the mutex
    if (!RaftMutex_lock(_handlerMutex, RAFT_MUTEX_WAIT_FOREVER))
    {
        LOG_E(MODULE_PREFIX, "registerNewDeviceIdentHandler failed to obtain mutex");
        return;
    }
    _newDeviceIdentFn = newDeviceIdentFn;
    _newDeviceIdentCtx = pCtx;
    if (newDeviceIdentFn)
        _hookEpoch++;
    _hookClaimsEpoch++;
    uint32_t callCountAtReg = _newDeviceIdentCallCount;
    RaftMutex_unlock(_handlerMutex);

    // Wait for any call to the previous handler to complete
    waitForHandlerQuiescence(_newDeviceIdentInProgress, _newDeviceIdentCallCount, callCountAtReg, "newDeviceIdent");
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Set the addresses the new-device identification handler handles (empty: every address)
void DeviceIdentMgr::setNewDeviceIdentAddrRanges(const std::vector<std::pair<uint32_t, uint32_t>>& ranges)
{
    if (!RaftMutex_lock(_handlerMutex, RAFT_MUTEX_WAIT_FOREVER))
        return;
    _hookAddrRanges = ranges;
    RaftMutex_unlock(_handlerMutex);
    // Addresses now handled are offered to the handler at once, and the scanner rebuilds its lists
    _hookEpoch++;
    _hookClaimsEpoch++;
    LOG_I(MODULE_PREFIX, "setNewDeviceIdentAddrRanges %d range(s)", (int)ranges.size());
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Check whether the identification handler could claim a device at an address
bool DeviceIdentMgr::isAddrClaimedByHook(uint32_t i2cAddr) const
{
    if (!RaftMutex_lock(_handlerMutex, RAFT_MUTEX_WAIT_FOREVER))
        return true;
    bool claimed = (_newDeviceIdentFn != nullptr) && isAddrInHookRangesLocked(i2cAddr);
    RaftMutex_unlock(_handlerMutex);
    return claimed;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Check whether an address is in the handler's ranges (all addresses if none are set) - mutex held
bool DeviceIdentMgr::isAddrInHookRangesLocked(uint32_t i2cAddr) const
{
    if (_hookAddrRanges.empty())
        return true;
    for (const auto& range : _hookAddrRanges)
        if ((i2cAddr >= range.first) && (i2cAddr <= range.second))
            return true;
    return false;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Register a handler serviced on the bus task, for application work that drives the bus itself
/// @param busTaskServiceFn handler function (nullptr to clear)
/// @param pCtx opaque context passed to the handler
void DeviceIdentMgr::registerBusTaskServiceHandler(RaftBusTaskServiceFn busTaskServiceFn, void* pCtx)
{
    // Store {fn, ctx} as one unit under the mutex
    if (!RaftMutex_lock(_handlerMutex, RAFT_MUTEX_WAIT_FOREVER))
    {
        LOG_E(MODULE_PREFIX, "registerBusTaskServiceHandler failed to obtain mutex");
        return;
    }
    _busTaskServiceFn = busTaskServiceFn;
    _busTaskServiceCtx = pCtx;
    uint32_t callCountAtReg = _busTaskServiceCallCount;
    RaftMutex_unlock(_handlerMutex);

    // Wait for any call to the previous handler to complete
    waitForHandlerQuiescence(_busTaskServiceInProgress, _busTaskServiceCallCount, callCountAtReg, "busTaskService");
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Service any registered bus-task handler (called from the bus worker loop only)
/// @return true if the handler is mid-operation
bool DeviceIdentMgr::serviceBusTaskHandler()
{
    // Copy out {fn, ctx} under the mutex
    if (!RaftMutex_lock(_handlerMutex, RAFT_MUTEX_WAIT_FOREVER))
        return false;
    RaftBusTaskServiceFn busTaskServiceFn = _busTaskServiceFn;
    void* pBusTaskServiceCtx = _busTaskServiceCtx;
    if (busTaskServiceFn)
    {
        _handlerCallingTask = (void*)xTaskGetCurrentTaskHandle();
        _busTaskServiceCallCount++;
        _busTaskServiceInProgress = true;
    }
    RaftMutex_unlock(_handlerMutex);

    // Call outside the mutex
    if (!busTaskServiceFn)
        return false;
    bool isBusy = busTaskServiceFn(pBusTaskServiceCtx);
    _busTaskServiceInProgress = false;
    return isBusy;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Wait (bounded) for a handler call in progress on another task to complete
/// @param inProgressFlag in-progress flag for the handler
/// @param callCount count of calls made to the handler
/// @param callCountAtReg value of callCount (read under the mutex) when the handler was changed
/// @param handlerName name for logging
/// @note Only a call which started before the handler was changed can be using the old {fn, ctx} so the
///       wait ends when that call completes (or a later call has started)
void DeviceIdentMgr::waitForHandlerQuiescence(const std::atomic<bool>& inProgressFlag,
            const std::atomic<uint32_t>& callCount, uint32_t callCountAtReg, const char* handlerName)
{
    // No wait if called from the handler itself (on the bus task)
    if (_handlerCallingTask == (void*)xTaskGetCurrentTaskHandle())
        return;
    uint32_t waitStartMs = millis();
    while (inProgressFlag && (callCount == callCountAtReg))
    {
        if (Raft::isTimeout(millis(), waitStartMs, HANDLER_QUIESCE_MAX_MS))
        {
            LOG_W(MODULE_PREFIX, "%s handler still in progress after %dms", handlerName, (int)HANDLER_QUIESCE_MAX_MS);
            break;
        }
        vTaskDelay(1);
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// @brief Callback for command result reports
/// @param reqResult Result of the bus request
void DeviceIdentMgr::cmdResultReportCallback(BusRequestResult &reqResult)
{
#ifdef DEBUG_CMD_RESULT_CALLBACK
    LOG_I(MODULE_PREFIX, "cmdResultReportCallback len %d", reqResult.getReadDataLen());
    Raft::logHexBuf(reqResult.getReadData(), reqResult.getReadDataLen(), MODULE_PREFIX, "cmdResultReportCallback");
#endif
}