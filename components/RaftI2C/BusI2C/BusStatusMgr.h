/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Bus Status Manager
//
// Rob Dobson 2020-2024
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <stdint.h>
#include "RaftThreading.h"
#include "RaftBus.h"
#include "RaftUtils.h"
#include "DeviceStatus.h"
#include "DevicePollScheduler.h"
#include "BusAddrRecord.h"
#include <list>
#include <atomic>

class BusStatusMgr {

public:
    /// @brief Constructor
    /// @param raftBus 
    BusStatusMgr(RaftBus& raftBus);

    /// @brief Destructor
    ~BusStatusMgr();

    /// @brief Setup
    /// @param config Configuration (JSON)
    void setup(const RaftJsonIF& config);

    /// @brief Loop (called frequently)
    /// @param hwIsOperatingOk true if the hardware is operating correctly
    void loop(bool hwIsOperatingOk);

    /// @brief Check if bus is operating ok
    /// @return true if bus is operating ok
    BusOperationStatus isOperatingOk() const
    {
        return _busOperationStatus;
    }

    /// @brief Bus element access barring
    /// @param timeNowMs current time in ms (used to calculate if barring is still in effect)
    /// @param address address of the bus element
    /// @param barAccessAfterSendMs time to bar access after send in ms
    void barElemAccessSet(uint32_t timeNowMs, BusElemAddrType address, uint32_t barAccessAfterSendMs);

    /// @brief Check if bus element access is barred
    /// @param timeNowMs current time in ms (used to calculate if barring is still in effect)
    /// @param address address of the bus element
    /// @return true if access is barred
    bool barElemAccessGet(uint32_t timeNowMs, BusElemAddrType address);

    /// @brief Check if a bus element is online
    /// @param address address of the bus element
    /// @return true if the bus element is online
    BusOperationStatus isElemOnline(BusElemAddrType address) const;

    /// @brief Update bus element state
    /// @param address address of the bus element
    /// @param elemResponding true if the element is responding
    /// @param isOnline (out) true if the element is online
    /// @return true if state has changed
    bool updateBusElemState(BusElemAddrType address, bool elemResponding, bool& isOnline);

    /// @brief Get count of address status records
    /// @return count of address status records
    uint32_t getAddrStatusCount() const;

    /// @brief Check if an address is being polled
    /// @param address address of the bus element
    /// @return true if the address is being polled
    bool isAddrBeingPolled(BusElemAddrType address) const;

    /// @brief Set bus element device status (which includes device type and can be empty) for an address
    /// @param address address of the bus element
    /// @param deviceStatus device status to set
    void setBusElemDeviceStatus(BusElemAddrType address, const DeviceStatus& deviceStatus);

    /// @brief Get device type index by address
    /// @param address address of the bus element
    /// @return device type index (DEVICE_TYPE_INDEX_INVALID if not known)
    uint16_t getDeviceTypeIndexByAddr(BusElemAddrType address) const;

    /// @brief Clear an element's identification so the scanner identifies it again
    /// @param address address (including slot)
    /// @note The element stays ONLINE - this says "what I concluded about this device is no
    ///       longer trustworthy", not "it has gone". Polling stops (there is no device type
    ///       to poll against) and BusScanner re-runs identification on its next sweep.
    ///       Used when sustained poll-CRC failure suggests a second device has appeared on
    ///       the address: identification is where that can actually be diagnosed, because
    ///       the serial read it performs is the only response that differs between two
    ///       devices of the same type.
    void clearDeviceIdentification(BusElemAddrType address);

    /// @brief Get pending ident poll (this is the poll of the device based on its identified type)
    /// @param timeNowUs current time in us
    /// @param pollInfo (out) device polling info
    /// @return true if there is a pending ident poll
    /// @note of the devices that are due, the one with the earliest due time is returned (on a tie, round robin)
    bool getPendingIdentPoll(uint64_t timeNowUs, DevicePollingInfo& pollInfo);

    /// @brief Get the earliest time any device poll (or poll step) is due
    /// @param timeNowUs current time in us
    /// @param dueUs (out) earliest due time (may be in the past)
    /// @return false if no device is polled
    bool getNextPollDueUs(uint64_t timeNowUs, uint64_t& dueUs);

