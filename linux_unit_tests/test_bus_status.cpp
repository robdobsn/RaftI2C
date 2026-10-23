/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// BusStatusMgr: a device's data subscriber gets each poll result whole - also a poll split by pauses (e.g. SCD30's
// "0x0202=p5&=r3&..."), which completes over several bus passes: the timestamp and every part, as stored. And
// which identified devices each re-identification scope selects - a device identified by the identification hook
// (a probe-only type) included in every scope but Outranked
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include <stdio.h>
#include <memory>
#include <vector>
#include "BusStatusMgr.h"
#include "RaftBus.h"
#include "PollDataAggregatorIF.h"
#include "DeviceTypeRecords.h"
#include "BusIdentState.h"

static int failCount = 0;
#define TEST_ASSERT(cond, msg) if (!(cond)) { printf("TEST_ASSERT failed %s\n", msg); failCount++; }

// Keeps what was put
class RecordingAggregator : public PollDataAggregatorIF
{
public:
    std::vector<std::vector<uint8_t>> puts;
    void clear() override { puts.clear(); }
    bool put(uint64_t, const std::vector<uint8_t>& data) override { puts.push_back(data); return true; }
    bool get(std::vector<uint8_t>&) override { return false; }
    uint32_t get(std::vector<uint8_t>&, uint32_t&, uint32_t) override { return 0; }
    uint32_t count() const override { return puts.size(); }
    bool getLatestValue(uint64_t&, std::vector<uint8_t>&) override { return false; }
    bool resize(uint32_t) override { return true; }
    uint32_t getWithLengths(std::vector<uint8_t>&, std::vector<uint16_t>&, uint32_t) override { return 0; }
};

int testBusStatus()
{
    printf("Running testBusStatus...\n");
    RaftBus bus(nullptr, nullptr);
    BusStatusMgr statusMgr(bus);
    const BusElemAddrType addr = 0x461;
    bool isOnline = false;
    statusMgr.updateBusElemState(addr, true, isOnline);

    DeviceStatus deviceStatus;
    deviceStatus.deviceTypeIndex = 7;
    deviceStatus.deviceIdentPolling.pollResultSizeIncTimestamp = 32;
    auto pAggregator = std::make_shared<RecordingAggregator>();
    deviceStatus.setAndOwnPollDataAggregator(pAggregator);
    statusMgr.setBusElemDeviceStatus(addr, deviceStatus);

    std::vector<std::vector<uint8_t>> received;
    std::vector<uint16_t> receivedTypes;
    int info = 0;
    statusMgr.registerForDeviceData(addr,
        [&](uint16_t deviceTypeIndex, std::vector<uint8_t> data, const void*) {
            receivedTypes.push_back(deviceTypeIndex);
            received.push_back(data);
        }, 0, &info);

    // A poll in three parts: the timestamp and the first read, then (after a pause) the second, then the last
    std::vector<uint8_t> part1 = {0x12, 0x34, 0xa1, 0xa2, 0xa3};
    std::vector<uint8_t> part2 = {0xb1, 0xb2, 0xb3};
    std::vector<uint8_t> part3 = {0xc1, 0xc2, 0xc3, 0xc4};
    statusMgr.handlePollResult(1, 1000000, addr, part1, nullptr, 5);
    statusMgr.handlePollResult(2, 1006000, addr, part2, nullptr, 5);
    TEST_ASSERT(received.empty(), "no data reported for a part");
    statusMgr.handlePollResult(0, 1012000, addr, part3, nullptr, 0);
    std::vector<uint8_t> whole = part1;
    whole.insert(whole.end(), part2.begin(), part2.end());
    whole.insert(whole.end(), part3.begin(), part3.end());
    TEST_ASSERT(received.size() == 1, "one report for the poll");
    TEST_ASSERT(!received.empty() && (received[0] == whole), "the subscriber gets the whole poll: timestamp and every part");
    TEST_ASSERT(!receivedTypes.empty() && (receivedTypes[0] == 7), "with the device type");
    TEST_ASSERT((pAggregator->puts.size() == 1) && (pAggregator->puts[0] == whole), "stored whole too");

    // A poll in one part
    std::vector<uint8_t> single = {0x00, 0x10, 0x01, 0x02};
    statusMgr.handlePollResult(0, 1100000, addr, single, nullptr, 0);
    TEST_ASSERT((received.size() == 2) && (received[1] == single), "a poll in one part");
    return failCount;
}

