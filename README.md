# RaftI2C

RaftI2C is the I2C component library for the [Raft](https://github.com/robdobsn/RaftCore) ESP32 framework.

It provides two main layers:

- `I2CCentral`: a low-level I2C master implementation for ESP32-class devices, intended to be more robust than the standard master path for Raft bus-management use cases.
- `BusI2C`: a Raft bus implementation with automatic scanning, device detection, device identification, initialization, polling, multiplexer support, and optional per-slot power control.

RaftI2C is designed for projects that need to discover and manage I2C devices dynamically. It supports devices connected directly to the main I2C bus and devices behind PCA9548A-style multiplexers. A background worker task handles scanning and polling, while Raft's device manager exposes discovered devices and data through the normal Raft APIs.

More Raft documentation is available in the [RaftCore wiki](https://github.com/robdobsn/RaftCore/wiki). There is also a blog post describing the auto-identification approach: [I2C Auto-identification](https://robdobson.com/2024/04/i2c-auto-identification/).

## Repository Layout

- [components/RaftI2C](components/RaftI2C): the ESP-IDF component implementation.
- [TestWebUI](TestWebUI): an ESP32 example firmware with a browser UI for viewing discovered I2C devices and live poll data.
- [unit_tests](unit_tests): ESP-IDF unit-test project.
- [linux_unit_tests](linux_unit_tests): host-side tests and generated records used during development.
- [scripts](scripts): helper scripts for generated device records and related tooling.

## Using The Library

In an ESP-IDF/Raft application, include RaftI2C as a component and register the I2C bus type during startup:

```cpp
#include "BusI2C.h"

raftBusSystem.registerBus("I2C", BusI2C::createFn);
```

The bus instances themselves are created from the active systype configuration under `DevMan/Buses/buslist`. A minimal bus entry looks like this:

```json
{
	"name": "I2CA",
	"type": "I2C",
	"sdaPin": 8,
	"sclPin": 9,
	"i2cFreq": 100000
}
```

More advanced configurations can add fields such as `lockupDetect`, `mux`, `ioExps`, `pwr`, and `slotControl` for multiplexed buses, IO expanders, per-slot power, and per-slot I2C/serial mode control.

## Example Firmware And Web App

[TestWebUI](TestWebUI) is a complete Raft firmware example. It runs `BusI2C`, hosts a web server, and serves a React-based WebUI from the ESP32 file system. The UI is built during the firmware build, copied into the LittleFS image, flashed to the device, and then accessed from a web browser.

![I2C Auto-identification demo](./media/I2C%20auto-identification.gif)

The WebUI source is in [TestWebUI/systypes/Common/WebUI](TestWebUI/systypes/Common/WebUI). The common features file enables the web app and LittleFS image generation:

```cmake
set(FS_TYPE "littlefs")
set(FS_IMAGE_PATH "../Common/FSImage")
set(UI_SOURCE_PATH "../Common/WebUI")
```

The build runs the WebUI build, copies the generated files into the filesystem image area, compresses assets by default, creates `fs.bin`, and flashes it alongside the firmware image.

The browser UI connects directly to the device using Raft RICJSON messages on the `devjson` WebSocket topic. It does not depend on the separate `raftjs` package. Generic fixed-layout poll responses are decoded from the device type metadata, while devices with packed FIFO responses need a matching custom decoder in the WebUI. The example currently includes custom FIFO decoding for `MAX30101` and `LSM6DS`.

## Prerequisites

Install:

- ESP-IDF for the target ESP32 family.
- The Raft CLI: <https://crates.io/crates/raftcli>
- Node.js and npm for the WebUI build.

On WSL, make sure `node` and `npm` resolve to Linux binaries, not Windows `npm.cmd`. A quick check is:

```bash
which node
which npm
node --version
npm --version
```

## Build And Flash The Example

From the example folder:

```bash
cd TestWebUI
raft build -i -s RoboticalAxiom1 -c
raft flash -s RoboticalAxiom1
```

Common development shortcuts are:

```bash
# Build only
raft b -i -s RoboticalAxiom1

# Clean build
raft b -i -s RoboticalAxiom1 -c

# Build, flash, and monitor
raft r -i -s RoboticalAxiom1
```

Available example systypes currently include:

- `RoboticalAxiom1`: ESP32-S3, USB Serial/JTAG console, I2C on SDA 8 / SCL 9, plus mux, IO expander, power, and slot-control configuration.
- `qtpys3`: ESP32-S3, USB Serial/JTAG console.
- `tinypico`: ESP32, UART console.

The active systype config is merged from [TestWebUI/systypes/Common](TestWebUI/systypes/Common) and the selected systype folder. For example, `RoboticalAxiom1` adds its board-specific `SysTypes.json` and `sdkconfig.defaults` on top of the common defaults.

After flashing, connect to the serial monitor and look for startup logs from `RaftI2CBusI2C`, `SerialConsole`, `WebServer`, and periodic system reports. The WebUI is served by the device's web server. Once the device is on the network, open the device IP address in a browser, for example:

```text
http://192.168.1.250/
```

If station Wi-Fi is not configured or available, connect to the configured access point instead and browse to the device on that network.

## WebUI Development

The firmware build handles WebUI bundling automatically, but the UI can also be run locally while developing it:

```bash
cd TestWebUI/systypes/Common/WebUI
npm install
npm run start
```

To create the production WebUI bundle manually:

```bash
npm run build
```

The production build writes to `dist`. During the ESP-IDF/Raft build, those files are copied into the generated LittleFS image and flashed to the device file-system partition.

## Systype Configuration

Each systype can override the common firmware and WebUI settings:

- `features.cmake`: target chip, Raft components, file-system settings, WebUI source path.
- `sdkconfig.defaults`: ESP-IDF defaults such as console transport, partition table, flash size, and target-specific options.
- `SysTypes.json`: Raft runtime configuration, including buses, devices, network services, and sysmods.

To change I2C pins, edit the selected systype's `SysTypes.json` under:

```text
DevMan -> Buses -> buslist
```

For example:

```json
{
	"name": "I2CA",
	"type": "I2C",
	"sdaPin": 8,
	"sclPin": 9,
	"i2cFreq": 100000
}
```

Console settings are intentionally target-specific. ESP32-S3 boards that expose USB Serial/JTAG should use `CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y`. ESP32 boards such as TinyPICO generally use the UART console instead.

## Tests

Host-side tests can be built from [linux_unit_tests](linux_unit_tests):

```bash
cd linux_unit_tests
make all
```

ESP-IDF unit tests live under [unit_tests](unit_tests) and use the normal ESP-IDF/Raft build flow for the `unittest` systype.

## Troubleshooting

If the build fails while generating the WebUI, check that Linux `node` and `npm` are installed and first on `PATH`. In WSL, accidentally invoking Windows npm can fail when the build path is a WSL UNC path.

If serial output appears to stop after early boot messages, check the selected systype's console setting. Early ROM/bootloader output may appear on a USB serial port even when the application console later switches to UART0. For ESP32-S3 USB Serial/JTAG monitoring, the generated sdkconfig should contain:

```text
CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y
```

If a selected systype appears to use the wrong I2C pins, inspect the generated merged config:

```bash
cat TestWebUI/build/<systype>/raft/SysTypes.json.merged
```

This file shows the actual runtime configuration compiled into the firmware.

If a device is detected but does not show live values in the WebUI, check the device type's `devInfoJson.resp` metadata. Simple responses are decoded automatically from the attribute list, but records with a custom `resp.c.n` handler, such as `lsm6ds_fifo`, must also be implemented in [TestWebUI/systypes/Common/WebUI/src/CustomAttrHandler.ts](TestWebUI/systypes/Common/WebUI/src/CustomAttrHandler.ts).

