import { DeviceTypeInfoRecs } from "./DeviceInfo";

export interface NVSettingsRecord {
    maxChartPoints?: number;
    maxStoredPoints?: number;
    i2cDevTypes?: DeviceTypeInfoRecs;
}

class SettingsManager {
    private static instance: SettingsManager;
    private settings: { [key: string]: { maxChartPoints: number; maxStoredPoints: number } } = {};
    private defaultSettings: NVSettingsRecord = { maxChartPoints: 100, maxStoredPoints: 1000 };

    // Test server path
    private _testServerPath = "";

    // Server address
    private _serverAddressPrefix = "";

    // URL prefix
    private _urlPrefix: string = "/api";

    // Modified configuration
    private _mutableConfig: NVSettingsRecord = {};

    // Config change callbacks
    private _configChangeCallbacks: Array<(config: NVSettingsRecord) => void> = [];

    private constructor() {}

    public getMaxDatapointsToStore(): number {
        return 1000;
    }

    public getMaxChartPoints(): number {
        return 100;
    }

    public static getInstance(): SettingsManager {
        if (!SettingsManager.instance) {
            SettingsManager.instance = new SettingsManager();
        }
        return SettingsManager.instance;
    }

    public getSettings(withDefaults: boolean): NVSettingsRecord {
        let settings = JSON.parse(JSON.stringify(this._mutableConfig));

        // Settings that don't exist in the mutable config are set to default values
        (Object.keys(this.defaultSettings) as Array<keyof NVSettingsRecord>).forEach(key => {
            if (!(key in settings)) {
              settings[key] = this.defaultSettings[key];
            }
          });
        return settings;
    }

    public setSettings(newSettings: NVSettingsRecord) {
        this._mutableConfig = newSettings;
    }

    ////////////////////////////////////////////////////////////////////////////
    // Config handling
    ////////////////////////////////////////////////////////////////////////////

    // // Get the config
    // public getConfig(): DevicesConfig {
    //     return this._devicesConfig;
    // }

    // // Get mutable config
    // public getMutableConfig(): DevicesConfig {
    //     return this._mutableConfig;
    // }

    // // Revert configuration
    // public revertConfig(): void {
    //     this._mutableConfig = JSON.parse(JSON.stringify(this._devicesConfig));
    //     this._configChangeCallbacks.forEach(callback => {
    //         callback(this._devicesConfig);
    //     });
    // }

    // // Persist configuration
    // public async persistConfig(): Promise<void> {

    //     // TODO - change config that needs to be changed
    //     this._devicesConfig = JSON.parse(JSON.stringify(this._mutableConfig));
    //     await this.postAppSettings();
    // }

    // // Check if config changed
    // public isConfigChanged(): boolean {
    //     return JSON.stringify(this._devicesConfig) !== JSON.stringify(this._mutableConfig);
    // }

    // // Register a config change callback
    // public onConfigChange(callback: (config:DevicesConfig) => void): void {
    //     // Add the callback
    //     this._configChangeCallbacks.push(callback);
    // }

    ////////////////////////////////////////////////////////////////////////////
    // Get the configuration from the main server
    ////////////////////////////////////////////////////////////////////////////

    public async getAppSettingsAndCheck(): Promise<boolean> {

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
    // Get the app settings from the server
    ////////////////////////////////////////////////////////////////////////////

    async getAppSettings(serverAddr:string) : Promise<boolean> {
        // Get the app settings
        console.log(`SettingsManager getAppSettings getting app settings`);
        let getSettingsResponse = null;
        try {
            getSettingsResponse = await fetch(serverAddr + this._urlPrefix + "/getsettings/nv");
            if (getSettingsResponse && getSettingsResponse.ok) {
                const settings = await getSettingsResponse.json();

                console.log(`SettingsManager getAppSettings ${JSON.stringify(settings)}`)

                if ("nv" in settings) {

                    // Get new settings
                    const newSettings = JSON.parse(JSON.stringify(settings.nv));

                    // Check for changes
                    const changesDetected = JSON.stringify(this._mutableConfig) !== JSON.stringify(newSettings);

                    // Update the mutable config
                    this._mutableConfig = newSettings;

                    // Inform listeners of config change
                    if (changesDetected) {
                        this._configChangeCallbacks.forEach(callback => {
                            callback(this._mutableConfig);
                        });

                        console.log(`SettingsManager getAppSettings config changed ${JSON.stringify(this._mutableConfig)}`)
                    }
                } else {
                    console.warn("SettingsManager getAppSettings settings missing nv section");
                }
                return true;
            } else {
                console.warn("SettingsManager getAppSettings response not ok");
            }
        } catch (error) {
            console.warn(`SettingsManager getAppSettings ${error}`);
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
                    body: JSON.stringify(this._mutableConfig).replace("\n", "\\n")
                }
            );

            console.log(`DeviceManager postAppSettings posted ${JSON.stringify(this._mutableConfig)}`)

            if (!postSettingsResponse.ok) {
                console.error(`DeviceManager postAppSettings response not ok ${postSettingsResponse.status}`);
            }
            return postSettingsResponse.ok;
        } catch (error) {
            console.error(`DeviceManager postAppSettings error ${error}`);
            return false;
        }
    }
}

export default SettingsManager;
