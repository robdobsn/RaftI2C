import { DevicesConfig } from "./DevicesConfig";
import { DeviceAttribute, DevicesState, DeviceState, getDeviceKey } from "./DeviceStates";
import { DeviceMsgJson } from "./DeviceMsg";
import { getAttrTypeBits, DeviceTypeInfo, DeviceTypeAttribute, DeviceTypeInfoTestJsonFile, isAttrTypeSigned, decodeAttrUnitsEncoding, DeviceTypeAction } from "./DeviceInfo";
import struct, { DataType } from 'python-struct';
import TestDataGen from "./TestDataGen";

let testingDeviceTypeRecsConditionalLoadPromise: Promise<any> | null = null;
if (process.env.TEST_DATA) {
    testingDeviceTypeRecsConditionalLoadPromise = import('../../../../../DeviceTypeRecords/DeviceTypeRecords.json');
}

export class DeviceManager {

    // Singleton
    private static _instance: DeviceManager;

    // Test server path
    private _testServerPath = "";

    // Server address
    private _serverAddressPrefix = "";

    // URL prefix
    private _urlPrefix: string = "/api";

    // Device configuration
    private _devicesConfig = new DevicesConfig();

    // Modified configuration
    private _mutableConfig = new DevicesConfig();

    // Config change callbacks
    private _configChangeCallbacks: Array<(config: DevicesConfig) => void> = [];

    // Devices state
    private _devicesState = new DevicesState();

    // Device callbacks
    private _callbackNewDevice: ((deviceKey: string, state: DeviceState) => void) | null = null;
    private _callbackNewDeviceAttribute: ((deviceKey: string, attr: DeviceAttribute) => void) | null = null;
    private _callbackNewAttributeData: ((deviceKey: string, attr: DeviceAttribute) => void) | null = null;

    // Last time we got a state update
    private _lastStateUpdate: number = 0;
    private MAX_TIME_BETWEEN_STATE_UPDATES_MS: number = 60000;

    // Device data series max values to store
    private MAX_DATA_POINTS_TO_STORE = 100;

    // Websocket
    private _websocket: WebSocket | null = null;

    // Message timestamp size
    private MSG_TIMESTAMP_SIZE_HEX_CHARS = 4;
    private MSG_TIMESTAMP_WRAP_VALUE = 65536;

    // Get instance
    public static getInstance(): DeviceManager {
        if (!DeviceManager._instance) {
            DeviceManager._instance = new DeviceManager();
        }
        return DeviceManager._instance;
    }

    public getDevicesState(): DevicesState {
        return this._devicesState;
    }

    public getDeviceState(deviceKey: string): DeviceState {
        return this._devicesState[deviceKey];
    }

    // Test device type data
    private _testDeviceTypeRecs: DeviceTypeInfoTestJsonFile | null = null;
    private _testDataGen = new TestDataGen();

    // Constructor
    private constructor() {
        // Check if test mode
        if (window.location.hostname === "localhost") {
            this._testDataGen.start((msg: string) => {
                this.handleClientMsgJson(msg);
            });
        }
    }

    ////////////////////////////////////////////////////////////////////////////
    // Send REST commands
    ////////////////////////////////////////////////////////////////////////////

    async sendCommand(cmd: string): Promise<boolean> {
        try {
            const sendCommandResponse = await fetch(this._serverAddressPrefix + this._urlPrefix + cmd);
            if (!sendCommandResponse.ok) {
                console.warn(`DeviceManager sendCommand response not ok ${sendCommandResponse.status}`);
            }
            return sendCommandResponse.ok;
        } catch (error) {
            console.warn(`DeviceManager sendCommand error ${error}`);
            return false;
        }
    }

    ////////////////////////////////////////////////////////////////////////////
    // Init
    ////////////////////////////////////////////////////////////////////////////

