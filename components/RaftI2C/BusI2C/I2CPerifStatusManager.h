class I2CPerifStatusManager {

private:
    // I2C address response status
    class I2CAddrRespStatus
    {
    public:
        I2CAddrRespStatus()
        {
            clear();
        }
        void clear()
        {
            count = 0;
            isChange = false;
            isOnline = false;
            isValid = false;
        }
        uint8_t count : 5;
        bool isChange : 1;
        bool isOnline : 1;
        bool isValid : 1;
    };

    // I2C address response status
    I2CAddrRespStatus _i2cAddrResponseStatus[BUS_SCAN_I2C_ADDRESS_MAX+1];
    static const uint32_t I2C_ADDR_RESP_COUNT_FAIL_MAX = 3;
    static const uint32_t I2C_ADDR_RESP_COUNT_OK_MAX = 2;

};