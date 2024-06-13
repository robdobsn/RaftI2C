/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Unit tests of I2C Bus
//
// Rob Dobson 2024
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "unity.h"
#include "unity_test_runner.h"
#include "RaftJsonPrefixed.h"

#include "BusI2C.h"

static const char* MODULE_PREFIX = "test_bus_i2c";

// #define DEBUG_FAILED_BUS_REQ_FN_SPECIFIC_ADDR 0x47
// #define DEBUG_SUCCESS_BUS_REQ_FN
// #define DEBUG_SLOT_ENABLE_RESPONSES
// #define DEBUG_BUS_OPERATION_CB

// Set busScanPeriodMs to 0 to speed up tests
static const uint32_t lockupDetectAddr = 0x55;
static RaftJson configJson = "{\"lockupDetect\":\"0x55\",\"scanBoost\":[\"0x55\"],\"busScanPeriodMs\":0}";

// List of status changes
std::vector<BusElemAddrAndStatus> statusChangesList;
BusOperationStatus busStatus = BUS_OPERATION_UNKNOWN;

// Test configuration
std::vector<BusI2CAddrAndSlot> testConfigOnlineAddrList;
uint32_t busExtenderStatusChanMask[I2C_BUS_EXTENDERS_MAX] = {0};

// Callback for bus operating
BusOperationStatusCB busOperationStatusCB = [](RaftBus& bus, BusOperationStatus busOperationStatus) {
#ifdef DEBUG_BUS_OPERATION_CB
    LOG_I(MODULE_PREFIX, "busOperationStatusCB %s", RaftBus::busOperationStatusToString(busOperationStatus));
#endif
    busStatus = busOperationStatus;
};

// Callback for bus element status
BusElemStatusCB busElemStatusCB = [](RaftBus& bus, const std::vector<BusElemAddrAndStatus>& statusChanges) {

    // Append to status changes list for later checking
    if (statusChangesList.size() < 500)
    {
        for (const auto& stat : statusChanges)
        {
            statusChangesList.push_back(stat);
            // LOG_I(MODULE_PREFIX, "busElemStatusCB addr %02x online %s", stat.address, stat.isChangeToOnline ? "Y" : "N");
        }
        // statusChangesList.insert(statusChangesList.end(), statusChangesNonConst.begin(), statusChangesNonConst.end());
    }
    else
    {
        LOG_E(MODULE_PREFIX, "statusChangesList full");
    }
};