    public async init(): Promise<boolean> {
        // Check if already initialized
        if (this._websocket) {
            console.warn(`DeviceManager init already initialized`)
            return true;
        }
        console.log(`DeviceManager init - first time`)

        // Get the configuration from the main server
        await this.getAppSettingsAndCheck();

        // Open websocket
        const rslt = await this.connectWebSocket();

        // Conditionally load the device type records
        if (testingDeviceTypeRecsConditionalLoadPromise) {
            testingDeviceTypeRecsConditionalLoadPromise.then((jsonData) => {
                this._testDeviceTypeRecs = jsonData as DeviceTypeInfoTestJsonFile;
            });
        }

        // Start timer to check for websocket reconnection
        setInterval(async () => {
            if (!this._websocket) {
                console.log(`DeviceManager init - reconnecting websocket`);
                await this.connectWebSocket();
            }
            else if ((Date.now() - this._lastStateUpdate) > this.MAX_TIME_BETWEEN_STATE_UPDATES_MS) {
                const inactiveTimeSecs = ((Date.now() - this._lastStateUpdate) / 1000).toFixed(1);
                if (this._websocket) {
                    console.log(`DeviceManager init - closing websocket due to ${inactiveTimeSecs}s inactivity`);
                    this._websocket.close();
                    this._websocket = null;
                }
            }
            console.log(`websocket state ${this._websocket?.readyState}`);
        }, 5000);

        return rslt;
    }

    ////////////////////////////////////////////////////////////////////////////
    // Get the configuration from the main server
    ////////////////////////////////////////////////////////////////////////////

    private async getAppSettingsAndCheck(): Promise<boolean> {

        // Get the configuration from the main server
        const appSettingsResult = await this.getAppSettings("");
        if (!appSettingsResult) {
            
            // See if test-server is available
            const appSettingAlt = await this.getAppSettings(this._testServerPath);
            if (!appSettingAlt) {
                console.warn("DeviceManager init unable to get app settings");
                return false;
            }
            this._serverAddressPrefix = this._testServerPath;
        }
        return true;
    }

    ////////////////////////////////////////////////////////////////////////////
    // Open websocket
    ////////////////////////////////////////////////////////////////////////////

    private async connectWebSocket(): Promise<boolean> {
        // Open a websocket to the server
        try {
            console.log(`DeviceManager init location.origin ${window.location.origin} ${window.location.protocol} ${window.location.host} ${window.location.hostname} ${window.location.port} ${window.location.pathname} ${window.location.search} ${window.location.hash}`)
            let webSocketURL = this._serverAddressPrefix;
            if (webSocketURL.startsWith("http")) {
                webSocketURL = webSocketURL.replace(/^http/, 'ws');
            } else {
                webSocketURL = window.location.origin.replace(/^http/, 'ws');
            }
            webSocketURL += "/devjson";
            console.log(`DeviceManager init opening websocket ${webSocketURL}`);
            this._websocket = new WebSocket(webSocketURL);
            if (!this._websocket) {
                console.error("DeviceManager init unable to create websocket");
                return false;
            }
            this._websocket.binaryType = "arraybuffer";
            this._lastStateUpdate = Date.now();
            this._websocket.onopen = () => {
                // Debug
                console.log(`DeviceManager init websocket opened to ${webSocketURL}`);

                // Send subscription request messages after a short delay
                setTimeout(() => {

                    // Subscribe to device messages
                    const subscribeName = "devices";
                    console.log(`DeviceManager init subscribing to ${subscribeName}`);
                    if (this._websocket) {
                        this._websocket.send(JSON.stringify({
                            cmdName: "subscription",
                            action: "update",
                            pubRecs: [
                                {name: subscribeName, msgID: subscribeName, rateHz: 0.1},
                            ]
                        }));
                    }
                }, 1000);
            }
            this._websocket.onmessage = (event) => {
                this.handleClientMsgJson(event.data);
            }
            this._websocket.onclose = () => {
                console.log(`DeviceManager websocket closed`);
                this._websocket = null;
            }
            this._websocket.onerror = (error) => {
                console.warn(`DeviceManager websocket error ${error}`);
            }
        }
        catch (error) {
            console.warn(`DeviceManager websocket error ${error}`);
            return false;
        }
        return true;
    }

