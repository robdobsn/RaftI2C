import { DeviceTypeInfo } from "./DeviceInfo";

export function deviceAttrGetLatestFormatted(attr: DeviceAttribute): string {

    if (attr.values.length === 0) {
        return 'N/A';
    }
    if (attr.format.length === 0) {
        return attr.values[attr.values.length - 1].toString();
    }
    const value = attr.values[attr.values.length - 1];
    const format = attr.format;
    if (format.endsWith('f')) {
        // Floating point number formatting
        const parts = format.split('.');
        let decimalPlaces = 0;
        if (parts.length === 2) {
            decimalPlaces = parseInt(parts[1], 10);
        }
        const formattedNumber = value.toFixed(decimalPlaces);
        let fieldWidth = parseInt(parts[0], 10);
        return fieldWidth ? formattedNumber.padStart(fieldWidth, ' ') : formattedNumber;
    } else if (format.endsWith('x')) {
        // Hexadecimal formatting
        const totalLength = parseInt(format.slice(0, -1), 10);
        return Math.floor(value).toString(16).padStart(totalLength, format.startsWith('0') ? '0' : ' ');
    } else if (format.endsWith('d')) {
        // Decimal integer formatting
        const totalLength = parseInt(format.slice(0, -1), 10);
        return Math.floor(value).toString(10).padStart(totalLength, format.startsWith('0') ? '0' : ' ');
    } else if (format.endsWith('b')) {
        // Binary formatting
        return Math.floor(value) === 0 ? 'no' : 'yes';
    }
    return value.toString();
}

export interface DeviceAttribute {
    name: string;
    newAttribute: boolean;
    newData: boolean;
    values: number[];
    units: string;
    range: number[];
    format: string;
    visibleSeries: boolean;
    visibleForm: boolean;
}

export interface DeviceAttributes {
    [attributeName: string]: DeviceAttribute;
}

export interface DeviceTimeline {
    timestamps: number[];
    lastReportTimestampMs: number;
    reportTimestampOffsetMs: number;
}
    
export interface DeviceState {
    deviceTypeInfo: DeviceTypeInfo;
    deviceTimeline: DeviceTimeline;
    deviceAttributes: DeviceAttributes;
    deviceIsNew: boolean;
    stateChanged: boolean;
    isOnline: boolean;
}

export class DevicesState {
    [deviceKey: string]: DeviceState;
}

// Add the getDeviceKey method to generate a composite key
export function getDeviceKey(busName: string, devAddr: string): string {
    return `${busName}_${devAddr}`;
}