BusI2CReqSyncFn busReqSyncFn = [](const BusI2CRequestRec* pReqRec, std::vector<uint8_t>* pReadData) {
    // LOG_I(MODULE_PREFIX, "busReqSyncFn addr@slotNum %s pollListIdx %d", 
    //                 pReqRec->getAddrAndSlot().toString().c_str(), pollListIdx);
    
    BusI2CAddrAndSlot addrAndSlot = pReqRec->getAddrAndSlot();
    uint32_t addr = addrAndSlot.addr;
    RaftI2CCentralIF::AccessResultCode reslt = RaftI2CCentralIF::ACCESS_RESULT_ACK_ERROR;

    // Check if this address is in the online list
    bool inOnlineList = false;
    for (auto& testConfigAddrAndSlot : testConfigOnlineAddrList)
    {
        if (testConfigAddrAndSlot.addr == addr)
        {
            inOnlineList = true;
            break;
        }
    }
    if(inOnlineList)
    {
        // Check if this is an extender
        if ((addr >= I2C_BUS_EXTENDER_BASE) && (addr < I2C_BUS_EXTENDER_BASE + I2C_BUS_EXTENDERS_MAX))
        {
            uint32_t extenderIdx = addr - I2C_BUS_EXTENDER_BASE;
            // Check if data being written to extender
            if (pReqRec->getWriteDataLen() > 0)
            {
                busExtenderStatusChanMask[extenderIdx] = pReqRec->getWriteData()[0];
            }
            reslt = RaftI2CCentralIF::ACCESS_RESULT_OK;
        }
        else
        {
            // Check through matching online addresses and see if the slot is enabled
            for (auto& testConfigAddrAndSlot : testConfigOnlineAddrList)
            {
                if (testConfigAddrAndSlot.addr == addr)
                {
                    // If device test record says slot 0 then the test device is connected to the main bus
                    // so always connected
                    if (testConfigAddrAndSlot.slotPlus1 == 0)
                    {
                        reslt = RaftI2CCentralIF::ACCESS_RESULT_OK;
                        break;
                    }
                    // Calculate the extender idx and mask required
                    uint32_t extenderIdx = (testConfigAddrAndSlot.slotPlus1 - 1) / BusExtenderMgr::I2C_BUS_EXTENDER_SLOT_COUNT;
                    uint32_t chanMask = 1 << ((testConfigAddrAndSlot.slotPlus1 - 1) % BusExtenderMgr::I2C_BUS_EXTENDER_SLOT_COUNT);
                    if (busExtenderStatusChanMask[extenderIdx] & chanMask)
                    {
#ifdef DEBUG_SLOT_ENABLE_RESPONSES
                        LOG_I(MODULE_PREFIX, "Slot enabled addr@slotNum %s extenderIdx %d mask %02x", 
                                testConfigAddrAndSlot.toString().c_str(), extenderIdx, chanMask);
#endif
                        reslt = RaftI2CCentralIF::ACCESS_RESULT_OK;
                        break;
                    }
                    else
                    {
#ifdef DEBUG_SLOT_ENABLE_RESPONSES
                        LOG_I(MODULE_PREFIX, "Slot not enabled addr@slotNum %s extenderIdx %d mask %02x", 
                                testConfigAddrAndSlot.toString().c_str(), extenderIdx, chanMask);
#endif
                    }
                }
            }
        }
    }
    String writeDataStr;
    Raft::getHexStrFromBytes(pReqRec->getWriteData(), pReqRec->getWriteDataLen(), writeDataStr);
#ifdef DEBUG_FAILED_BUS_REQ_FN_SPECIFIC_ADDR
    if ((reslt != RaftI2CCentralIF::ACCESS_RESULT_OK) && (addr == DEBUG_FAILED_BUS_REQ_FN_SPECIFIC_ADDR))
    {
        LOG_E(MODULE_PREFIX, "======= busReqSyncFn rslt %s addr@slotNum %s isOnline %s writeData <%s> readDataLen %d", 
            RaftI2CCentralIF::getAccessResultStr(reslt),
            addrAndSlot.toString().c_str(),
            inOnlineList ? "Y" : "N", 
            writeDataStr.c_str(), pReqRec->getReadReqLen());
    }
#endif
#ifdef DEBUG_SUCCESS_BUS_REQ_FN
    if (reslt == RaftI2CCentralIF::ACCESS_RESULT_OK)
    {
        LOG_I(MODULE_PREFIX, "======= busReqSyncFn rslt %s addr@slotNum %s isOnline %s writeData <%s> readDataLen %d", 
            RaftI2CCentralIF::getAccessResultStr(reslt),
            addrAndSlot.toString().c_str(),
            inOnlineList ? "Y" : "N", 
            writeDataStr.c_str(), pReqRec->getReadReqLen());
    }
#endif

    return reslt;
};

// RaftBus
RaftBus raftBus(busElemStatusCB, busOperationStatusCB);

// BusStatusMgr
BusStatusMgr busStatusMgr(raftBus);
BusPowerController busPowerController(busReqSyncFn);
BusStuckHandler busStuckHandler(busReqSyncFn);
BusExtenderMgr busExtenderMgr(busPowerController, busStuckHandler, busStatusMgr, busReqSyncFn);
DeviceIdentMgr deviceIdentMgr(busStatusMgr, busExtenderMgr, busReqSyncFn);
BusScanner busScanner(busStatusMgr, busExtenderMgr, deviceIdentMgr, busReqSyncFn);

void helper_reset_status_changes_list()
{
    statusChangesList.clear();
}

void helper_set_online_addrs(const std::vector<BusI2CAddrAndSlot>& onlineAddrs)
{
    testConfigOnlineAddrList = onlineAddrs;
}

void helper_setup_i2c_tests(std::vector<BusI2CAddrAndSlot> onlineAddrs)
{    
    busStatus = BUS_OPERATION_UNKNOWN;
    statusChangesList.clear();
    helper_set_online_addrs(onlineAddrs);

    // Config
    raftBus.setup(configJson);
    busStatusMgr.setup(configJson);
    RaftJsonPrefixed busExtenderConfigJson(configJson, "mux");
    busExtenderMgr.setup(busExtenderConfigJson);
    busScanner.setup(configJson);
}

