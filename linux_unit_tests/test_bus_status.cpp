/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// BusStatusMgr: a device's data subscriber gets each poll result whole - also a poll split by pauses (e.g. SCD30's
// "0x0202=p5&=r3&..."), which completes over several bus passes: the timestamp and every part, as stored
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include <stdio.h>
#include <memory>
#include <vector>
#include "BusStatusMgr.h"
#include "RaftBus.h"
#include "PollDataAggregatorIF.h"

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
