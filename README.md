# RaftI2C

RaftI2C is part of the Raft opinionated framework for the Espressif ESP32 family. For more details about Raft see https://github.com/robdobsn/RaftCore/wiki

This is the I2C component of Raft which provides:
* I2CCentral - a component which replaces the standard I2C master functionality on ESP32 with a more robust implementation
* BusI2C - a Raft SysMod which implements an advanced I2C bus with automated scanning, detection, identification, initialization and polling of connected I2C devices. BusI2C also supports multiplexers (PCA9548A) and power control to individual I2C multiplexed slots.

A [blog post is available](https://robdobson.com/2024/04/i2c-auto-identification/) which explains how device auto-identification works. 

# Example web-app

The example app, TestWebUI, is a complete web-based application which uses BusI2C on an ESP32 and demonstrates the automation of I2C that BusI2C provides.

![I2C Auto-identification demo](./media/I2C%20auto-identification.gif)

To build the demonstration application, it is easiest to install the Raft CLI program first https://crates.io/crates/raftcli

Then use the following command line to build, flash and monitor the demonstration onto either:

- an ESP32 dev-board like the [TinyPICO](https://www.tinypico.com/) (the default is to use pins 21 and 22 for SDA and SCL respectively - see below how to change this)
- an ESP32-S3 dev-board like the [Adafruit QTPy S3](https://www.adafruit.com/product/5426) (the default is to use oins 41 and 40 for SDA and SCL respectively - see below how to change this)

```
cd ./TestWebUI
raft run -s <systype> -p <serial-port>
```

where:
- <systype> is either tinypico or qtpy
- <serial-port> is the serial port the ESP32 is connected to.

If you need to change any settings (such as SCL and SDA pins for the I2C bus), first find the systypes.json file for your particular systype (these are located in the systypes folder, so the file for the qtpy systype is in ./TestWebUI/systypes/qtpy/systypes.json). Open this file in a text editor and change the required settings. E.g. the sdaPin and sclPin fields in the HWDevMan/Buses/buslist entry for "I2CA".