void helper_cleardown_i2c_tests()
{
}

void helper_service_some(uint32_t serviceLoops, bool serviceScanner)
{
    // Service the status for some time
    for (int i = 0; i < serviceLoops; i++)
    {
        busStatusMgr.loop(true);
        if (serviceScanner)
            busScanner.taskService(micros(), 10000, 2000);
        if ((i % 1000) == 0)
        {
            vTaskDelay(1);
        }
    }
}

void helper_elem_states_handle(const std::vector<BusI2CAddrAndSlot>& addrs, bool elemResponding, uint32_t count)
{
    for (int i = 0; i < count; i++)
    {
        for (auto addr : addrs)
        {
            bool isOnline = false;
            busStatusMgr.updateBusElemState(addr, elemResponding, isOnline);
        }
    }
}

void helper_show_status_change_list(const char* linePrefix)
{
    for (auto& statusChange : statusChangesList)
    {
        LOG_I(MODULE_PREFIX, "%s statusChangesList addr %02x online %s", linePrefix, statusChange.address, statusChange.isChangeToOnline ? "Y" : "N");
    }
}

bool helper_check_bus_extender_list(std::vector<uint32_t> busExtenderList)
{
    // Check bus extenders
    std::vector<uint32_t> busExtenders;
    busExtenderMgr.getActiveExtenderAddrs(busExtenders);
    if (busExtenders.size() != busExtenderList.size())
    {
        LOG_E(MODULE_PREFIX, "Bus extender list size mismatch actual len %d expected %d", busExtenders.size(), busExtenderList.size());
        for (auto addr : busExtenders)
            LOG_I(MODULE_PREFIX, "Actual bus extender %02x", addr);
        return false;
    }
    for (int i = 0; i < busExtenders.size(); i++)
    {
        if (busExtenders[i] != busExtenderList[i])
        {
            LOG_E(MODULE_PREFIX, "Bus extender list mismatch %d %02x %d %02x", i, busExtenders[i], i, busExtenderList[i]);
            return false;
        }
    }
    return true;
}

bool helper_check_online_offline_elems(std::vector<BusI2CAddrAndSlot> onlineElems)
{
    // Create a list of all addresses
    std::vector<BusI2CAddrAndSlot> offlineAddrs;
    for (int i = I2C_BUS_ADDRESS_MIN; i < I2C_BUS_ADDRESS_MAX; i++)
        offlineAddrs.push_back(BusI2CAddrAndSlot(i,0));

    // Go through addresses that should be online
    for (auto addr : onlineElems)
    {
        if (!busStatusMgr.isElemOnline(addr))
        {
            LOG_E(MODULE_PREFIX, "Address 0x%02x slotNum %d should be online", addr.addr, addr.slotPlus1);
            return false;
        }
        // Remove from list
        for (int i = 0; i < offlineAddrs.size(); i++)
        {
            if (offlineAddrs[i].addr == addr.addr)
            {
                offlineAddrs.erase(offlineAddrs.begin() + i);
                break;
            }
        }
    }

    // Go through addresses that should be offline
    for (auto addr : offlineAddrs)
    {
        if (busStatusMgr.isElemOnline(addr))
        {
            LOG_E(MODULE_PREFIX, "Address 0x%02x should be offline", addr.addr);
            return false;
        }
    }
    return true;
}

