[] Support for I2C mux - need a channel/sub-bus value for each device as well as address
[] ... firstly we maybe need to know what muxes we're dealing with - looks like 0x71..0x73 might be good addresses
... to assume muxes are at since nothing much else uses these according to https://i2cdevices.org/addresses 
[] ... if the only thing at addresses 0x71-0x73 are considered to be muxes then this makes things a bit simpler?
[] ... maybe first scan for changes in mux addresses - how to handle changes here???? - maybe reset entire table of devices???
[] ... maybe enable all sub-busses and scan - if any address found then investigate which sub-bus it is on ?
[] ... is it possible that an address conflict could make a device lock up? hopefully not!
[] ... assume initially no address conflict lockup - so strategy at start of every scan sequence is to enable all muxes
... then scan all devices as normal
... any devices not found are treated the way they would normally be
... for any devices actually found then go through EVERY sub-bus to determine which sub-busses devices with that address are on - may be more than one!
... only put device into the normal operation list when sub-bus(ses) known
... any devices that appear on more than one sub-bus needs to be handled in a special way - the sub-bus for the second device with same address needs to be normally disabled and then it needs to be serviced differently from everything else when polling, etc

