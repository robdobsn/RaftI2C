#pragma once
#include <stdint.h>

struct poll_VCNL4040 {
    uint32_t timeMs;
    uint16_t prox;
    float als;
    float white;
};

struct poll_VL6180 {
    uint32_t timeMs;
    bool valid;
    float dist;
};

struct poll_MAX30101 {
    uint32_t timeMs;
    uint32_t Red;
    uint32_t IR;
};

struct poll_ADXL313 {
    uint32_t timeMs;
    float x;
    float y;
    float z;
};

struct poll_AHT20 {
    uint32_t timeMs;
    uint8_t status;
    float humidity;
    float temperature;
};

struct poll_MCP9808 {
    uint32_t timeMs;
    float temperature;
};

struct poll_LPS25 {
    uint32_t timeMs;
    uint8_t status;
    float pressure;
    float temperature;
};

struct poll_CAP1203 {
    uint32_t timeMs;
    bool A;
    bool B;
    bool C;
    uint16_t status;
};

struct poll_Qwiic_Button {
    uint32_t timeMs;
    bool press;
};

struct poll_Qwiic_LED_Stick {
    uint32_t timeMs;
};