    /// @brief Handle poll result
    /// @param nextReqIdx index of next request to store (0 = full poll, 1+ = partial poll)
    /// @param timeNowUs time in us (passed in to aid testing)
    /// @param address address
    /// @param pollResultData poll result data
    /// @param pPollInfo pointer to device polling info (maybe nullptr)
    /// @param pauseAfterSendMs pause after send in ms
    /// @return true if result stored
    bool handlePollResult(uint32_t nextReqIdx, uint64_t timeNowUs, BusElemAddrType address, 
                    const std::vector<uint8_t>& pollResultData, const DevicePollingInfo* pPollInfo,
                    uint32_t pauseAfterSendMs);

    /// @brief Get latest timestamp of change to device info (online/offline, new data, etc)
    /// @param includeElemOnlineStatusChanges include changes in online status of elements
    /// @param includeDeviceDataUpdates include new data updates
    /// @return latest update time ms
    uint64_t getDeviceInfoTimestampMs(bool includeElemOnlineStatusChanges, bool includeDeviceDataUpdates) const;

    /// @brief Return addresses of devices attached to the bus
    /// @param addresses - vector to store the addresses of devices
    /// @param onlyAddressesWithIdentPollResponses - true to only return addresses with ident poll responses
    /// @return true if there are any ident poll responses available
    bool getBusElemAddresses(std::vector<uint32_t>& addresses, bool onlyAddressesWithIdentPollResponses) const;

    /// @brief Get bus element poll responses for a specific address
    /// @param address - address of device to get responses for
    /// @param onlineState - (out) device online state
    /// @param deviceTypeIndex - (out) device type index
    /// @param devicePollResponseData - (out) vector to store the device poll response data
    /// @param responseSize - (out) size of the response data
    /// @param maxResponsesToReturn - maximum number of responses to return (0 for no limit)
    /// @return number of responses returned
    uint32_t getBusElemPollResponses(uint32_t address, DeviceOnlineState& onlineState, uint16_t& deviceTypeIndex, 
                std::vector<uint8_t>& devicePollResponseData, 
                uint32_t& responseSize, uint32_t maxResponsesToReturn);

    /// @brief Get bus element poll responses with per-sample actual lengths
    /// @param address - address of device to get responses for
    /// @param onlineState - (out) device online state
    /// @param deviceTypeIndex - (out) device type index
    /// @param devicePollResponseData - (out) concatenated sample data (trimmed to actual lengths)
    /// @param sampleLengths - (out) actual length of each sample
    /// @param maxResponsesToReturn - maximum number of responses to return (0 for no limit)
    /// @return number of responses returned
    uint32_t getBusElemPollResponsesWithLengths(BusElemAddrType address,
                DeviceOnlineState& onlineState, uint16_t& deviceTypeIndex,
                std::vector<uint8_t>& devicePollResponseData,
                std::vector<uint16_t>& sampleLengths,
                uint32_t maxResponsesToReturn);

    /// @brief Get latest poll response for a bus element (does not drain the stored results)
    /// @param address - address of device to get response for
    /// @param onlineState - (out) device online state
    /// @param deviceTypeIndex - (out) device type index
    /// @param dataTimeUs - (out) timestamp of latest data in microseconds
    /// @param data - (out) latest poll response data
    /// @param peek - true to return the latest response whether or not it has been read before;
    ///               false to return it only if new since the previous (non-peek) call
    /// @return true if data was available
    bool getBusElemLatestPollResponse(BusElemAddrType address,
                DeviceOnlineState& onlineState, uint16_t& deviceTypeIndex,
                uint64_t& dataTimeUs, std::vector<uint8_t>& data, bool peek = false);

    /// @brief Get and increment per-device publish sequence counter
    /// @param address address of the bus element
    /// @return current sequence counter (pre-increment)
    uint8_t getAndIncrementDeviceSeqCounter(BusElemAddrType address);

    /// @brief Register for device data notifications
    /// @param addrAndSlot address
    /// @param dataChangeCB Callback for data change
    /// @param minTimeBetweenReportsMs Minimum time between reports (ms)
    /// @param pCallbackInfo Callback info (passed to the callback)
    void registerForDeviceData(BusElemAddrType addrAndSlot, RaftDeviceDataChangeCB dataChangeCB,
                uint32_t minTimeBetweenReportsMs, const void* pCallbackInfo);

