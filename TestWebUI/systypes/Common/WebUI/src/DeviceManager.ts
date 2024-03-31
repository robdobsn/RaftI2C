import { DeviceConfig } from "./DeviceConfig";
import { DeviceStates } from "./DeviceStates";

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
    private _DeviceConfig = new DeviceConfig();

    // Modified configuration
    private _mutableConfig = new DeviceConfig();

    // Config change callbacks
    private _configChangeCallbacks: Array<(config: DeviceConfig) => void> = [];

    // State change callbacks
    private _stateChangeCallback: (state: DeviceStates) => void = (state: DeviceStates) => {
        console.log(`DeviceManager default state change ${JSON.stringify(state)}`);
    }

    // Last time we got a state update
    private _lastStateUpdate: number = 0;
    private MAX_TIME_BETWEEN_STATE_UPDATES_MS: number = 60000;

    // Websocket
    private _websocket: WebSocket | null = null;

    // Get instance
    public static getInstance(): DeviceManager {
        if (!DeviceManager._instance) {
            DeviceManager._instance = new DeviceManager();
        }
        return DeviceManager._instance;
    }

    // Constructor
    private constructor() {
        console.log("DeviceManager constructed");
    }

    ////////////////////////////////////////////////////////////////////////////
    // Send REST commands
    ////////////////////////////////////////////////////////////////////////////

    async sendCommand(cmd: string): Promise<boolean> {
        try {
            const sendCommandResponse = await fetch(this._serverAddressPrefix + this._urlPrefix + cmd);
            if (!sendCommandResponse.ok) {
                console.log(`DeviceManager sendCommand response not ok ${sendCommandResponse.status}`);
            }
            return sendCommandResponse.ok;
        } catch (error) {
            console.log(`DeviceManager sendCommand error ${error}`);
            return false;
        }
    }

    ////////////////////////////////////////////////////////////////////////////
    // Init
    ////////////////////////////////////////////////////////////////////////////

    public async init(): Promise<boolean> {
        // Check if already initialized
        if (this._websocket) {
            console.log(`DeviceManager init already initialized`)
            return true;
        }
        console.log(`DeviceManager init - first time`)

        await this.getAppSettingsAndCheck();

        const rslt = await this.connectWebSocket();

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
                console.log("DeviceManager init unable to get app settings");
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
            webSocketURL += "/devices";
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
                let data: DeviceStates;
                try {
                    data = JSON.parse(event.data) as DeviceStates;
                    console.log(`DeviceManager websocket message ${JSON.stringify(data)}`);
                } catch (error) {
                    console.error(`DeviceManager websocket message error ${error} msg ${event.data}`);
                    return;
                }
                // Update the last state update time
                this._lastStateUpdate = Date.now();
                // Use the callback to update the state
                this._stateChangeCallback(data);
            }
            this._websocket.onclose = () => {
                console.log(`DeviceManager websocket closed`);
                this._websocket = null;
            }
            this._websocket.onerror = (error) => {
                console.log(`DeviceManager websocket error ${error}`);
            }
        }
        catch (error) {
            console.log(`DeviceManager websocket error ${error}`);
            return false;
        }
        return true;
    }

    ////////////////////////////////////////////////////////////////////////////
    // Config handling
    ////////////////////////////////////////////////////////////////////////////

    // Get the config
    public getConfig(): DeviceConfig {
        return this._DeviceConfig;
    }

    // Get mutable config
    public getMutableConfig(): DeviceConfig {
        return this._mutableConfig;
    }

    // Revert configuration
    public revertConfig(): void {
        this._mutableConfig = JSON.parse(JSON.stringify(this._DeviceConfig));
        this._configChangeCallbacks.forEach(callback => {
            callback(this._DeviceConfig);
        });
    }

    // Persist configuration
    public async persistConfig(): Promise<void> {

        // TODO - change config that needs to be changed
        this._DeviceConfig = JSON.parse(JSON.stringify(this._mutableConfig));
        await this.postAppSettings();
    }

    // Check if config changed
    public isConfigChanged(): boolean {
        return JSON.stringify(this._DeviceConfig) !== JSON.stringify(this._mutableConfig);
    }

    // Register a config change callback
    public onConfigChange(callback: (config:DeviceConfig) => void): void {
        // Add the callback
        this._configChangeCallbacks.push(callback);
    }

    // Register a state change callback
    public onStateChange(callback: (state: DeviceStates) => void): void {
        // Add the callback
        this._stateChangeCallback = callback;
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
                    const configBase = new DeviceConfig();

                    console.log(`DeviceManager getAppSettings empty config ${JSON.stringify(configBase)}`);

                    // Add in the non-volatile settings
                    this.addNonVolatileSettings(configBase, settings.nv);

                    console.log(`DeviceManager getAppSettings config with nv ${JSON.stringify(configBase)}`);

                    // Extract non-volatile settings
                    this._DeviceConfig = configBase;
                    this._mutableConfig = JSON.parse(JSON.stringify(configBase));

                    // Inform screens of config change
                    this._configChangeCallbacks.forEach(callback => {
                        callback(this._DeviceConfig);
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
                    body: JSON.stringify(this._DeviceConfig).replace("\n", "\\n")
                }
            );

            console.log(`DeviceManager postAppSettings posted ${JSON.stringify(this._DeviceConfig)}`)

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

    private addNonVolatileSettings(config:DeviceConfig, nv:DeviceConfig) {
        // Iterate over keys in nv
        let key: keyof DeviceConfig;
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
}