TEST_CASE("raft_i2c_bus_extender_next_slot", "[rafti2c_busi2c_tests]")
{
    // Setup bus extenders
    BusExtenderMgr busExtenderMgr(busPowerController, busStuckHandler, busStatusMgr, busReqSyncFn);
    busExtenderMgr.setup(configJson);

    // Check next slot
    TEST_ASSERT_MESSAGE(busExtenderMgr.getNextSlotNum(0) == 0, "getNextSlotNum 0 not 0 when no extenders");
    TEST_ASSERT_MESSAGE(busExtenderMgr.getNextSlotNum(11) == 0, "getNextSlotNum 11 not 0 when no extenders");

    // Add some bus extenders
    busExtenderMgr.elemStateChange(0x73, true); // SlotPlus1 range = 25-32 (inclusive)
    busExtenderMgr.elemStateChange(0x75, true); // SlotPlus1 range = 41-48 (inclusive)

    // Check next slot
    TEST_ASSERT_MESSAGE(busExtenderMgr.getNextSlotNum(0) == 25, "getNextSlotNum 0 not 25");
    TEST_ASSERT_MESSAGE(busExtenderMgr.getNextSlotNum(1) == 25, "getNextSlotNum 1 not 25");
    TEST_ASSERT_MESSAGE(busExtenderMgr.getNextSlotNum(24) == 25, "getNextSlotNum 24 not 25");
    TEST_ASSERT_MESSAGE(busExtenderMgr.getNextSlotNum(25) == 26, "getNextSlotNum 25 not 26");
    TEST_ASSERT_MESSAGE(busExtenderMgr.getNextSlotNum(28) == 29, "getNextSlotNum 28 not 29");
    TEST_ASSERT_MESSAGE(busExtenderMgr.getNextSlotNum(31) == 32, "getNextSlotNum 31 not 32");
    TEST_ASSERT_MESSAGE(busExtenderMgr.getNextSlotNum(32) == 41, "getNextSlotNum 32 not 41");
    TEST_ASSERT_MESSAGE(busExtenderMgr.getNextSlotNum(41) == 42, "getNextSlotNum 33 not 42");
    TEST_ASSERT_MESSAGE(busExtenderMgr.getNextSlotNum(47) == 48, "getNextSlotNum 47 not 48");
    TEST_ASSERT_MESSAGE(busExtenderMgr.getNextSlotNum(48) == 0, "getNextSlotNum 48 not 0");
}

TEST_CASE("test_rafti2c_bus_status", "[rafti2c_busi2c_adv_tests]")
{
    static const uint32_t testAddr = lockupDetectAddr;

    // Setup test
    helper_setup_i2c_tests({});

    // Service some
    helper_service_some(1000, false);

    // Check status changes list is empty
    TEST_ASSERT_MESSAGE(statusChangesList.size() == 0, "statusChangesList not empty initially");

    // Detect change to online
    helper_elem_states_handle({{testAddr,0}}, true, BusStatusMgr::I2C_ADDR_RESP_COUNT_OK_MAX);

    // Service the status for some time
    helper_service_some(1000, false);

    // Check status changes list has one item which is the change of state for addr lockupDetectAddr to online
    TEST_ASSERT_MESSAGE(statusChangesList.size() == 1, "statusChangesList empty when lockupDetectAddr change to online");
    if (statusChangesList.size() == 1)
    {
        TEST_ASSERT_MESSAGE(statusChangesList[0].address == testAddr, "statusChangesList addr not lockupDetectAddr");
        TEST_ASSERT_MESSAGE(statusChangesList[0].isChangeToOnline, "statusChangesList status not online");
    }

    // Test bus is operational
    TEST_ASSERT_MESSAGE(busStatus == BUS_OPERATION_OK, "busStatus not BUS_OPERATION_OK");

    // Clear the list
    helper_reset_status_changes_list();

    // Detect change to offline
    helper_elem_states_handle({{testAddr,0}}, false, BusStatusMgr::I2C_ADDR_RESP_COUNT_FAIL_MAX);

    // Service the status for some time
    helper_service_some(1000, false);

    // Check status changes list has one item which is the change of state for addr lockupDetectAddr to offline
    TEST_ASSERT_MESSAGE(statusChangesList.size() == 1, "statusChangesList not 1 when lockupDetectAddr change to offline");
    if (statusChangesList.size() == 1)
    {
        TEST_ASSERT_MESSAGE(statusChangesList[0].address == lockupDetectAddr, "statusChangesList addr not lockupDetectAddr");
        TEST_ASSERT_MESSAGE(!statusChangesList[0].isChangeToOnline, "statusChangesList status not offline");
    }
    TEST_ASSERT_MESSAGE(busStatusMgr.getAddrStatusCount() == 1, "getAddrStatusCount not 1");

    // Test bus is failing
    TEST_ASSERT_MESSAGE(busStatus == BUS_OPERATION_FAILING, "busStatus not BUS_OPERATION_FAILING");

    // Check spurious address handing
    statusChangesList.clear();

    // Add some spurious records
    helper_elem_states_handle({{0x60,0},{0x64,0},{0x67,0}}, true, 1);

    // Send some more status changes - should result in spurious records being removed
    helper_elem_states_handle({{0x60,0},{0x61,0},{0x62,0},{0x63,0},{0x64,0},{0x65,0},{0x66,0},{0x67,0}}, false, 
                        BusStatusMgr::I2C_ADDR_RESP_COUNT_FAIL_MAX);

    // Service the status for some time
    helper_service_some(1000, false);

    // Check status changes list is empty
    helper_show_status_change_list("After spurious:");
    TEST_ASSERT_MESSAGE(statusChangesList.size() == 0, "statusChangesList not empty at end of test");
    TEST_ASSERT_MESSAGE(busStatusMgr.getAddrStatusCount() == 1, "address status recs should be 1 at end of test");
}

