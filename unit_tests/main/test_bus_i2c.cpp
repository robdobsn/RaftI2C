/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Unit tests of RaftJson value extraction
//
// Rob Dobson 2017-2023
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

TEST_CASE("test_rafti2c_bus_status", "[rafti2c_busi2c_tests]")
{
    // List of status changes
    std::vector<BusElemAddrAndStatus> statusChangesList;

    // Callback for bus operating
    BusOperationStatusCB busOperationStatusCB = [](BusBase& bus, BusOperationStatus busOperationStatus) {
        LOG_I(MODULE_PREFIX, "busOperationStatusCB %d", busOperationStatus);
    };

    // Callback for bus element status
    BusElemStatusCB busElemStatusCB = [&statusChangesList](BusBase& bus, const std::vector<BusElemAddrAndStatus>& statusChanges) {

        // Append to status changes list for later checking
        if (statusChangesList.size() < 500)
        {
            for (const auto& stat : statusChanges)
            {
                statusChangesList.push_back(stat);
                LOG_I(MODULE_PREFIX, "busElemStatusCB addr %02x online %s", stat.address, stat.isChangeToOnline ? "Y" : "N");
            }
            // statusChangesList.insert(statusChangesList.end(), statusChangesNonConst.begin(), statusChangesNonConst.end());
        }
        else
        {
            LOG_E(MODULE_PREFIX, "statusChangesList full");
        }
    };

    // Config
    RaftJson config = "{\"lockupDetect\":\"0x55\"}";

    // BusBase
    BusBase busBase(busElemStatusCB, busOperationStatusCB);
    busBase.setup(config);

    // BusStatusMgr
    BusStatusMgr busStatusMgr(busBase);
    busStatusMgr.setup(config);

    // Service the status for some time
    for (int i = 0; i < 100; i++)
    {
        busStatusMgr.service(true);
    }

    // Check status changes list is empty
    TEST_ASSERT_MESSAGE(statusChangesList.size() == 0, "statusChangesList not empty when no changes detected");

    // Detect change to online
    for (int i = 0; i < BusStatusMgr::I2C_ADDR_RESP_COUNT_OK_MAX; i++)
    {
        busStatusMgr.handleBusElemStateChanges(0x55, true);
    }

    // Service the status for some time
    for (int i = 0; i < 100; i++)
    {
        busStatusMgr.service(true);
    }

    // Check status changes list has one item which is the change of state for addr 0x55 to online
    TEST_ASSERT_MESSAGE(statusChangesList.size() == 1, "statusChangesList empty when changes detected");
    if (statusChangesList.size() == 1)
    {
        TEST_ASSERT_MESSAGE(statusChangesList[0].address == 0x55, "statusChangesList addr not 0x55");
        TEST_ASSERT_MESSAGE(statusChangesList[0].isChangeToOnline, "statusChangesList status not online");
    }

    // Clear the list
    statusChangesList.clear();

    // Detect change to offline
    for (int i = 0; i < BusStatusMgr::I2C_ADDR_RESP_COUNT_FAIL_MAX; i++)
    {
        busStatusMgr.handleBusElemStateChanges(0x55, false);
    }

    // Service the status for some time
    for (int i = 0; i < 100; i++)
    {
        busStatusMgr.service(true);
    }

    // Check status changes list has one item which is the change of state for addr 0x55 to offline
    TEST_ASSERT_MESSAGE(statusChangesList.size() == 1, "statusChangesList empty when changes detected");
    if (statusChangesList.size() == 1)
    {
        TEST_ASSERT_MESSAGE(statusChangesList[0].address == 0x55, "statusChangesList addr not 0x55");
        TEST_ASSERT_MESSAGE(!statusChangesList[0].isChangeToOnline, "statusChangesList status not offline");
    }
}

TEST_CASE("test_rafti2c_bus_scanner", "[rafti2c_busi2c_tests]")
{
    // List of status changes
    std::vector<BusElemAddrAndStatus> statusChangesList;

    // Callback for bus operating
    BusOperationStatusCB busOperationStatusCB = [](BusBase& bus, BusOperationStatus busOperationStatus) {
        LOG_I(MODULE_PREFIX, "busOperationStatusCB %d", busOperationStatus);
    };

    // Callback for bus element status
    BusElemStatusCB busElemStatusCB = [&statusChangesList](BusBase& bus, const std::vector<BusElemAddrAndStatus>& statusChanges) {

        // Append to status changes list for later checking
        if (statusChangesList.size() < 500)
        {
            for (const auto& stat : statusChanges)
            {
                statusChangesList.push_back(stat);
                LOG_I(MODULE_PREFIX, "busElemStatusCB addr %02x online %s", stat.address, stat.isChangeToOnline ? "Y" : "N");
            }
            // statusChangesList.insert(statusChangesList.end(), statusChangesNonConst.begin(), statusChangesNonConst.end());
        }
        else
        {
            LOG_E(MODULE_PREFIX, "statusChangesList full");
        }
    };

    // typedef std::function<RaftI2CCentralIF::AccessResultCode(BusI2CRequestRec* pReqRec, uint32_t pollListIdx)> BusI2CRequestFn;
    BusI2CRequestFn busOperationRequestFn = [](BusI2CRequestRec* pReqRec, uint32_t pollListIdx) {
        LOG_I(MODULE_PREFIX, "busOperationRequestFn addr 0x%02x pollListIdx %d", pReqRec->getAddress(), pollListIdx);
        return RaftI2CCentralIF::ACCESS_RESULT_OK;
    };

    // Config
    RaftJson config = "{\"lockupDetect\":\"0x55\",\"scanBoost\":[\"0x55\"]}";

    // BusBase
    BusBase busBase(busElemStatusCB, busOperationStatusCB);
    busBase.setup(config);

    // BusStatusMgr
    BusStatusMgr busStatusMgr(busBase);
    busStatusMgr.setup(config);

    // BusScanner
    BusScanner busScanner(busStatusMgr, busOperationRequestFn);
    busScanner.setup(config);

    // Service the status for some time
    for (int i = 0; i < 100; i++)
    {
        busStatusMgr.service(true);
        busScanner.service();
    }
}