    ////////////////////////////////////////////////////////////////////////////
    // Config handling
    ////////////////////////////////////////////////////////////////////////////

    // Get the config
    public getConfig(): DevicesConfig {
        return this._devicesConfig;
    }

    // Get mutable config
    public getMutableConfig(): DevicesConfig {
        return this._mutableConfig;
    }

    // Revert configuration
    public revertConfig(): void {
        this._mutableConfig = JSON.parse(JSON.stringify(this._devicesConfig));
        this._configChangeCallbacks.forEach(callback => {
            callback(this._devicesConfig);
        });
    }

    // Persist configuration
    public async persistConfig(): Promise<void> {

        // TODO - change config that needs to be changed
        this._devicesConfig = JSON.parse(JSON.stringify(this._mutableConfig));
        await this.postAppSettings();
    }

    // Check if config changed
    public isConfigChanged(): boolean {
        return JSON.stringify(this._devicesConfig) !== JSON.stringify(this._mutableConfig);
    }

    // Register a config change callback
    public onConfigChange(callback: (config:DevicesConfig) => void): void {
        // Add the callback
        this._configChangeCallbacks.push(callback);
    }

    // Register state change callbacks
    public onNewDevice(callback: (deviceKey: string, state: DeviceState) => void): void {
        // Save the callback
        this._callbackNewDevice = callback;
    }
    public onNewDeviceAttribute(callback: (deviceKey: string, attr: DeviceAttribute) => void): void {
        // Save the callback
        this._callbackNewDeviceAttribute = callback;
    }
    public onNewAttributeData(callback: (deviceKey: string, attr: DeviceAttribute) => void): void {
        // Save the callback
        this._callbackNewAttributeData = callback;
    }
    
    ////////////////////////////////////////////////////////////////////////////
    // Get the app settings from the server
    ////////////////////////////////////////////////////////////////////////////

    async getAppSettings(serverAddr:string) : Promise<boolean> {
        // Get the app settings
        console.log(`DeviceManager getting app settings`);
        let getSettingsResponse = null;
        try {
            getSettingsResponse = await fetch(serverAddr + this._urlPrefix + "/getsettings/nv");
            if (getSettingsResponse && getSettingsResponse.ok) {
                const settings = await getSettingsResponse.json();

                console.log(`DeviceManager getAppSettings ${JSON.stringify(settings)}`)

                if ("nv" in settings) {

                    // Start with a base config
                    const configBase = new DevicesConfig();

                    console.log(`DeviceManager getAppSettings empty config ${JSON.stringify(configBase)}`);

                    // Add in the non-volatile settings
                    this.addNonVolatileSettings(configBase, settings.nv);

                    console.log(`DeviceManager getAppSettings config with nv ${JSON.stringify(configBase)}`);

                    // Extract non-volatile settings
                    this._devicesConfig = configBase;
                    this._mutableConfig = JSON.parse(JSON.stringify(configBase));

                    // Inform screens of config change
                    this._configChangeCallbacks.forEach(callback => {
                        callback(this._devicesConfig);
                    });
                } else {
                    alert("DeviceManager getAppSettings settings missing nv section");
                }
                return true;
            } else {
                alert("DeviceManager getAppSettings response not ok");
            }
        } catch (error) {
            console.log(`DeviceManager getAppSettings ${error}`);
            return false;
        }
        return false;
    }

    ////////////////////////////////////////////////////////////////////////////
    // Post applicaton settings
    ////////////////////////////////////////////////////////////////////////////

