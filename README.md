# RaftI2C

Raft is a framework for ESP32 development which comprises:
- Configuration system using JSON config files and overridable options
- Communications chanels supporting BLE, WiFi & WebSockets and USB serial with consistent messaging protocols
- WebServer
- I2C polling and device management model
- Flexible publishing mechanism for high speed outbound data comms
- REST API for imperative commands
- Audio streaming

Supported devices:
- ESP32
- ESP32 S3

Supported frameworks:
- ESP IDF
- Arduino

This is the I2C component of Raft which provides I2C bus functionality
