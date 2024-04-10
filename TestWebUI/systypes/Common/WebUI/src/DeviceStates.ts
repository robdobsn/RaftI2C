
export class DeviceState {
    busName: string = "";
    i2cAddress: number = 0;
    i2cSlot: number = 0;
    deviceType: string = "";
    active: boolean = false;
    lastReportTimeMs: number = 0;
    lastReportData: string = "";
}

export class DeviceStates {
    devices: Array<DeviceState> = [];
}

