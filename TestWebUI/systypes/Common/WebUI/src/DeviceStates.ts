

// export class DeviceState {
//     busName: string = "";
//     busType: string = "";
//     address: number = 0;
//     deviceType: string = "";
//     active: boolean = false;
//     lastReportTimeMs: number = 0;
// }

import { DeviceTypeInfo } from "./DeviceInfo";

export interface DeviceAttribute {
    name: string;
    newAttribute: boolean;
    newData: boolean;
    values: number[];
}

export interface DeviceAttributes {
    [attributeName: string]: DeviceAttribute;
}

export interface DeviceState {
    deviceTypeInfo: DeviceTypeInfo;
    deviceTimeline: Array<number>;
    deviceAttributes: DeviceAttributes;
    deviceRecordNew: boolean;
    deviceStateChanged: boolean;
    lastReportTimestampMs: number;
    reportTimestampOffsetMs: number;
}

export class DevicesState {
    [deviceKey: string]: DeviceState;
}

// Add the getDeviceKey method to generate a composite key
export function getDeviceKey(busName: string, devAddr: string): string {
    return `${busName}_${devAddr}`;
}

