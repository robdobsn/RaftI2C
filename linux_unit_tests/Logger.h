#include <chrono>
#include "utils.h"

#define LOG_E( tag, format, ... ) fprintf(stderr, "E (%ld) %s: " format "\n", std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count(), tag, ##__VA_ARGS__);
#define LOG_W( tag, format, ... ) fprintf(stderr, "W (%ld) %s: " format "\n", std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count(), tag, ##__VA_ARGS__);
#define LOG_I( tag, format, ... ) fprintf(stderr, "I (%ld) %s: " format "\n", std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count(), tag, ##__VA_ARGS__);
#define LOG_D( tag, format, ... ) fprintf(stderr, "D (%ld) %s: " format "\n", std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count(), tag, ##__VA_ARGS__);
#define LOG_V( tag, format, ... ) fprintf(stderr, "V (%ld) %s: " format "\n", std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count(), tag, ##__VA_ARGS__);