    async postAppSettings(): Promise<boolean> {
        try {
            const postSettingsURI = this._serverAddressPrefix + this._urlPrefix + "/postsettings/reboot";
            const postSettingsResponse = await fetch(postSettingsURI, 
                {
                    method: "POST",
                    headers: {
                        "Content-Type": "application/json"
                    },
                    body: JSON.stringify(this._devicesConfig).replace("\n", "\\n")
                }
            );

            console.log(`DeviceManager postAppSettings posted ${JSON.stringify(this._devicesConfig)}`)

            if (!postSettingsResponse.ok) {
                console.error(`DeviceManager postAppSettings response not ok ${postSettingsResponse.status}`);
            }
            return postSettingsResponse.ok;
        } catch (error) {
            console.error(`DeviceManager postAppSettings error ${error}`);
            return false;
        }
    }

    ////////////////////////////////////////////////////////////////////////////
    // Add non-volatile settings to the config
    ////////////////////////////////////////////////////////////////////////////

    private addNonVolatileSettings(config:DevicesConfig, nv:DevicesConfig) {
        // Iterate over keys in nv
        let key: keyof DevicesConfig;
        for (key in nv) {
            // Check if key exists in config
            if (!(key in config)) {
                console.log(`DeviceManager addNonVolatileSettings key ${key} not in config`);
                continue;
            }
            Object.assign(config[key], nv[key])
        }
    }

    ////////////////////////////////////////////////////////////////////////////
    // Set the friendly name for the device
    ////////////////////////////////////////////////////////////////////////////

    public async setFriendlyName(friendlyName:string): Promise<void> {
        try {
            await fetch(this._serverAddressPrefix + this._urlPrefix + "/friendlyname/" + friendlyName);
        } catch (error) {
            console.log(`DeviceManager setFriendlyName ${error}`);
        }
    }

    ////////////////////////////////////////////////////////////////////////////
    // Handle device message JSON
    ////////////////////////////////////////////////////////////////////////////

    private handleClientMsgJson(jsonMsg: string) {

        const removeDevicesNoLongerPresent = true;

        let data = JSON.parse(jsonMsg) as DeviceMsgJson;
        // console.log(`DeviceManager websocket message ${JSON.stringify(data)}`);

        // Iterate over the buses
        Object.entries(data).forEach(([busName, devices]) => {

            // Check for bus status info
            if (devices && typeof devices === "object" && "_s" in devices) {
                // console.log(`DeviceManager bus status ${JSON.stringify(devices._s)}`);
                return;
            }

            // Get a list of keys for the current devicesState
            const deviceKeysToRemove = Object.keys(this._devicesState);
            
            // Iterate over the devices
            Object.entries(devices).forEach(async ([devAddr, attrGroups]) => {

                // Check for non-device info (starts with _)
                if (devAddr.startsWith("_")) {
                    return;
                }
                
                // Device key
                const deviceKey = getDeviceKey(busName, devAddr);

                // Remove from the list of keys for the current devicesState
                const idx = deviceKeysToRemove.indexOf(deviceKey);
                if (idx >= 0) {
                    deviceKeysToRemove.splice(idx, 1);
                }

                // Check if a device state already exists
                if (!(deviceKey in this._devicesState)) {
                    
                    let deviceTypeName = "";
                    if (attrGroups && typeof attrGroups === 'object' && "_t" in attrGroups && typeof attrGroups._t === "string") {
                        deviceTypeName = attrGroups._t || "";
                    } else {
                        console.warn(`DeviceManager missing device type attrGroups ${JSON.stringify(attrGroups)}`);
                        return;
                    }

                    // Create device record
                    this._devicesState[deviceKey] = {
                        deviceTypeInfo: await this.getDeviceTypeInfo(busName, devAddr, deviceTypeName),
                        deviceTimeline: [],
                        deviceAttributes: {},
                        deviceRecordNew: true,
                        deviceStateChanged: false,
                        lastReportTimestampMs: 0,
                        reportTimestampOffsetMs: 0,
                        deviceIsOnline: true
                    };
                }
                
                // Check for online/offline state information
                if (attrGroups && typeof attrGroups === "object" && "_o" in attrGroups) {
                    this._devicesState[deviceKey].deviceIsOnline = ((attrGroups._o === "1") || (attrGroups._o === 1));
                }

                // Iterate attribute groups
                Object.entries(attrGroups).forEach(([attrGroup, msgHexStr]) => {

                    // Check valid
                    if (attrGroup.startsWith("_") || (typeof msgHexStr != 'string')) {
                        return;
                    }

                    // Work through the message which may contain multiple data instances
                    let msgHexStrIdx = 0;

                    // Loop
                    while (msgHexStrIdx < msgHexStr.length) {
                        msgHexStrIdx = this.processMsgAttrGroup(msgHexStr, msgHexStrIdx, deviceKey, attrGroup);
                        if (msgHexStrIdx < 0)
                            break;
                    }
                });
            });

            // Remove devices no longer present
            if (removeDevicesNoLongerPresent) {
                deviceKeysToRemove.forEach((deviceKey) => {
                    delete this._devicesState[deviceKey];
                });
            }

        });

        // Update the last state update time
        this._lastStateUpdate = Date.now();

        // Process the callback
        this.processStateCallback();

    }

