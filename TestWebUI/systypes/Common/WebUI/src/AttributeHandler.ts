import CustomAttrHandler from "./CustomAttrHandler";
import { DeviceTypeAttribute, DeviceTypeAttributeGroup, decodeAttrUnitsEncoding, isAttrTypeSigned } from "./DeviceInfo";
import { DeviceAttributes, DeviceTimeline } from "./DeviceStates";
import struct, { DataType } from 'python-struct';

export default class AttributeHandler {

    // Custom attribute handler
    private _customAttrHandler = new CustomAttrHandler();

    // Message timestamp size
    private MSG_TIMESTAMP_SIZE_BYTES = 2;
    private MSG_TIMESTAMP_WRAP_VALUE = 65536;
    
    public processMsgAttrGroup(msgBuffer: Buffer, msgBufIdx: number, deviceTimeline: DeviceTimeline, attrGroup: DeviceTypeAttributeGroup, 
                        devAttrs: DeviceAttributes, maxDataPoints: number): number {
        
        // Extract timestamp
        let { newBufIdx, timestamp } = this.extractTimestampAndAdvanceIdx(msgBuffer, msgBufIdx, deviceTimeline);
        if (newBufIdx < 0)
            return -1;
        msgBufIdx = newBufIdx;

        // console.log(`processMsgAttrGroup msg ${msgHexStr} timestamp ${timestamp} origTimestamp ${origTimestamp} msgBufIdx ${msgBufIdx}`)

        // Start of message data
        const msgDataStartIdx = msgBufIdx;

        // Number of bytes in group
        const attrGroupDataBytes = attrGroup.b;

        // Check if a custom attribute function is used
        let newAttrValues: number[][] = [];
        if ("c" in attrGroup) {

            // Extract attr values using custom handler
            newAttrValues = this._customAttrHandler.handleAttr(attrGroup, msgBuffer, msgBufIdx, attrGroupDataBytes);

        } else {  

            // Iterate over attributes
            for (let attrIdx = 0; attrIdx < attrGroup.a.length; attrIdx++) {

                // Get the attr definition
                const attr: DeviceTypeAttribute = attrGroup.a[attrIdx];
                if (!("t" in attr)) {
                    console.warn(`DeviceManager msg unknown msgBuffer ${msgBuffer} ts ${timestamp} attr ${JSON.stringify(attr)}`);
                    newAttrValues.push([]);
                    continue;
                }

                // Process the attribute
                const { values, newMsgBufIdx } = this.processMsgAttribute(attr, msgBuffer, msgBufIdx, msgDataStartIdx);
                if (newMsgBufIdx < 0) {
                    newAttrValues.push([]);
                    continue;                    
                }
                msgBufIdx = newMsgBufIdx;
                newAttrValues.push(values);
            }
        }

        // Check if any attributes were added
        if (newAttrValues.length === 0) {
            console.warn(`DeviceManager msg attrGroup ${attrGroup} newAttrValues ${newAttrValues} is empty`);
            return msgDataStartIdx+attrGroupDataBytes;
        }

        // All attributes must have the same number of new values
        let numNewValues = newAttrValues[0].length;
        for (let i = 1; i < newAttrValues.length; i++) {
            if (newAttrValues[i].length !== numNewValues) {
                console.warn(`DeviceManager msg attrGroup ${attrGroup} attr ${attrGroup.a[i].n} newAttrValues ${newAttrValues} do not have the same length`);
                return msgDataStartIdx+attrGroupDataBytes;
            }
        }

        // All attributes in the schema should have values
        if (newAttrValues.length !== attrGroup.a.length) {
            console.warn(`DeviceManager msg attrGroup ${attrGroup} newAttrValues ${newAttrValues} length does not match attrGroup.a length`);
            return msgDataStartIdx+attrGroupDataBytes;
        }

        // Add the new attribute values to the device state
        for (let attrIdx = 0; attrIdx < attrGroup.a.length; attrIdx++) {
            // Check if attribute already exists in the device state
            const attr: DeviceTypeAttribute = attrGroup.a[attrIdx];
            if (!(attr.n in devAttrs)) {
                devAttrs[attr.n] = {
                    name: attr.n,
                    newAttribute: true,
                    newData: true,
                    values: [],
                    units: decodeAttrUnitsEncoding(attr.u || ""),
                    range: attr.r || [0, 0],
                    format: ("f" in attr && typeof attr.f == "string") ? attr.f : "",
                    visibleSeries: "v" in attr ? attr.v === 0 || attr.v === false : ("vs" in attr ? (attr.vs === 0 || attr.vs === false ? false : !!attr.vs) : true),
                    visibleForm: "v" in attr ? attr.v === 0 || attr.v === false : ("vf" in attr ? (attr.vf === 0 || attr.vf === false ? false : !!attr.vf) : true),
                };
            }

            // Iterate through added values for this attribute
            const values = newAttrValues[attrIdx];
            for (let valueIdx = 0; valueIdx < values.length; valueIdx++) {
                // Limit to MAX_DATA_POINTS_TO_STORE
                if (devAttrs[attr.n].values.length >= maxDataPoints) {
                    devAttrs[attr.n].values.shift();
                }
                devAttrs[attr.n].values.push(values[valueIdx]);
                devAttrs[attr.n].newData = true;
            }
        }

        // Check if a time increment is specified
        const timeIncUs: number = attrGroup.us ? attrGroup.us : 1000;

        // Add to the timeline
        for (let i = 0; i < numNewValues; i++) {
            // Limit to MAX_DATA_POINTS_TO_STORE
            if (deviceTimeline.timestamps.length >= maxDataPoints) {
                deviceTimeline.timestamps.shift();
            }
            deviceTimeline.timestamps.push(timestamp);

            // Increment the timestamp
            timestamp += timeIncUs / 1000;
        }

        return msgDataStartIdx+attrGroupDataBytes;
    }

