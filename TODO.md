[] remove TEST_DATA in npm run build option
[] use latest ESP IDF 5.2 I2C
[] TestWebUI don't cache devices - if not in JSON then remove from UI
[] Title on TestWebUI
[] TestWebUI could subtract off ms time from time of first data received from any device?
[] Power delivery code
[] Current monitoring code?
[] Support for using a MUXED channel for power and ADCs on hardware
[] add config options for reset on i2c mux devices - addr and pin for each?? - maybe pin is the same for multiple? then reset at start - and maybe in bus reset process check if this solves lockup problem??
[] investigate idea of callback functions to do device-detection/init/polling/decoding - could define struct with values and then serialize it out - endianness TBD - perhaps include and endian-ness marker (a known 2 byte value for instance) - or just define little-endian knowing it is ok on ESP32 and needs adjustment on other platforms?
[] in UI should just show devices included in current msg - and remove others? - this should be ok since messages are generated with all previously seen devices - so the ones that aren't there would be due to restart, etc
[] deal with bus generic functions
[] get working with problem devices like button and servo
[] consider whether there is any benefit in different devices at different speeds based on them being isolated on slots? 
[] possibly try toggling the SCL line multiple times (24 or more?) to remove bus-stuck conditions?
[] add a value validity check - maybe overlapping the range used for actual values to avoid issues with reading - perhaps on each individual polled data value or perhaps as a whole - or both
[] handling of invalid values in UI - see if chart can cope with nulls - form should maybe display N/A - or blank?
[] check ALS values on VL6180 - in general VL6180 isn't working well
[] handling of formatting of bit fields