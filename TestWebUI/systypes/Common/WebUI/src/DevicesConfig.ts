
export class SingleDeviceConfig {
    i2cAddress: number = 0;
    i2cSlot: number = 0;
    name: string = "";
}

export class DevicesConfig {
    devices: Array<SingleDeviceConfig> = [];
}