    private processMsgAttribute(attr: DeviceTypeAttribute, msgBuffer: Buffer, msgBufIdx: number, msgDataStartIdx: number): { values: number[], newMsgBufIdx: number} {

        // Current field message string index
        let curFieldBufIdx = msgBufIdx;
        let attrUsesAbsPos = false;

        // Check for "at": N which means start reading from byte N of the message (after the timestamp bytes)
        if (attr.at !== undefined) {
            curFieldBufIdx = msgDataStartIdx + attr.at;
            attrUsesAbsPos = true;
        }

        // Check if outside bounds of message
        if (curFieldBufIdx >= msgBuffer.length) {
            console.warn(`DeviceManager msg outside bounds msgBuffer ${msgBuffer} attr ${attr.n}`);
            return { values: [], newMsgBufIdx: -1 };
        }

        // Attribute type
        const attrTypesOnly = attr.t;

        // Slice into buffer
        const attrBuf = msgBuffer.slice(curFieldBufIdx);
 
        // Check if a mask is used and the value is signed
        const maskOnSignedValue = "m" in attr && isAttrTypeSigned(attrTypesOnly);

        // Extract the value using python-struct
        let unpackValues = struct.unpack(maskOnSignedValue ? attrTypesOnly.toUpperCase() : attrTypesOnly, attrBuf);
        let attrValues = unpackValues as number[];

        // Get number of bytes consumed
        const numBytesConsumed = struct.sizeOf(attrTypesOnly);

        // // Check if sign extendable mask specified on signed value
        // if (mmSpecifiedOnSignedValue) {
        //     const signBitMask = 1 << (signExtendableMaskSignPos - 1);
        //     const valueOnlyMask = signBitMask - 1;
        //     if (value & signBitMask) {
        //         value = (value & valueOnlyMask) - signBitMask;
        //     } else {
        //         value = value & valueOnlyMask;
        //     }
        // }

        // Check for mask
        if ("m" in attr) {
            const mask = typeof attr.m === "string" ? parseInt(attr.m, 16) : attr.m as number;
            attrValues = attrValues.map((value) => (maskOnSignedValue ? this.signExtend(value, mask) : value & mask));
        }

        // Check for a sign-bit
        if ("sb" in attr) {
            const signBitPos = attr.sb as number;
            const signBitMask = 1 << signBitPos;
            if ("ss" in attr) {
                const signBitSubtract = attr.ss as number;
                attrValues = attrValues.map((value) => (value & signBitMask) ? signBitSubtract - value : value);
            } else {
                attrValues = attrValues.map((value) => (value & signBitMask) ? value - (signBitMask << 1) : value);
            }
        }

        // Check for bit shift required
        if ("s" in attr && attr.s) {
            const bitshift = attr.s as number;
            if (bitshift > 0) {
                attrValues = attrValues.map((value) => (value) >> bitshift);
            } else if (bitshift < 0) {
                attrValues = attrValues.map((value) => (value) << -bitshift);
            }
        }

        // Check for divisor
        if ("d" in attr && attr.d) {
            const divisor = attr.d as number;
            attrValues = attrValues.map((value) => (value) / divisor);
        }

        // Check for value to add
        if ("a" in attr && attr.a !== undefined) {
            const addValue = attr.a as number;
            attrValues = attrValues.map((value) => (value) + addValue);
        }

        // console.log(`DeviceManager msg attrGroup ${attrGroup} devkey ${deviceKey} valueHexChars ${valueHexChars} msgHexStr ${msgHexStr} ts ${timestamp} attr ${attr.n} type ${attr.t} value ${value} signExtendableMaskSignPos ${signExtendableMaskSignPos} attrTypeDefForStruct ${attrTypeDefForStruct} attr ${attr}`);
        // Move buffer position if using relative positioning
        msgBufIdx += attrUsesAbsPos ? 0 : numBytesConsumed;

        // Return the value
        return { values: attrValues, newMsgBufIdx: msgBufIdx };
    }
    
