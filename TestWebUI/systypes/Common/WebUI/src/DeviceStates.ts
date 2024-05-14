import { DeviceTypeInfo } from "./DeviceInfo";

export function deviceAttrGetLatestFormatted(attrState: DeviceAttributeState): string {

    if (attrState.values.length === 0) {
        return 'N/A';
    }
    if (attrState.format.length === 0) {
        return attrState.values[attrState.values.length - 1].toString();
    }
    const value = attrState.values[attrState.values.length - 1];
    const format = attrState.format;
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

export interface DeviceAttributeState {
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

export interface DeviceAttributesState {
    [attributeName: string]: DeviceAttributeState;
}

export interface DeviceTimeline {
    timestampsUs: number[];
    lastReportTimestampUs: number;
    reportTimestampOffsetUs: number;
}
    
export interface DeviceState {
    deviceTypeInfo: DeviceTypeInfo;
    deviceTimeline: DeviceTimeline;
    deviceAttributes: DeviceAttributesState;
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

