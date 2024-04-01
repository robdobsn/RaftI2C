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

#include "BusI2C.h"

static const char* MODULE_PREFIX = "TestRaftI2C";

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
std::vector<RaftI2CAddrAndSlot> testConfigOnlineAddrList;
uint32_t busExtenderStatusChanMask[I2C_BUS_EXTENDERS_MAX] = {0};

// Callback for bus operating
BusOperationStatusCB busOperationStatusCB = [](BusBase& bus, BusOperationStatus busOperationStatus) {
#ifdef DEBUG_BUS_OPERATION_CB
    LOG_I(MODULE_PREFIX, "busOperationStatusCB %s", BusBase::busOperationStatusToString(busOperationStatus));
#endif
    busStatus = busOperationStatus;
};

// Callback for bus element status
BusElemStatusCB busElemStatusCB = [](BusBase& bus, const std::vector<BusElemAddrAndStatus>& statusChanges) {

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

BusI2CRequestFn busOperationRequestFn = [](BusI2CRequestRec* pReqRec, uint32_t pollListIdx) {
    // LOG_I(MODULE_PREFIX, "busOperationRequestFn addr 0x%02x slot+1 %d pollListIdx %d", 
    //                 pReqRec->getAddrAndSlot().addr, pReqRec->getAddrAndSlot().slotPlus1, pollListIdx);
    
    RaftI2CAddrAndSlot addrAndSlot = pReqRec->getAddrAndSlot();
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
        if (BusStatusMgr::isBusExtender(addr))
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
                    uint32_t extenderIdx = (testConfigAddrAndSlot.slotPlus1 - 1) / BusStatusMgr::I2C_BUS_EXTENDER_SLOT_COUNT;
                    uint32_t chanMask = 1 << ((testConfigAddrAndSlot.slotPlus1 - 1) % BusStatusMgr::I2C_BUS_EXTENDER_SLOT_COUNT);
                    if (busExtenderStatusChanMask[extenderIdx] & chanMask)
                    {
#ifdef DEBUG_SLOT_ENABLE_RESPONSES
                        LOG_I(MODULE_PREFIX, "Slot enabled addr 0x%02x slot+1 %d extenderIdx %d mask %02x", 
                                addr, testConfigAddrAndSlot.slotPlus1, extenderIdx, chanMask);
#endif
                        reslt = RaftI2CCentralIF::ACCESS_RESULT_OK;
                        break;
                    }
                    else
                    {
#ifdef DEBUG_SLOT_ENABLE_RESPONSES
                        LOG_I(MODULE_PREFIX, "Slot not enabled addr 0x%02x slot+1 %d extenderIdx %d mask %02x", 
                                addr, testConfigAddrAndSlot.slotPlus1, extenderIdx, chanMask);
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
        LOG_E(MODULE_PREFIX, "======= busOperationRequestFn rslt %s addr 0x%02x isOnline %s slot+1 %d pollListIdx %d writeData <%s> readDataLen %d", 
            RaftI2CCentralIF::getAccessResultStr(reslt),
            addr,
            inOnlineList ? "Y" : "N", 
            addrAndSlot.slotPlus1,
            pollListIdx,
            writeDataStr.c_str(), pReqRec->getReadReqLen());
    }
#endif
#ifdef DEBUG_SUCCESS_BUS_REQ_FN
    if (reslt == RaftI2CCentralIF::ACCESS_RESULT_OK)
    {
        LOG_I(MODULE_PREFIX, "======= busOperationRequestFn rslt %s addr 0x%02x isOnline %s slot+1 %d pollListIdx %d writeData <%s> readDataLen %d", 
            RaftI2CCentralIF::getAccessResultStr(reslt),
            addr,
            inOnlineList ? "Y" : "N", 
            addrAndSlot.slotPlus1,
            pollListIdx,
            writeDataStr.c_str(), pReqRec->getReadReqLen());
    }
#endif

    return reslt;
};

// BusBase
BusBase busBase(busElemStatusCB, busOperationStatusCB);

// BusStatusMgr
BusStatusMgr busStatusMgr(busBase);

BusScanner busScanner(busStatusMgr, busOperationRequestFn);

void helper_reset_status_changes_list()
{
    statusChangesList.clear();
}

void helper_set_online_addrs(const std::vector<RaftI2CAddrAndSlot>& onlineAddrs)
{
    testConfigOnlineAddrList = onlineAddrs;
}

void helper_setup_i2c_tests(std::vector<RaftI2CAddrAndSlot> onlineAddrs)
{    
    busStatus = BUS_OPERATION_UNKNOWN;
    statusChangesList.clear();
    helper_set_online_addrs(onlineAddrs);

    // Config
    busBase.setup(configJson);
    busStatusMgr.setup(configJson);
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
        busStatusMgr.service(true);
        if (serviceScanner)
            busScanner.service();

        // Check if any bus extenders need to be initialised
        uint32_t addr = 0;
        if (busStatusMgr.getBusExtenderAddrRequiringInit(addr))
        {
            LOG_I(MODULE_PREFIX, "Bus extender requiring init 0x%02x", addr);
        }
    }
}