    private signExtend(value: number, mask: number): number {
        const signBitMask = (mask + 1) >> 1;
        const signBit = value & signBitMask;
    
        if (signBit !== 0) {  // If sign bit is set
            const highBitsMask = ~mask & ~((mask + 1) >> 1);
            value |= highBitsMask;  // Apply the sign extension
        }
    
        return value;
    }

    private extractTimestampAndAdvanceIdx(msgBuffer: Buffer, msgBufIdx: number, timestampWrapHandler: DeviceTimeline): 
                    { newBufIdx: number, timestamp: number } {

        // Check there are enough characters for the timestamp
        if (msgBufIdx + this.MSG_TIMESTAMP_SIZE_BYTES > msgBuffer.length) {
            return { newBufIdx: -1, timestamp: 0 };
        }

        // Use struct to extract the timestamp
        const tsBuffer = msgBuffer.slice(msgBufIdx, msgBufIdx + this.MSG_TIMESTAMP_SIZE_BYTES);
        let timestamp = struct.unpack(">H", tsBuffer)[0] as number;

        // Check if time is before lastReportTimeMs - in which case a wrap around occurred to add on the max value
        if (timestamp < timestampWrapHandler.lastReportTimestampMs) {
            timestampWrapHandler.reportTimestampOffsetMs += this.MSG_TIMESTAMP_WRAP_VALUE;
        }
        timestampWrapHandler.lastReportTimestampMs = timestamp;

        // Offset timestamp
        const origTimestamp = timestamp;
        timestamp += timestampWrapHandler.reportTimestampOffsetMs;

        // Advance the index
        msgBufIdx += this.MSG_TIMESTAMP_SIZE_BYTES;

        // Return the timestamp
        return { newBufIdx: msgBufIdx, timestamp: timestamp };
    }


}