    /// @brief Unregister for device data notifications for a specific address
    /// @param address address (including slot)
    /// @param pCallbackInfo Callback info that was passed when registering (identifies the subscriber)
    /// @return true if a matching registration was found (and disarmed)
    /// @note The callback and its info are disarmed in the address record under the mutex. If the callback is in
    ///       progress on another task (it is called from the bus task) this waits (bounded) for it to complete so
    ///       that on return the subscriber can safely be destroyed. May be called from within the callback itself
    ///       (in which case there is no wait).
    bool unregisterForDeviceData(BusElemAddrType address, const void* pCallbackInfo);

    /// @brief Unregister for device data notifications on all addresses
    /// @param pCallbackInfo Callback info that was passed when registering (identifies the subscriber)
    /// @return number of registrations disarmed
    /// @note see unregisterForDeviceData(address, pCallbackInfo)
    uint32_t unregisterForDeviceDataAll(const void* pCallbackInfo);

    /// @brief Inform that the devices on a slot are going offline (e.g. the slot is being power cycled)
    /// @param slotNum slot number (0 for the main bus)
    void goingOfflineOnSlot(uint32_t slotNum);

    /// @brief Inform that the bus is stuck
    void informBusStuck();

    /// @brief Get addresses whose per-address state on the bus task (e.g. CRC trailer capability) should be
    ///        forgotten since the last call, because the record was deleted or its identification was cleared
    /// @param addrs (out) addresses
    /// @return false if some were missed (more than the list holds) - then forget the state of every address
    /// @note called from the bus task; cheap when there is nothing to report
    bool getForgottenAddresses(std::vector<BusElemAddrType>& addrs);

    /// @brief Get debug JSON
    /// @return JSON string
    String getDebugJSON(bool includeBraces) const;

    /// @brief Set device polling interval for a specific address
    /// @param address Composite address
    /// @param pollIntervalUs Poll interval in microseconds
    /// @return true if updated
    bool setDevicePollIntervalUs(BusElemAddrType address, uint32_t pollIntervalUs);

    /// @brief Get device polling interval for a specific address
    /// @param address Composite address
    /// @return Poll interval in microseconds (0 if not supported)
    uint64_t getDevicePollIntervalUs(BusElemAddrType address) const;

    /// @brief Set number of poll result samples to store for a specific address
    /// @param address Composite address
    /// @param numSamples Number of samples to store
    /// @return true if updated
    bool setDeviceNumSamples(BusElemAddrType address, uint32_t numSamples);

    /// @brief Get number of poll result samples stored for a specific address
    /// @param address Composite address
    /// @return Number of samples (0 if not supported)
    uint32_t getDeviceNumSamples(BusElemAddrType address) const;

    /// @brief Set bus frequency override for polling a specific address
    /// @param address Composite address
    /// @param busHz Bus frequency in Hz (0 = use bus default)
    /// @return true if updated
    bool setDevicePollBusHz(BusElemAddrType address, uint32_t busHz);

    /// @brief Get bus frequency override for polling a specific address
    /// @param address Composite address
    /// @return Bus frequency in Hz (0 = use bus default)
    uint32_t getDevicePollBusHz(BusElemAddrType address) const;

    /// @brief Set bus frequency slot mask for polling a specific address
    /// @param address Composite address
    /// @param slotMask Bitmask of slots where busHz applies (0 = all slots)
    /// @return true if updated
    bool setDevicePollBusHzSlotMask(BusElemAddrType address, uint64_t slotMask);

    /// @brief Get bus frequency slot mask for polling a specific address
    /// @param address Composite address
    /// @return Bitmask of slots where busHz applies (0 = all slots)
    uint64_t getDevicePollBusHzSlotMask(BusElemAddrType address) const;

    /// @brief Deletion notice - used to queue deletion messages for publishing
    struct DeletionNotice
    {
        BusElemAddrType address;
        uint16_t deviceTypeIndex;
    };

    /// @brief Get pending deletions and clear the queue
    /// @param deletions (out) vector to receive pending deletions
    void getPendingDeletions(std::vector<DeletionNotice>& deletions);

    /// @brief Get poll timing statistics for each polled device as JSON
    /// @return JSON object keyed by element address (hex), e.g. {"68":{"dtIdx":5,"cfgUs":50000,...}}
    String getPollTimingJson() const;

    /// @brief Record the bus time a device's poll (or poll step) took
    /// @param address address
    /// @param busTimeUs bus time in us
    void recordPollBusTimeUs(BusElemAddrType address, uint64_t busTimeUs);

