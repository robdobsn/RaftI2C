#pragma once

#include <string>

extern "C" {
    char* itoa(int value, char* result, int base);
    char* utoa(unsigned int value, char* result, int base);
    char* ltoa(long value, char* result, int base);
    char* ultoa(unsigned long value, char* result, int base);
    char* dtostrf(double value, int width, unsigned int precision, char* result);
    char* ftoa(float value, int width, unsigned int precision, char* result);
    char* lltoa(long long value, char* result, int base);
    char* ulltoa(unsigned long long value, char* result, int base);
    size_t strlcat(char *dst, const char *src, size_t siz);
}