    private findAttrTypeIndexAfterAt(s: string): number {
        // This regular expression looks for '@' followed by digits and captures the first non-digit character that follows
        const regex = /@(\d+)(\D)/;
        const match = s.match(regex);
    
        if (match && match.index !== undefined) {
            // Return the index of the captured non-digit character, which is at index 0 of the match plus the length of '@' and the digits
            return match.index + match[1].length + 1;
        }
    
        return 0;
    }

    private processMsgAttrGroup(msgHexStr: string, msgHexStrIdx: number, deviceKey: string, attrGroup: string): number {

        // Check there are enough characters for the timestamp
        if (msgHexStrIdx + 4 > msgHexStr.length) {
            return -1;
        }

        // Extract timestamp which is the first MSG_TIMESTAMP_SIZE_HEX_CHARS chars of the hex string
        let timestamp = parseInt(msgHexStr.slice(msgHexStrIdx, msgHexStrIdx+this.MSG_TIMESTAMP_SIZE_HEX_CHARS), 16);

        // Check if time is before lastReportTimeMs - in which case a wrap around occurred to add on the max value
        if (timestamp < this._devicesState[deviceKey].lastReportTimestampMs) {
            this._devicesState[deviceKey].reportTimestampOffsetMs += this.MSG_TIMESTAMP_WRAP_VALUE;
        }
        this._devicesState[deviceKey].lastReportTimestampMs = timestamp;

        // Offset timestamp
        const origTimestamp = timestamp;
        timestamp += this._devicesState[deviceKey].reportTimestampOffsetMs;

        console.log(`processMsgAttrGroup msg ${msgHexStr} timestamp ${timestamp} origTimestamp ${origTimestamp} deviceKey ${deviceKey} attrGroup ${attrGroup} msgHexStrIdx ${msgHexStrIdx}`)

        // Flag indicating any attrs added
        let attrsAdded = false;
        msgHexStrIdx += this.MSG_TIMESTAMP_SIZE_HEX_CHARS;
        const msgAbsStrStartIdx = msgHexStrIdx;

        // Check for the attrGroup name in the device type info
        if (attrGroup in this._devicesState[deviceKey].deviceTypeInfo.attr) {

            // Set device state changed flag
            this._devicesState[deviceKey].deviceStateChanged = true;

            // Iterate over attributes in the group
            const devAttrDefinitions: DeviceTypeAttribute[] = this._devicesState[deviceKey].deviceTypeInfo.attr[attrGroup];
            for (let attrIdx = 0; attrIdx < devAttrDefinitions.length; attrIdx++) {

                // Get the attr definition
                const attr: DeviceTypeAttribute = devAttrDefinitions[attrIdx];
                if (!("t" in attr)) {
                    console.warn(`DeviceManager msg unknown attrGroup ${attrGroup} devkey ${deviceKey} msgHexStr ${msgHexStr} ts ${timestamp} attr ${JSON.stringify(attr)}`);
                    continue;
                }

                // Current field message string index
                let curFieldStartIdx = msgHexStrIdx;
                let attrUsesAbsPos = false;

                // Attr type can be of the form AA or AA:NN or AA:NN:MM (and @X can prefix any of the preceding options), where:
                //   AA is the python struct type (e.g. >h)
                //   NN is the number of bits to read from the message (this has to be a multiple of 4)
                //   MM is for signed (2s compliment) numbers and specifies the position of the sign bit
                //   @X means start reading from byte X of the message (after the timestamp bytes)
                // Check if @X is present
                let attrPos = 0;
                if (attr.t.startsWith("@")) {
                    const startByte = parseInt(attr.t.slice(1));
                    curFieldStartIdx = msgAbsStrStartIdx + startByte * 2;

                    // Move attrPos to the first non-digit of the attribute type
                    attrPos = this.findAttrTypeIndexAfterAt(attr.t);
                    attrUsesAbsPos = true;
                }

                // Check if outside bounds of message
                if (curFieldStartIdx >= msgHexStr.length) {
                    console.warn(`DeviceManager msg outside bounds attrGroup ${attrGroup} devkey ${deviceKey} msgHexStr ${msgHexStr} ts ${timestamp} attr ${attr.n}`);
                    continue;
                }

                // Extract the attribute type and number of bits to read
                const attrTypeOnly = attr.t.slice(attrPos)
                const attrSplit = attrTypeOnly.split(":");
                const attrStructBits = getAttrTypeBits(attrSplit[0]);
                const attrReadBits = attrSplit.length > 1 ? parseInt(attrSplit[1]) : attrStructBits;
                const attrReadHexChars = Math.ceil(attrReadBits / 4);
                let attrTypeDefForStruct = attrSplit[0];
                const signExtendableMaskSignPos = attrSplit.length > 2 ? parseInt(attrSplit[2]) : 0;

                // Extract the hex chars for the value
                let valueHexChars = msgHexStr.slice(curFieldStartIdx, curFieldStartIdx + attrReadHexChars);

                // Pad the value to the right length for the conversion
                const padDigitsReqd = Math.ceil(attrStructBits / 4) - valueHexChars.length;
                if (attrTypeDefForStruct.startsWith("<")) {
                    for (let i = 0; i < padDigitsReqd; i++)
                        valueHexChars += "0";
                } else {
                    for (let i = 0; i < padDigitsReqd; i++)
                        valueHexChars = "0" + valueHexChars;
                }

                // Check if an MM part is present
                const mmSpecifiedOnSignedValue = (signExtendableMaskSignPos > 0) && isAttrTypeSigned(attrTypeDefForStruct);
                if (mmSpecifiedOnSignedValue) {
                    // Make the conversion unsigned for now
                    attrTypeDefForStruct = attrSplit[0].toUpperCase();
                }

                // Convert the value using python-struct
                let value = 0;
                const dataBuf = Buffer.from(valueHexChars, 'hex')
                value = struct.unpack(attrSplit[0], dataBuf)[0] as number;

                // Check if sign extendable mask specified on signed value
                if (mmSpecifiedOnSignedValue) {
                    const signBitMask = 1 << (signExtendableMaskSignPos - 1);
                    const valueOnlyMask = signBitMask - 1;
                    if (value & signBitMask) {
                        value = (value & valueOnlyMask) - signBitMask;
                    } else {
                        value = value & valueOnlyMask;
                    }
                }

                // Check for simple mask
                if ("m" in attr) {
                    if (typeof attr.m === "string")
                        value = value & parseInt(attr.m, 16);
                    else if (typeof attr.m === "number")
                        value = (value & attr.m);
                }

                // Check for bit shift required
                if ("s" in attr && attr.s) {
                    if (attr.s > 0) {
                        value = value >> attr.s;
                    } else if (attr.s < 0) {
                        value = value << -attr.s;
                    }
                }

                // Check for divisor
                if ("d" in attr && attr.d) {
                    value = value / attr.d;
                }

                // Check for value to add
                if ("a" in attr && attr.a !== undefined) {
                    value += attr.a;
                }

                // console.log(`DeviceManager msg attrGroup ${attrGroup} devkey ${deviceKey} valueHexChars ${valueHexChars} msgHexStr ${msgHexStr} ts ${timestamp} attr ${attr.n} type ${attr.t} value ${value} signExtendableMaskSignPos ${signExtendableMaskSignPos} attrTypeDefForStruct ${attrTypeDefForStruct} attr ${attr}`);
                msgHexStrIdx += attrUsesAbsPos ? 0 : attrReadHexChars;

                // Check if attribute already exists in the device state
                if (attr.n in this._devicesState[deviceKey].deviceAttributes) {

                    // Limit to MAX_DATA_POINTS_TO_STORE
                    if (this._devicesState[deviceKey].deviceAttributes[attr.n].values.length >= this.MAX_DATA_POINTS_TO_STORE) {
                        this._devicesState[deviceKey].deviceAttributes[attr.n].values.shift();
                    }
                    this._devicesState[deviceKey].deviceAttributes[attr.n].values.push(value);
                    this._devicesState[deviceKey].deviceAttributes[attr.n].newData = true;
                } else {
                    this._devicesState[deviceKey].deviceAttributes[attr.n] = {
                        name: attr.n,
                        newAttribute: true,
                        newData: true,
                        values: [value],
                        units: decodeAttrUnitsEncoding(attr.u || ""),
                        range: attr.r || [0, 0],
                        format: ("f" in attr && typeof attr.f == "string") ? attr.f : "",
                        visibleSeries: "vs" in attr ? (attr.vs === 0 || attr.vs === false ? false : !!attr.vs) : true,
                        visibleForm: "vf" in attr ? (attr.vf === 0 || attr.vf === false ? false : !!attr.vf) : true,
                    };
                }
                attrsAdded = true;
            }
        }

        // If any attributes added then add the timestamp to the device timeline
        if (attrsAdded) {
            // Limit to MAX_DATA_POINTS_TO_STORE
            if (this._devicesState[deviceKey].deviceTimeline.length >= this.MAX_DATA_POINTS_TO_STORE) {
                this._devicesState[deviceKey].deviceTimeline.shift();
            }
            this._devicesState[deviceKey].deviceTimeline.push(timestamp);
        }
        return msgHexStrIdx;
    }