    /// @brief Estimate the share of the bus time that polling online devices at their intervals needs
    /// @param defaultBusHz bus frequency for devices that don't set their own
    /// @return estimated load in percent (can exceed 100 when more polling is configured than the bus can do)
    /// @note uses each device's measured bus time per poll once it has been polled, else an estimate
    uint32_t getPollLoadPct(uint32_t defaultBusHz) const;

    /// @brief Estimate the bus time one poll of a device takes
    /// @param pollInfo device polling info
    /// @param busHz bus frequency
    /// @return estimated time in us
    static uint32_t estimatePollTimeUs(const DevicePollingInfo& pollInfo, uint32_t busHz);

    /// @brief Reset the poll timing statistics of all devices
    void resetPollTiming();
    // An element to identify, or an identified element to re-verify
    struct IdentWork
    {
        BusElemAddrType address = 0;
        bool reverify = false;
        bool reident = false;       // an identified device to identify again (keeping its type until the result)
        DeviceTypeIndexType deviceTypeIndex = DEVICE_TYPE_INDEX_INVALID;
    };

    /// @brief Mark the devices in a re-identification request (identified devices are identified again keeping
    ///        their type until the result is known; others are tried at once)
    /// @return number of devices marked
    uint32_t applyReIdentRequest(const BusReIdentRequest& request);

    /// @brief Set the outcome of re-identifying an identified device
    enum class ReIdentResult : uint8_t
    {
        SameType,       // the same type matched - nothing changes (not initialised again)
        NewType,        // another type matched (deviceStatus is its status) - reported as TypeChanged
        NoMatch,        // nothing matched - reported as Lost
        TryAgain,       // a bus error, or the hook deferred - the device keeps its type and is tried again later
    };
    void setReIdentOutcome(BusElemAddrType address, ReIdentResult result, const DeviceStatus& deviceStatus,
                uint32_t generation, uint32_t hookEpoch);

    /// @brief Get the next element due for identification (round robin)
    /// @param timeNowMs time now
    /// @param generation device type records generation (a NoMatch element whose last attempt was made against
    ///        another generation, or another hook epoch, is due at once)
    /// @param hookEpoch identification hook epoch
    /// @param work (out) the element
    /// @return true if an element is due
    /// @note only an element that is online and responded to its last access is due
    bool getNextIdentWork(uint32_t timeNowMs, uint32_t generation, uint32_t hookEpoch, IdentWork& work);

    /// @brief Set the outcome of identifying an element
    /// @param address address
    /// @param state outcome (Pending: in progress - resumed at resumeAtMs, with the state left as it was)
    /// @param deviceStatus device status (used if Identified)
    /// @param generation device type records generation it was identified against
    /// @param hookEpoch identification hook epoch
    /// @param resumeAtMs when to resume (Pending)
    void setIdentOutcome(BusElemAddrType address, BusIdentState state, const DeviceStatus& deviceStatus,
                uint32_t generation, uint32_t hookEpoch, uint32_t resumeAtMs);

    /// @brief Set the outcome of re-verifying an element
    enum class ReverifyResult : uint8_t
    {
        StillMatches,       // still that type (re-initialised)
        Mismatch,           // not that type now - identify it again
        BusError,           // re-verify again later
    };
    void setReverifyOutcome(BusElemAddrType address, ReverifyResult result);


    /// @brief Get identification timing for each element seen since the bus started, as JSON
    /// @return JSON array, e.g. [{"addr":"68","seenMs":120,"onlineMs":140,"identMs":152,"dtIdx":5}]. Times are
    ///         ms since the bus status manager was set up; -1 means not (yet) reached
    String getIdentTimingJson() const;

private:
    // Identification timing for an element: when it was first seen, came online and was identified
    struct IdentTiming
    {
        BusElemAddrType address = 0;
        static constexpr uint32_t NOT_REACHED = UINT32_MAX;
        uint32_t seenMs = NOT_REACHED;
        uint32_t onlineMs = NOT_REACHED;
        uint32_t identMs = NOT_REACHED;
        uint16_t deviceTypeIndex = DEVICE_TYPE_INDEX_INVALID;
        uint16_t identAttempts = 0;     // identification attempts (all outcomes)
    };
    static const uint32_t MAX_IDENT_TIMING = 16;

    // Position of the identification scheduler's round robin
    uint32_t _identCursor = 0;

    // Attempts at re-identifying a device (bus errors, or the hook deferring) before it is left as it is
    static const uint32_t REIDENT_MAX_ATTEMPTS = 3;
    IdentTiming _identTiming[MAX_IDENT_TIMING];
    uint32_t _identTimingCount = 0;
    uint32_t _setupTimeMs = 0;