TEST_CASE("test_rafti2c_bus_scanner_basic", "[rafti2c_busi2c_tests]")
{
    // Setup test
    uint32_t testAddr = lockupDetectAddr;
    uint32_t extenderAddr = 0x73;
    helper_setup_i2c_tests({{testAddr, 0}, {extenderAddr, 0}});

    // Service the status for some time
    helper_service_some(10000, true);

    // Test bus is operational
    TEST_ASSERT_MESSAGE(busStatusMgr.isOperatingOk() == BUS_OPERATION_OK, "busStatus not BUS_OPERATION_OK");

    // Check that the scanner has detected one bus extender
    TEST_ASSERT_MESSAGE(helper_check_bus_extender_list({extenderAddr}), "busExtenderList not correct");

    // Check elems that should be online are online, etc
    TEST_ASSERT_MESSAGE(helper_check_online_offline_elems({{testAddr,0},{extenderAddr,0}}), "online/offline elems not correct");
}

TEST_CASE("test_rafti2c_bus_scanner_slotted", "[rafti2c_busi2c_tests]")
{
    // Setup test
    BusI2CAddrAndSlot testAddr1 = {lockupDetectAddr, 0};
    BusI2CAddrAndSlot extenderAddr1 = {0x73, 0};
    BusI2CAddrAndSlot testSlottedAddr1 = {0x47, (extenderAddr1.addr - I2C_BUS_EXTENDER_BASE) * BusExtenderMgr::I2C_BUS_EXTENDER_SLOT_COUNT + 1};
    helper_setup_i2c_tests({testAddr1, testSlottedAddr1, extenderAddr1});

    // Service the status for some time
    helper_service_some(20000, true);

    // Test bus is operational
    TEST_ASSERT_MESSAGE(busStatusMgr.isOperatingOk() == BUS_OPERATION_OK, "busStatus not BUS_OPERATION_OK");

    // Check that the scanner has detected one bus extender
    TEST_ASSERT_MESSAGE(helper_check_bus_extender_list({extenderAddr1.addr}), "busExtenderList not correct");

    // Check elems that should be online are online, etc
    TEST_ASSERT_MESSAGE(helper_check_online_offline_elems({testAddr1, testSlottedAddr1, extenderAddr1}), "online/offline elems not correct");

    // Add two further slotted addresses
    BusI2CAddrAndSlot testSlottedAddr2 = {0x47, (extenderAddr1.addr - I2C_BUS_EXTENDER_BASE) * BusExtenderMgr::I2C_BUS_EXTENDER_SLOT_COUNT + 2};
    BusI2CAddrAndSlot testSlottedAddr3 = {0x47, (extenderAddr1.addr - I2C_BUS_EXTENDER_BASE) * BusExtenderMgr::I2C_BUS_EXTENDER_SLOT_COUNT + 5};
    helper_set_online_addrs({testAddr1, testSlottedAddr1, testSlottedAddr2, testSlottedAddr3, extenderAddr1});

    // Service the status for some time
    helper_service_some(10000, true);

    // Check elems that should be online are online, etc
    TEST_ASSERT_MESSAGE(helper_check_online_offline_elems({testAddr1, testSlottedAddr1, testSlottedAddr2, testSlottedAddr3, extenderAddr1}), "online/offline elems not correct 2");

    // Remove one of the slotted addresses
    helper_set_online_addrs({testAddr1, testSlottedAddr1, testSlottedAddr3, extenderAddr1});

    // Service the status for some time
    helper_service_some(10000, true);

    // Check elems that should be online are online, etc
    TEST_ASSERT_MESSAGE(helper_check_online_offline_elems({testAddr1, testSlottedAddr1, testSlottedAddr3, extenderAddr1}), "online/offline elems not correct 3");
}
