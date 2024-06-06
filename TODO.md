Main library
[] Current monitoring for hardware that supports it
[] Support for using a MUXED channel for power and ADCs
[] investigate idea of callback functions to do device-detection/init/polling/decoding - could define struct with values and then serialize it out - endianness TBD - perhaps include and endian-ness marker (a known 2 byte value for instance) - or just define little-endian knowing it is ok on ESP32 and needs adjustment on other platforms?
[] add more devices - Robotical servos, etc
[] consider whether there is any benefit in different devices at different speeds based on them being isolated on slots? 
[] possibly try toggling the SCL line multiple times (24 or more?) to remove bus-stuck conditions?
[] add a value validity check - maybe overlapping the range used for actual values to avoid issues with reading - perhaps on each individual polled data value or perhaps as a whole - or both
[] check ALS values on VL6180 - in general VL6180 isn't working well
[] handling of formatting of bit fields
[] need to clear bus extenders after a burst of scanning - if stuck send many clocks with SDA high or just scan address 0x77 or similar?
[] plugged into main bus - detected on slots incorrectly - try with Qwiic Button for instance
[] work out divisor on MSA301 - try out on qtpy hardware as address conflict on pwr management
[] bus stuck on LEDStick - maybe power - be good to work out if it is fixable
[] consider whether sync messages a good idea - maybe general call 0x00 address can be used? a time-stamp in ms could be sent to all devices this way
[] add a field into JSON to say that device should be tried later in the detection process - e.g. for robotical servos they need a wake-up command and this shouldn't be tried before trying other, simpler, devices
[] put polling and attributes into groups so there can be multiple polls for each device
[] consider whether custom functions for detection, initialisation and polling would be helpful - e.g. for responses of variable length in polls
[] check w value in actions in JSON dev types - should it have 0x prefix?
[] implement facility to add new device types - need to check them before the ones in DeviceTypeRecords.json and override settings for devices with same type
[] implement filters in the script which processes DeviceTypeRecords.json to: (a) limit the embedded records to a smaller number of devices, (b) enable or disable code generation, (c) enable or disable storing of verbose JSON for the schema
[] Add ability to use DecodeGenerator.py as a main program and allow for generation in python/ts on demand for a specific device type
[] Add the capability to generate JSON from decoded device information - this would require auto-generator to create the sprintf type code to format to JSON
[] Add an option to publish JSON with decoded device info instead of raw info

TestWebUI
[] ensure that only one call to get device type info is performed
[] ensure that TEST_DATA in npm start doesn't leak into non-local testing
[] TestWebUI could subtract off ms time from time of first data received from any device?
[] handling of invalid values in UI - see if chart can cope with nulls - form should maybe display N/A - or blank?