    private processStateCallback() {

        // Iterate over the devices
        Object.entries(this._devicesState).forEach(([deviceKey, deviceState]) => {
            
            // Check if device record is new
            if (deviceState.deviceRecordNew) {
                if (this._callbackNewDevice) {
                    this._callbackNewDevice(
                        deviceKey,
                        deviceState
                    );
                }
                deviceState.deviceRecordNew = false;
            }

            // Iterate over the attributes
            Object.entries(deviceState.deviceAttributes).forEach(([attrKey, attr]) => {
                if (attr.newAttribute) {
                    if (this._callbackNewDeviceAttribute) {
                        this._callbackNewDeviceAttribute(
                            deviceKey,
                            attr
                        );
                    }
                    attr.newAttribute = false;
                }
                if (attr.newData) {
                    if (this._callbackNewAttributeData) {
                        this._callbackNewAttributeData(
                            deviceKey,
                            attr
                        );
                    }
                    attr.newData = false;
                }
            });
        });
    }

    ////////////////////////////////////////////////////////////////////////////
    // Get device type info
    ////////////////////////////////////////////////////////////////////////////

    private async getDeviceTypeInfo(busName: string, devAddr: string, deviceType: string): Promise<DeviceTypeInfo> {

        const emptyRec = {
            "name": "Unknown",
            "desc": "Unknown",
            "manu": "Unknown",
            "type": "Unknown",
            "attr": {}
        };
        // Ensure that this._testDeviceTypeRecs and devTypes[deviceType] are properly initialized
        if (process.env.TEST_DATA) {
            if (this._testDeviceTypeRecs && this._testDeviceTypeRecs.devTypes[deviceType]) {
                return this._testDeviceTypeRecs.devTypes[deviceType].devInfoJson;
            } else {
                // Handle the case where the necessary data isn't available
                console.error("Device type info not available for:", deviceType);
                return emptyRec;
            }
        }

        // Get the device type info from the server
        try {
            const getDevTypeInfoResponse = await fetch(this._serverAddressPrefix + this._urlPrefix + "/devman/typeinfo?bus=" + busName + "&type=" + deviceType);
            if (!getDevTypeInfoResponse.ok) {
                console.error(`DeviceManager getDeviceTypeInfo response not ok ${getDevTypeInfoResponse.status}`);
                return emptyRec;
            }
            const devTypeInfo = await getDevTypeInfoResponse.json();
            if ("devinfo" in devTypeInfo) {
                return devTypeInfo.devinfo;
            }
            return emptyRec;
        } catch (error) {
            console.error(`DeviceManager getDeviceTypeInfo error ${error}`);
            return emptyRec;
        }
    }

