// Host stand-ins for the FreeRTOS calls BusStatusMgr makes (forced in with -include for the host tests)
#pragma once
#include <stdint.h>
#ifndef ESP_PLATFORM
inline void* xTaskGetCurrentTaskHandle() { static int task; return &task; }
inline void vTaskDelay(uint32_t) {}
#endif
