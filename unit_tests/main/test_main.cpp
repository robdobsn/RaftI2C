/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Unit Testing for I2C
//
// Rob Dobson 2024
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "sdkconfig.h"
#include <stdio.h>
#include <string.h>
#include "unity.h"
#include "unity_test_runner.h"
#include "nvs_flash.h"

// FreeRTOS
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

void setUp(void)
{
}

void tearDown(void)
{
}

extern "C" void app_main(void)
{
    // Wait to allow serial port to be opened
    vTaskDelay(2000);

    // Initialize flash
    esp_err_t flashInitResult = nvs_flash_init();
    if (flashInitResult != ESP_OK)
    {
        // Error message
        printf("nvs_flash_init() failed with error %s (%d)\n", esp_err_to_name(flashInitResult), flashInitResult);

        // Clear flash if possible
        if ((flashInitResult == ESP_ERR_NVS_NO_FREE_PAGES) || (flashInitResult == ESP_ERR_NVS_NEW_VERSION_FOUND))
        {
            esp_err_t flashEraseResult = nvs_flash_erase();
            if (flashEraseResult != ESP_OK)
            {
                printf("nvs_flash_erase() failed with error %s (%d)\n", 
                                esp_err_to_name(flashEraseResult), flashEraseResult);
            }
            flashInitResult = nvs_flash_init();
            if (flashInitResult != ESP_OK)
            {
                // Error message
                printf("nvs_flash_init() failed a second time with error %s (%d)\n", 
                                esp_err_to_name(flashInitResult), flashInitResult);
            }
        }
    }

    // Run registered tests
    UNITY_BEGIN();
    unity_run_all_tests();
    UNITY_END();

    // Run menu
    unity_run_menu();
}
