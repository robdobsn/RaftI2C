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
/// @brief Identify device
/// @param address address of device
/// @param deviceStatus (out) device status
/// @note This is called from within the scanning code so the device should already be selected if it is on a bus extender, etc.
void DeviceIdentMgr::identifyDevice(BusElemAddrType address, DeviceStatus& deviceStatus)
{
    // Clear device status
    deviceStatus.clear();

    // Check if enabled
    if (!_isEnabled)
    {
#ifdef DEBUG_DEVICE_IDENT_MGR
        LOG_I(MODULE_PREFIX, "identifyDevice disabled");
#endif
        return;
    }

    // Identification has started: from now on a device may hold any device type index, so a record can no longer
    // replace a base record at its index
    deviceTypeRecords.markReady();

    // Get the raw I2C address (excluding slot number) and the device type records that can identify a
    // device there, in the order to try them
    uint32_t i2cAddr = BusI2CAddrAndSlot::getI2CAddr(address);
    std::vector<DeviceTypeIndexType> deviceTypesForAddr = deviceTypeRecords.getIdentCandidates(i2cAddr);

    // Address-based identification runs FIRST. Detection is a non-destructive read/compare, so a
    // device with a matching device-type record is identified without disturbing it. Only if
    // nothing here matches do we fall back to the delegated new-device hook below (which probes
    // with a framed vendor command that can be destructive to third-party sensors).
#ifdef DEBUG_DEVICE_IDENT_MGR
    bool anyDeviceIdentified = false;
#endif
    bool identified = false;
    bool busErrorDuringDetection = false;
    for (const auto& deviceTypeIndex : deviceTypesForAddr)
    {
        // Get JSON definition for device
        const DeviceTypeRecord* pDevTypeRec = deviceTypeRecords.getRecord(deviceTypeIndex);
        if (!pDevTypeRec)
            continue;

#ifdef DEBUG_DEVICE_IDENT_MGR
        LOG_I(MODULE_PREFIX, "identifyDevice potential deviceType %s address %s", 
                    pDevTypeRec->deviceType ? pDevTypeRec->deviceType : "NO NAME", BusI2CAddrAndSlot::toString(address).c_str());
#endif

        // Check if the detection value(s) match responses from the device. A bus error (other than a NACK, which
        // is how many devices reject a command meant for another type) ends the attempt: trying weaker records,
        // or the identification hook, on a device that couldn't be read would give a wrong or harmful result.
        // The scanner tries again later
        DetectionResult detectionResult = checkDeviceTypeMatch(address, deviceTypeIndex);
        if (detectionResult == DetectionResult::BusError)
        {
            busErrorDuringDetection = true;
            LOG_W(MODULE_PREFIX, "identifyDevice bus error detecting %s at %s - identification deferred",
                        pDevTypeRec->deviceType ? pDevTypeRec->deviceType : "?", BusI2CAddrAndSlot::toString(address).c_str());
            break;
        }
        if (detectionResult == DetectionResult::Match)
        {
#ifdef DEBUG_DEVICE_IDENT_MGR_DETAIL
            LOG_I(MODULE_PREFIX, "identifyDevice FOUND %s", pDevTypeRec->devInfoJson ? pDevTypeRec->devInfoJson : "NO INFO");
#endif
#ifdef INFO_NEW_DEVICE_IDENTIFIED
            LOG_I(MODULE_PREFIX, "identifyDevice new device %s at address %s", 
                    pDevTypeRec->deviceType ? pDevTypeRec->deviceType : "NO NAME", 
                    BusI2CAddrAndSlot::toString(address).c_str());
#endif

            // Initialise the device if required
            processDeviceInit(address, deviceTypeIndex);

            // Set device type index
            deviceStatus.deviceTypeIndex = deviceTypeIndex;

            // Get polling info
            deviceTypeRecords.getPollInfo(address, deviceTypeIndex, deviceStatus.deviceIdentPolling);

            // Set polling results size
            auto pDataAggregator = std::make_shared<PollDataAggregator>(
                    deviceStatus.deviceIdentPolling.numPollResultsToStore,
                    deviceStatus.deviceIdentPolling.pollResultSizeIncTimestamp);
            deviceStatus.setAndOwnPollDataAggregator(pDataAggregator);

#ifdef DEBUG_HANDLE_BUS_DEVICE_INFO
            LOG_I(MODULE_PREFIX, "setBusElemDevInfo address %s numPollResToStore %d pollResSizeIncTimestamp %d", 
                    BusI2CAddrAndSlot::toString(address).c_str(),
                    deviceStatus.deviceIdentPolling.numPollResultsToStore,
                    deviceStatus.deviceIdentPolling.pollResultSizeIncTimestamp);
#endif
            // Identified - stop searching
            identified = true;
#ifdef DEBUG_DEVICE_IDENT_MGR
            anyDeviceIdentified = true;
#endif
            break;
        }
        else
        {
#ifdef DEBUG_DEVICE_IDENT_MGR_DETAIL
            LOG_I(MODULE_PREFIX, "identifyDevice CHECK FAILED %s", pDevTypeRec->devInfoJson ? pDevTypeRec->devInfoJson : "NO INFO");
#endif
        }
    }
#ifdef DEBUG_DEVICE_IDENT_MGR
    if (!anyDeviceIdentified)
    {
        LOG_I(MODULE_PREFIX, "identifyDevice NO MATCH for address %s numDeviceTypesForAddr %d", 
                    BusI2CAddrAndSlot::toString(address).c_str(), deviceTypesForAddr.size());
    }
#endif

    // Fallback: give a registered new-device identification handler (device-agnostic delegation
    // hook, e.g. the RSAO WHOAMI probe) a chance to claim the device, but ONLY when no standard
    // device-type record matched above. The handler probes with a framed vendor command that can
    // be destructive to third-party sensors (e.g. Sensirion devices interpret the probe bytes as
    // commands and stop measuring), so it must run last - after all non-destructive address-based
    // identification. Dynamically-addressed RSAO devices are excludeFromAddrMap so they never match
    // above and are correctly handled here.
    // The handler {fn, ctx} pair is copied out under the handler mutex (it may be registered/cleared from
    // another task) and called outside it
    RaftNewDeviceIdentFn newDeviceIdentFn = nullptr;
    void* pNewDeviceIdentCtx = nullptr;
    // The hook isn't run at an address claimed by a record whose policy is DISABLED_CLAIM
    bool hookBlocked = !identified && deviceTypeRecords.isAddrHookBlocked(i2cAddr);
    if (!identified && !busErrorDuringDetection && !hookBlocked && RaftMutex_lock(_handlerMutex, RAFT_MUTEX_WAIT_FOREVER))
    {
        newDeviceIdentFn = _newDeviceIdentFn;
        pNewDeviceIdentCtx = _newDeviceIdentCtx;
        if (newDeviceIdentFn)
        {
            _handlerCallingTask = (void*)xTaskGetCurrentTaskHandle();
            _newDeviceIdentCallCount++;
            _newDeviceIdentInProgress = true;
        }
        RaftMutex_unlock(_handlerMutex);
    }
    if (!identified && newDeviceIdentFn)
    {
        RaftDeviceIdentVerdict verdict = newDeviceIdentFn(address, deviceStatus, pNewDeviceIdentCtx);
        _newDeviceIdentInProgress = false;

        // The handler may have performed bus transactions (e.g. a synchronous probe) that reset
        // the multiplexer slot selection. The scanner selected this device's slot before calling
        // identifyDevice and device init relies on it remaining selected, so re-select it here
        // before continuing.
        if (_reselectSlotFn)
            _reselectSlotFn(BusI2CAddrAndSlot::getSlotNum(address));

        if (verdict == RaftDeviceIdentVerdict::Handled)
        {
            // The handler has identified the device and set deviceStatus.deviceTypeIndex. Complete
            // the device status here (init + polling + data aggregator) from that index using the
            // standard device-type records, so the handler stays device-agnostic and the existing
            // polling/decode pipeline is reused unchanged.
            // A type that is disabled isn't used, whoever identified it
            DeviceTypeRecord devTypeRec;
            if (deviceTypeRecords.getDeviceInfo(deviceStatus.deviceTypeIndex, devTypeRec) &&
                    (deviceTypeRecords.getPolicy(deviceStatus.deviceTypeIndex) != DeviceTypeRecord::POLICY_ENABLED))
            {
                LOG_W(MODULE_PREFIX, "identifyDevice handler claimed address %s as %s, which is disabled - not used",
                        BusI2CAddrAndSlot::toString(address).c_str(), devTypeRec.deviceType ? devTypeRec.deviceType : "?");
                deviceStatus.clear();
                return;
            }
            if (deviceTypeRecords.getDeviceInfo(deviceStatus.deviceTypeIndex, devTypeRec))
            {
                processDeviceInit(address, deviceStatus.deviceTypeIndex);
                deviceTypeRecords.getPollInfo(address, deviceStatus.deviceTypeIndex, deviceStatus.deviceIdentPolling);
                auto pDataAggregator = std::make_shared<PollDataAggregator>(
                        deviceStatus.deviceIdentPolling.numPollResultsToStore,
                        deviceStatus.deviceIdentPolling.pollResultSizeIncTimestamp);
                deviceStatus.setAndOwnPollDataAggregator(pDataAggregator);
#ifdef INFO_NEW_DEVICE_IDENTIFIED
                LOG_I(MODULE_PREFIX, "identifyDevice handler claimed address %s as %s (typeIdx %d)",
                        BusI2CAddrAndSlot::toString(address).c_str(),
                        devTypeRec.deviceType ? devTypeRec.deviceType : "NO NAME",
                        deviceStatus.deviceTypeIndex);
#endif
                return;
            }
            // Handler returned Handled but the device-type index is not valid: treat as unidentified.
            LOG_W(MODULE_PREFIX, "identifyDevice handler claimed address %s but device-type index %d invalid",
                    BusI2CAddrAndSlot::toString(address).c_str(), deviceStatus.deviceTypeIndex);
            deviceStatus.clear();
            return;
        }
        if (verdict == RaftDeviceIdentVerdict::Deferred)
        {
            // Not ready to identify yet; leave unidentified and unpolled. identifyDevice is
            // called on an offline->online transition and also periodically (BusScanner retry)
            // for online devices that are still unidentified, so this will be retried.
#ifdef DEBUG_DEVICE_IDENT_MGR
            LOG_I(MODULE_PREFIX, "identifyDevice handler DEFERRED address %s - left unidentified and unpolled",
                    BusI2CAddrAndSlot::toString(address).c_str());
#endif
            deviceStatus.clear();
            return;
        }
        // NotMine -> device remains unidentified
#ifdef DEBUG_DEVICE_IDENT_MGR
        LOG_I(MODULE_PREFIX, "identifyDevice handler NOT MINE address %s - remains unidentified",
                BusI2CAddrAndSlot::toString(address).c_str());
#endif
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Access device and check response
///////////////////////////////////////////////////////////////////////////////////////////////////////////////

DeviceIdentMgr::DetectionResult DeviceIdentMgr::checkDeviceTypeMatch(BusElemAddrType address, DeviceTypeIndexType deviceTypeIndex)
{
    // Get the detection records - a record with an invalid detection value never matches
    std::vector<DeviceTypeRecords::DeviceDetectionRec> detectionRecs;
    if (!deviceTypeRecords.getDetectionRecs(deviceTypeIndex, detectionRecs))
        return DetectionResult::NoMatch;

    // Each step must match; detection stops at the first that doesn't
    for (const auto& detectionRec : detectionRecs)
    {
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

        if (detectionRec.pauseAfterSendMs > 0)
            delay(detectionRec.pauseAfterSendMs);
    }
    return DetectionResult::Match;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Process initialisation of a device
///////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool DeviceIdentMgr::processDeviceInit(BusElemAddrType address, DeviceTypeIndexType deviceTypeIndex)
{
    // Get initialisation bus requests
    std::vector<BusRequestInfo> initBusRequests;
    deviceTypeRecords.getInitBusRequests(address, deviceTypeIndex, initBusRequests);

#ifdef DEBUG_DEVICE_IDENT_MGR
    LOG_I(MODULE_PREFIX, "processDeviceInit address %s numInitBusRequests %d", 
                BusI2CAddrAndSlot::toString(address).c_str(), initBusRequests.size());
#endif

    // Initialise the device
    for (auto& initBusRequest : initBusRequests)
    {
        std::vector<uint8_t> readData;
        BusRequestInfo reqRec(initBusRequest);
        if (_busReqSyncFn != nullptr)
            _busReqSyncFn(&reqRec, &readData);

        // Check for bar-access time after each request
        if (initBusRequest.getBarAccessForMsAfterSend() > 0)
            delay(initBusRequest.getBarAccessForMsAfterSend());
    }

    return true;
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
    uint32_t callCountAtReg = _newDeviceIdentCallCount;
    RaftMutex_unlock(_handlerMutex);

    // Wait for any call to the previous handler to complete
    waitForHandlerQuiescence(_newDeviceIdentInProgress, _newDeviceIdentCallCount, callCountAtReg, "newDeviceIdent");
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