void helper_elem_states_handle(const std::vector<RaftI2CAddrAndSlot>& addrs, bool elemResponding, uint32_t count)
{
    for (int i = 0; i < count; i++)
    {
        for (auto addr : addrs)
        {
            busStatusMgr.handleBusElemStateChanges(addr, elemResponding);
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
    busStatusMgr.getBusExtenderAddrList(busExtenders);
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

bool helper_check_online_offline_elems(std::vector<RaftI2CAddrAndSlot> onlineElems)
{
    // Create a list of all addresses
    std::vector<RaftI2CAddrAndSlot> offlineAddrs;
    for (int i = I2C_BUS_ADDRESS_MIN; i < I2C_BUS_ADDRESS_MAX; i++)
        offlineAddrs.push_back(RaftI2CAddrAndSlot(i,0));

    // Go through addresses that should be online
    for (auto addr : onlineElems)
    {
        if (!busStatusMgr.isElemOnline(addr))
        {
            LOG_E(MODULE_PREFIX, "Address 0x%02x slot+1 %d should be online", addr.addr, addr.slotPlus1);
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

bool helper_check_bus_extender_init_ok(std::vector<uint32_t> busExtenderList)
{
    for (auto addr : busExtenderList)
    {
        // Extender should have had all channels enabled (mask = 0xff)
        if (busExtenderStatusChanMask[addr - I2C_BUS_EXTENDER_BASE] != 0xff)
        {
            LOG_E(MODULE_PREFIX, "Bus extender 0x%02x not initialised", addr);
            return false;
        }
    }
    return true;
}

TEST_CASE("test_rafti2c_bus_status", "[rafti2c_busi2c_adv_tests]")
{
    static const uint32_t testAddr = lockupDetectAddr;

    // Setup test
    helper_setup_i2c_tests({});

    // Service some
    helper_service_some(100, false);

    // Check status changes list is empty
    TEST_ASSERT_MESSAGE(statusChangesList.size() == 0, "statusChangesList not empty initially");

    // Detect change to online
    helper_elem_states_handle({{testAddr,0}}, true, BusStatusMgr::I2C_ADDR_RESP_COUNT_OK_MAX);

    // Service the status for some time
    helper_service_some(100, false);

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
    helper_service_some(100, false);

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
    helper_service_some(100, false);

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
    helper_service_some(1000, true);

    // Test bus is operational
    TEST_ASSERT_MESSAGE(busStatusMgr.isOperatingOk() == BUS_OPERATION_OK, "busStatus not BUS_OPERATION_OK");

    // Check that the scanner has detected one bus extender
    TEST_ASSERT_MESSAGE(helper_check_bus_extender_list({extenderAddr}), "busExtenderList not correct");

    // Check elems that should be online are online, etc
    TEST_ASSERT_MESSAGE(helper_check_online_offline_elems({{testAddr,0},{extenderAddr,0}}), "online/offline elems not correct");

    // Check bus extender is initialised
    TEST_ASSERT_MESSAGE(helper_check_bus_extender_init_ok({extenderAddr}), "busExtenderInitialised not true");
}

TEST_CASE("test_rafti2c_bus_scanner_slotted", "[rafti2c_busi2c_tests]")
{
    // Setup test
    RaftI2CAddrAndSlot testAddr1 = {lockupDetectAddr, 0};
    RaftI2CAddrAndSlot extenderAddr1 = {0x73, 0};
    RaftI2CAddrAndSlot testSlottedAddr1 = {0x47, (extenderAddr1.addr - I2C_BUS_EXTENDER_BASE) * BusStatusMgr::I2C_BUS_EXTENDER_SLOT_COUNT + 1};
    helper_setup_i2c_tests({testAddr1, testSlottedAddr1, extenderAddr1});

    // Service the status for some time
    helper_service_some(1000, true);

    // Test bus is operational
    TEST_ASSERT_MESSAGE(busStatusMgr.isOperatingOk() == BUS_OPERATION_OK, "busStatus not BUS_OPERATION_OK");

    // Check that the scanner has detected one bus extender
    TEST_ASSERT_MESSAGE(helper_check_bus_extender_list({extenderAddr1.addr}), "busExtenderList not correct");

    // Check elems that should be online are online, etc
    TEST_ASSERT_MESSAGE(helper_check_online_offline_elems({testAddr1, testSlottedAddr1, extenderAddr1}), "online/offline elems not correct");

    // Check bus extender is initialised
    TEST_ASSERT_MESSAGE(helper_check_bus_extender_init_ok({extenderAddr1.addr}), "busExtenderInitialised not true");

    // Add two further slotted addresses
    RaftI2CAddrAndSlot testSlottedAddr2 = {0x47, (extenderAddr1.addr - I2C_BUS_EXTENDER_BASE) * BusStatusMgr::I2C_BUS_EXTENDER_SLOT_COUNT + 2};
    RaftI2CAddrAndSlot testSlottedAddr3 = {0x47, (extenderAddr1.addr - I2C_BUS_EXTENDER_BASE) * BusStatusMgr::I2C_BUS_EXTENDER_SLOT_COUNT + 5};
    helper_set_online_addrs({testAddr1, testSlottedAddr1, testSlottedAddr2, testSlottedAddr3, extenderAddr1});

    // Service the status for some time
    helper_service_some(1000, true);

    // Check elems that should be online are online, etc
    TEST_ASSERT_MESSAGE(helper_check_online_offline_elems({testAddr1, testSlottedAddr1, testSlottedAddr2, testSlottedAddr3, extenderAddr1}), "online/offline elems not correct 2");

    // Remove one of the slotted addresses
    helper_set_online_addrs({testAddr1, testSlottedAddr1, testSlottedAddr3, extenderAddr1});

    // Service the status for some time
    helper_service_some(1000, true);

    // Check elems that should be online are online, etc
    TEST_ASSERT_MESSAGE(helper_check_online_offline_elems({testAddr1, testSlottedAddr1, testSlottedAddr3, extenderAddr1}), "online/offline elems not correct 3");
}