    // Find (or add) the identification timing entry for an element - assumes semaphore taken
    IdentTiming* getIdentTiming(BusElemAddrType address, bool addIfMissing);

    // Bus element status change mutex
    mutable RaftMutex _busElemStatusMutex;

    // Bus base
    RaftBus& _raftBus;

    // Address status records (internal heavy structure)
    std::vector<BusAddrRecord> _addrStatus;
    static const uint32_t ADDR_STATUS_MAX = 50;

    // Chooses the next device to poll (earliest due first) - only online devices are polled
    DevicePollScheduler _pollScheduler;
    static DeviceStatus* getRecordDeviceStatusIfOnline(BusAddrRecord& rec)
    {
        return (rec.onlineState == DeviceOnlineState::ONLINE) ? &rec.deviceStatus : nullptr;
    }

    // A device that was online, or found and not yet online, has gone (e.g. its slot lost power): an online
    // device is reported and then deleted, others are deleted at once. Assumes the lock is held
    void markGoneOffline(BusAddrRecord& rec);
    void eraseSpuriousRecords();

    // Addresses whose per-address state on the bus task (CRC recovery) should be forgotten
    static const uint32_t FORGOTTEN_ADDRS_MAX = 16;
    std::vector<BusElemAddrType> _forgottenAddrs;
    bool _forgottenAddrsOverflow = false;
    std::atomic<bool> _forgottenAddrsPending = false;
    void addForgottenAddr(BusElemAddrType address);

    // Warn once when the address table is full
    bool _addrStatusFullWarned = false;

    // Find address record
    // Assumes semaphore already taken
    const BusAddrRecord* findAddrStatusRecord(BusElemAddrType address) const
    {
        for (const BusAddrRecord& addrStatus : _addrStatus)
        {
            if (addrStatus.address == address)
                return &addrStatus;
        }
        return nullptr;
    }

    // Find address record editable
    // Assumes semaphore already taken
    BusAddrRecord* findAddrStatusRecordEditable(BusElemAddrType address)
    {
        for (BusAddrRecord& addrStatus : _addrStatus)
        {
            if (addrStatus.address == address)
                return &addrStatus;
        }
        return nullptr;
    }

    // Address for lockup detect
    uint8_t _addrForLockupDetect = 0;
    bool _addrForLockupDetectValid = false;

    // Bus operation status
    BusOperationStatus _busOperationStatus = BUS_OPERATION_UNKNOWN;

    // Bus element status change detection
    // Set (under _busElemStatusMutex) by the bus task and checked without the mutex in loop()
    std::atomic<bool> _busElemStatusChangeDetected{false};

    // Last status update times us
    uint32_t _lastIdentPollUpdateTimeMs = 0;
    uint32_t _lastBusElemOnlineStatusUpdateTimeMs = 0;
    uint32_t _lastPollOrStatusUpdateTimeMs = 0;

    // Data change callback in-progress tracking (the callback is made outside the mutex on the task which
    // calls handlePollResult - normally the bus task) - used by unregisterForDeviceData to wait for quiescence
    std::atomic<bool> _dataChangeCBInProgress{false};
    std::atomic<const void*> _dataChangeCBInProgressInfo{nullptr};
    std::atomic<void*> _dataChangeCBInProgressTask{nullptr};
    std::atomic<uint32_t> _dataChangeCBCallCount{0};
    static const uint32_t DATA_CHANGE_CB_QUIESCE_MAX_MS = 500;

    /// @brief Unregister helper
    /// @param matchAllAddresses true to match all addresses
    /// @param address address to match (if not matchAllAddresses)
    /// @param pCallbackInfo Callback info to match
    /// @return number of registrations disarmed
    uint32_t unregisterForDeviceDataHelper(bool matchAllAddresses, BusElemAddrType address, const void* pCallbackInfo);

    // Pending deletion queue - holds deletion notices until they are published
    std::vector<DeletionNotice> _pendingDeletionQueue;
    static const uint32_t PENDING_DELETION_QUEUE_MAX = 20;

    // Debug overflow monitor
    static const uint32_t DEBUG_OVERFLOW_LOG_INTERVAL_MS = 1000;
    uint32_t _debugOverflowLastLogMs = 0;

    // Debug
    static constexpr const char* MODULE_PREFIX = "I2CBusStMgr";    
};
