import CustomAttrHandler from "./CustomAttrHandler";
import { DeviceTypeAttribute, DeviceTypePollRespMetadata, decodeAttrUnitsEncoding, isAttrTypeSigned } from "./DeviceInfo";
import { DeviceAttributesState, DeviceTimeline } from "./DeviceStates";
import struct, { DataType } from 'python-struct';

export default class AttributeHandler {

    // Custom attribute handler
    private _customAttrHandler = new CustomAttrHandler();

    // Message timestamp size
    private POLL_RESULT_TIMESTAMP_SIZE = 2;
    private POLL_RESULT_WRAP_VALUE = this.POLL_RESULT_TIMESTAMP_SIZE === 2 ? 65536 : 4294967296;
    private POLL_RESULT_RESOLUTION_US = 1000;
    
    public processMsgAttrGroup(msgBuffer: Buffer, msgBufIdx: number, deviceTimeline: DeviceTimeline, pollRespMetadata: DeviceTypePollRespMetadata, 
                        devAttrsState: DeviceAttributesState, maxDataPoints: number): number {
        
        // console.log(`processMsgAttrGroup msg ${msgHexStr} timestamp ${timestamp} origTimestamp ${origTimestamp} msgBufIdx ${msgBufIdx}`)

        // Extract msg timestamp
        let { newBufIdx, timestampUs } = this.extractTimestampAndAdvanceIdx(msgBuffer, msgBufIdx, deviceTimeline);
        if (newBufIdx < 0)
            return -1;
        msgBufIdx = newBufIdx;
        
        // Start of message data
        const msgDataStartIdx = msgBufIdx;

        // New attribute values (in order as they appear in the attributes JSON)
        let newAttrValues: number[][] = [];
        if ("c" in pollRespMetadata) {

            // Extract attribute values using custom handler
            newAttrValues = this._customAttrHandler.handleAttr(pollRespMetadata, msgBuffer, msgBufIdx);

        } else {

            // Iterate over attributes
            for (let attrIdx = 0; attrIdx < pollRespMetadata.a.length; attrIdx++) {

                // Get the attribute definition
                const attrDef: DeviceTypeAttribute = pollRespMetadata.a[attrIdx];
                if (!("t" in attrDef)) {
                    console.warn(`DeviceManager msg unknown msgBuffer ${msgBuffer} tsUs ${timestampUs} attrDef ${JSON.stringify(attrDef)}`);
                    newAttrValues.push([]);
                    continue;
                }

                // Process the attribute
                const { values, newMsgBufIdx } = this.processMsgAttribute(attrDef, msgBuffer, msgBufIdx, msgDataStartIdx);
                if (newMsgBufIdx < 0) {
                    newAttrValues.push([]);
                    continue;
                }
                msgBufIdx = newMsgBufIdx;
                newAttrValues.push(values);
            }

        }
        
        // Number of bytes in group
        const pollRespSizeBytes = pollRespMetadata.b;

        // Check if any attributes were added (in addition to timestamp)
        if (newAttrValues.length === 0) {
            console.warn(`DeviceManager msg attrGroup ${JSON.stringify(pollRespMetadata)} newAttrValues ${newAttrValues} is empty`);
            return msgDataStartIdx+pollRespSizeBytes;
        }

        // All attributes must have the same number of new values
        let numNewDataPoints = newAttrValues[0].length;
        for (let i = 1; i < newAttrValues.length; i++) {
            if (newAttrValues[i].length !== numNewDataPoints) {
                console.warn(`DeviceManager msg attrGroup ${pollRespMetadata} attrName ${pollRespMetadata.a[i].n} newAttrValues ${newAttrValues} do not have the same length`);
                return msgDataStartIdx+pollRespSizeBytes;
            }
        }

        // All attributes in the schema should have values
        if (newAttrValues.length !== pollRespMetadata.a.length) {
            console.warn(`DeviceManager msg attrGroup ${pollRespMetadata} newAttrValues ${newAttrValues} length does not match attrGroup.a length`);
            return msgDataStartIdx+pollRespSizeBytes;
        }

        // Add the new attribute values to the device state
        for (let attrIdx = 0; attrIdx < pollRespMetadata.a.length; attrIdx++) {
            // Check if attribute already exists in the device state
            const attrDef: DeviceTypeAttribute = pollRespMetadata.a[attrIdx];
            if (!(attrDef.n in devAttrsState)) {
                devAttrsState[attrDef.n] = {
                    name: attrDef.n,
                    newAttribute: true,
                    newData: true,
                    values: [],
                    units: decodeAttrUnitsEncoding(attrDef.u || ""),
                    range: attrDef.r || [0, 0],
                    format: ("f" in attrDef && typeof attrDef.f == "string") ? attrDef.f : "",
                    visibleSeries: "v" in attrDef ? attrDef.v === 0 || attrDef.v === false : ("vs" in attrDef ? (attrDef.vs === 0 || attrDef.vs === false ? false : !!attrDef.vs) : true),
                    visibleForm: "v" in attrDef ? attrDef.v === 0 || attrDef.v === false : ("vf" in attrDef ? (attrDef.vf === 0 || attrDef.vf === false ? false : !!attrDef.vf) : true),
                };
            }

            // Check if any data points need to be discarded
            const discardCount = Math.max(0, devAttrsState[attrDef.n].values.length + newAttrValues[attrIdx].length - maxDataPoints);
            if (discardCount > 0) {
                devAttrsState[attrDef.n].values.splice(0, discardCount);
            }

            // Add the new values
            devAttrsState[attrDef.n].values.push(...newAttrValues[attrIdx]);
            devAttrsState[attrDef.n].newData = newAttrValues[attrIdx].length > 0;
        }

        // Handle the timestamps with increments if specified
        const timeIncUs: number = pollRespMetadata.us ? pollRespMetadata.us : 1000;
        let timestampsUs = Array(numNewDataPoints).fill(0);
        for (let i = 1; i < numNewDataPoints; i++) {
            timestampsUs[i] =  timestampUs + i * timeIncUs;
        }
        
        // Check if timeline points need to be discarded
        const discardCount = Math.max(0, deviceTimeline.timestampsUs.length + timestampsUs.length - maxDataPoints);
        if (discardCount > 0) {
            deviceTimeline.timestampsUs.splice(0, discardCount);
        }

        // Add the new timestamps
        deviceTimeline.timestampsUs.push(...timestampsUs);

        // Return the next message buffer index
        return msgDataStartIdx+pollRespSizeBytes;
    }