// An online device identified as a type
static void addIdentifiedDevice(BusStatusMgr& statusMgr, BusElemAddrType addr, DeviceTypeIndexType deviceTypeIndex)
{
    bool isOnline = false;
    for (int i = 0; (i < 10) && !isOnline; i++)
        statusMgr.updateBusElemState(addr, true, isOnline);
    DeviceStatus deviceStatus;
    deviceStatus.deviceTypeIndex = deviceTypeIndex;
    statusMgr.setIdentOutcome(addr, BusIdentState::Identified, deviceStatus, deviceTypeRecords.getGeneration(), 0, 0);
}

// Re-identification scopes: a device identified by the hook (probe-only type, e.g. an RSAO) is re-identified in every
// scope but Outranked - so disabling its type (TypeIndex) drops it at once, as for any other type
int testReIdentHookDevices()
{
    printf("Running testReIdentHookDevices...\n");
    DeviceTypeIndexType addrTypeIdx = DEVICE_TYPE_INDEX_INVALID;
    DeviceTypeRecordDynamic addrRec("TestAddrType", "0x48", "0x0f=0x33", "", R"({"c":"0x00=r2","i":100,"s":1})", 2,
                R"({"name":"Test","resp":{"b":2,"a":[]}})", nullptr);
    deviceTypeRecords.addRecord(addrRec, addrTypeIdx);
    DeviceTypeIndexType hookTypeIdx = DEVICE_TYPE_INDEX_INVALID;
    DeviceTypeRecordDynamic hookRec("TestHookType", "", "", "", R"({"c":"=r2","i":100,"s":1})", 2,
                R"({"name":"Test","resp":{"b":2,"a":[]}})", nullptr);
    hookRec.flags |= DeviceTypeRecord::FLAG_PROBE_ONLY;
    deviceTypeRecords.addRecord(hookRec, hookTypeIdx);
    TEST_ASSERT((addrTypeIdx != DEVICE_TYPE_INDEX_INVALID) && (hookTypeIdx != DEVICE_TYPE_INDEX_INVALID), "records added");
    TEST_ASSERT(deviceTypeRecords.getRecord(hookTypeIdx) && deviceTypeRecords.getRecord(hookTypeIdx)->isProbeOnly(),
                "hook type is probe-only");

    RaftBus bus(nullptr, nullptr);
    const BusElemAddrType addrDev = 0x148;
    const BusElemAddrType hookDev = 0x231;
    auto count = [&](BusReIdentScope scope, BusElemAddrType address, DeviceTypeIndexType typeIdx) {
        BusStatusMgr statusMgr(bus);
        addIdentifiedDevice(statusMgr, addrDev, addrTypeIdx);
        addIdentifiedDevice(statusMgr, hookDev, hookTypeIdx);
        BusReIdentRequest request;
        request.scope = scope;
        request.address = address;
        request.deviceTypeIndex = typeIdx;
        return statusMgr.applyReIdentRequest(request);
    };
    TEST_ASSERT(count(BusReIdentScope::TypeIndex, 0, hookTypeIdx) == 1, "TypeIndex: the hook type's device (disabling it)");
    TEST_ASSERT(count(BusReIdentScope::TypeIndex, 0, addrTypeIdx) == 1, "TypeIndex: an address type's device");
    TEST_ASSERT(count(BusReIdentScope::All, 0, DEVICE_TYPE_INDEX_INVALID) == 2, "All: both devices");
    TEST_ASSERT(count(BusReIdentScope::Address, hookDev, DEVICE_TYPE_INDEX_INVALID) == 1, "Address: the hook device");
    TEST_ASSERT(count(BusReIdentScope::Unidentified, 0, DEVICE_TYPE_INDEX_INVALID) == 0, "Unidentified: neither");

    // Outranked: a record now tried at the hook device's address doesn't take it over (the probe-only type is never a
    // candidate, so it would always count as outranked and the record's detection would be run on it)
    DeviceTypeIndexType rankTypeIdx = DEVICE_TYPE_INDEX_INVALID;
    DeviceTypeRecordDynamic rankRec("TestRankType", "0x31", "0x0f=0x34", "", R"({"c":"0x00=r2","i":100,"s":1})", 2,
                R"({"name":"Test","resp":{"b":2,"a":[]}})", nullptr);
    deviceTypeRecords.addRecord(rankRec, rankTypeIdx);
    TEST_ASSERT(count(BusReIdentScope::Outranked, 0, rankTypeIdx) == 0, "Outranked: not the hook device");
    return failCount;
}