    ////////////////////////////////////////////////////////////////////////////
    // Send action to device
    ////////////////////////////////////////////////////////////////////////////

    public sendAction(deviceKey: string, action: DeviceTypeAction, data: DataType[]): void {
        // console.log(`DeviceManager sendAction ${deviceKey} action name ${action.n} value ${value} prefix ${action.w}`);

        // Form the write bytes
        let writeBytes = action.t ? struct.pack(action.t, data) : Buffer.from([]);

        // Convert to hex string
        let writeHexStr = Buffer.from(writeBytes).toString('hex');

        // Add prefix
        writeHexStr = action.w + writeHexStr;

        // Separate the bus and address in the deviceKey (_ char)
        const devBus = deviceKey.split("_")[0]
        const devAddr = deviceKey.split("_")[1]

        // Send the action to the server
        const url = this._serverAddressPrefix + this._urlPrefix + "/devman/cmdraw?bus=" + devBus + "&addr=" + devAddr + "&hexWr=" + writeHexStr;

        console.log(`DeviceManager deviceKey ${deviceKey} action name ${action.n} value ${data} prefix ${action.w} sendAction ${url}`);
        fetch(url)
            .then(response => {
                if (!response.ok) {
                    console.error(`DeviceManager sendAction response not ok ${response.status}`);
                }
            })
            .catch(error => {
                console.error(`DeviceManager sendAction error ${error}`);
            });
    }

    ////////////////////////////////////////////////////////////////////////////
    // Send a compound action to the device
    ////////////////////////////////////////////////////////////////////////////

    public sendCompoundAction(deviceKey: string, action: DeviceTypeAction, data: DataType[][]): void {
        // console.log(`DeviceManager sendAction ${deviceKey} action name ${action.n} value ${value} prefix ${action.w}`);

        // Check if all data to be sent at once
        if (action.concat) {
            // Form a single list by flattening data
            let dataToWrite: DataType[] = [];
            for (let dataIdx = 0; dataIdx < data.length; dataIdx++) {
                dataToWrite = dataToWrite.concat(data[dataIdx]);
            }

            // Use sendAction to send this
            this.sendAction(deviceKey, action, dataToWrite);
        } else {
            // Iterate over the data
            for (let dataIdx = 0; dataIdx < data.length; dataIdx++) {

                // Create the data to write by prepending the index to the data for this index
                let dataToWrite = [dataIdx as DataType].concat(data[dataIdx]);

                // Use sendAction to send this
                this.sendAction(deviceKey, action, dataToWrite);
            }
        }
    }
}