    private processMsgAttribute(attrDef: DeviceTypeAttribute, msgBuffer: Buffer, msgBufIdx: number, msgDataStartIdx: number): { values: number[], newMsgBufIdx: number} {

        // Current field message string index
        let curFieldBufIdx = msgBufIdx;
        let attrUsesAbsPos = false;

        // Check for "at": N which means start reading from byte N of the message (after the timestamp bytes)
        if (attrDef.at !== undefined) {
            curFieldBufIdx = msgDataStartIdx + attrDef.at;
            attrUsesAbsPos = true;
        }

        // Check if outside bounds of message
        if (curFieldBufIdx >= msgBuffer.length) {
            console.warn(`DeviceManager msg outside bounds msgBuffer ${msgBuffer} attrName ${attrDef.n}`);
            return { values: [], newMsgBufIdx: -1 };
        }

        // Attribute type
        const attrTypesOnly = attrDef.t;

        // Slice into buffer
        const attrBuf = msgBuffer.slice(curFieldBufIdx);
 
        // Check if a mask is used and the value is signed
        const maskOnSignedValue = "m" in attrDef && isAttrTypeSigned(attrTypesOnly);

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

        // Check for XOR mask
        if ("x" in attrDef) {
            const mask = typeof attrDef.x === "string" ? parseInt(attrDef.x, 16) : attrDef.x as number;
            attrValues = attrValues.map((value) => value ^ mask);
        }
        
        // Check for AND mask
        if ("m" in attrDef) {
            const mask = typeof attrDef.m === "string" ? parseInt(attrDef.m, 16) : attrDef.m as number;
            attrValues = attrValues.map((value) => (maskOnSignedValue ? this.signExtend(value, mask) : value & mask));
        }

        // Check for a sign-bit
        if ("sb" in attrDef) {
            const signBitPos = attrDef.sb as number;
            const signBitMask = 1 << signBitPos;
            if ("ss" in attrDef) {
                const signBitSubtract = attrDef.ss as number;
                attrValues = attrValues.map((value) => (value & signBitMask) ? signBitSubtract - value : value);
            } else {
                attrValues = attrValues.map((value) => (value & signBitMask) ? value - (signBitMask << 1) : value);
            }
        }

        // Check for bit shift required
        if ("s" in attrDef && attrDef.s) {
            const bitshift = attrDef.s as number;
            if (bitshift > 0) {
                attrValues = attrValues.map((value) => (value) >> bitshift);
            } else if (bitshift < 0) {
                attrValues = attrValues.map((value) => (value) << -bitshift);
            }
        }

        // Check for divisor
        if ("d" in attrDef && attrDef.d) {
            const divisor = attrDef.d as number;
            attrValues = attrValues.map((value) => (value) / divisor);
        }

        // Check for value to add
        if ("a" in attrDef && attrDef.a !== undefined) {
            const addValue = attrDef.a as number;
            attrValues = attrValues.map((value) => (value) + addValue);
        }

        // console.log(`DeviceManager msg attrGroup ${attrGroup} devkey ${deviceKey} valueHexChars ${valueHexChars} msgHexStr ${msgHexStr} ts ${timestamp} attrName ${attrDef.n} type ${attrDef.t} value ${value} signExtendableMaskSignPos ${signExtendableMaskSignPos} attrTypeDefForStruct ${attrTypeDefForStruct} attrDef ${attrDef}`);
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
                    { newBufIdx: number, timestampUs: number } {

        // Check there are enough characters for the timestamp
        if (msgBufIdx + this.POLL_RESULT_TIMESTAMP_SIZE > msgBuffer.length) {
            return { newBufIdx: -1, timestampUs: 0 };
        }

        // Use struct to extract the timestamp
        const tsBuffer = msgBuffer.slice(msgBufIdx, msgBufIdx + this.POLL_RESULT_TIMESTAMP_SIZE);
        let timestampUs: number;
        if (this.POLL_RESULT_TIMESTAMP_SIZE === 2) { 
            timestampUs = struct.unpack(">H", tsBuffer)[0] as number * this.POLL_RESULT_RESOLUTION_US;
        } else {
            timestampUs = struct.unpack(">I", tsBuffer)[0] as number * this.POLL_RESULT_RESOLUTION_US;
        }

        // Check if time is before lastReportTimeMs - in which case a wrap around occurred to add on the max value
        if (timestampUs < timestampWrapHandler.lastReportTimestampUs) {
            timestampWrapHandler.reportTimestampOffsetUs += this.POLL_RESULT_WRAP_VALUE * this.POLL_RESULT_RESOLUTION_US;
        }
        timestampWrapHandler.lastReportTimestampUs = timestampUs;

        // Offset timestamp
        timestampUs += timestampWrapHandler.reportTimestampOffsetUs;

        // Advance the index
        msgBufIdx += this.POLL_RESULT_TIMESTAMP_SIZE;

        // Return the timestamp
        return { newBufIdx: msgBufIdx, timestampUs: timestampUs };
    }


}