// A device the identification hook says isn't its own is offered to the hook HOOK_DECLINES_MAX times, then not again
// (its probe is a write that can change an ordinary device) until it comes online again, is asked for by address, or
// the hook's claims change
int testHookDeclined()
{
    printf("Running testHookDeclined...\n");
    RaftBus bus(nullptr, nullptr);
    BusStatusMgr statusMgr(bus);
    const BusElemAddrType dev = 0x15d;
    const uint32_t hookEpoch = 5;
    bool isOnline = false;
    for (int i = 0; (i < 10) && !isOnline; i++)
        statusMgr.updateBusElemState(dev, true, isOnline);
    TEST_ASSERT(isOnline, "device online");

    // Due at once (another records generation), so the work's hookDeclined can be read
    auto declined = [&](uint32_t epoch) {
        DeviceStatus noStatus;
        statusMgr.setIdentOutcome(dev, BusIdentState::NoMatch, noStatus, 0, epoch, 0);
        BusStatusMgr::IdentWork work;
        bool due = statusMgr.getNextIdentWork(millis(), 1, epoch, work);
        return due && work.hookDeclined;
    };
    TEST_ASSERT(!declined(hookEpoch), "not declined before the hook has been asked");
    for (uint32_t i = 0; i + 1 < BusStatusMgr::HOOK_DECLINES_MAX; i++)
        statusMgr.noteHookDeclined(dev, hookEpoch);
    TEST_ASSERT(!declined(hookEpoch), "offered again after fewer than HOOK_DECLINES_MAX declines");
    statusMgr.noteHookDeclined(dev, hookEpoch);
    TEST_ASSERT(declined(hookEpoch), "not offered again after HOOK_DECLINES_MAX declines");
    TEST_ASSERT(statusMgr.getIdentStateJsonFields(dev, hookEpoch).indexOf("\"hookDeclined\":true") >= 0,
                "identinfo reports hookDeclined");
    TEST_ASSERT(!declined(hookEpoch + 1), "offered again when the hook's claims change");
    statusMgr.noteHookDeclined(dev, hookEpoch + 1);
    TEST_ASSERT(!declined(hookEpoch + 1), "declines counted again in a new hook epoch");

    // Asked for by address: offered again
    for (uint32_t i = 0; i < BusStatusMgr::HOOK_DECLINES_MAX; i++)
        statusMgr.noteHookDeclined(dev, hookEpoch);
    TEST_ASSERT(declined(hookEpoch), "declined again");
    BusReIdentRequest request;
    request.scope = BusReIdentScope::All;
    statusMgr.applyReIdentRequest(request);
    TEST_ASSERT(declined(hookEpoch), "still declined after re-identifying all");
    request.scope = BusReIdentScope::Address;
    request.address = dev;
    statusMgr.applyReIdentRequest(request);
    TEST_ASSERT(!declined(hookEpoch), "offered again when asked for by address");

    // Offline and online again: offered again (it may be another device)
    for (uint32_t i = 0; i < BusStatusMgr::HOOK_DECLINES_MAX; i++)
        statusMgr.noteHookDeclined(dev, hookEpoch);
    TEST_ASSERT(declined(hookEpoch), "declined before going offline");
    for (int i = 0; (i < 10) && isOnline; i++)
        statusMgr.updateBusElemState(dev, false, isOnline);
    TEST_ASSERT(!isOnline, "device offline");
    for (int i = 0; (i < 10) && !isOnline; i++)
        statusMgr.updateBusElemState(dev, true, isOnline);
    TEST_ASSERT(isOnline, "device online again");
    TEST_ASSERT(!declined(hookEpoch), "offered again after coming online again");
    return failCount;
}

