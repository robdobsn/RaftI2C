import { DeviceTypeAttribute, DeviceTypePollRespMetadata } from "./DeviceInfo";

export default class CustomAttrHandler {
    
    public handleAttr(pollRespMetadata: DeviceTypePollRespMetadata, msgBuffer: Buffer, msgBufIdx: number): number[][] {

        // Number of bytes in the each message
        const numMsgBytes = pollRespMetadata.b;

        // Create a vector for each attribute in the metadata
        let attrValueVecs: number[][] = [];

        // Reference to each vector by attribute name
        let attrValues: { [key: string]: number[] } = {};

        // Add attributes to the vector
        for (let attrIdx = 0; attrIdx < pollRespMetadata.a.length; attrIdx++) {
            attrValueVecs.push([]);
            attrValues[pollRespMetadata.a[attrIdx].n] = attrValueVecs[attrIdx];
        }

        // Custom code for each device type
        if (pollRespMetadata.c!.n === "max30101_fifo") {
            // Hex dump msgBuffer
            // console.log(`CustomAttrHandler handleAttr ${pollRespMetadata.c!.n} msgBuffer: ${msgBuffer.toString('hex')}`); 
            let buf = msgBuffer.slice(msgBufIdx);
            if (buf.length < numMsgBytes) {
                return [];
            }

            // Generated code ...
            let N=(buf[0]+32-buf[2])%32;
            let k=3;
            let i=0;
            while (i<N) {
                attrValues['Red'].push(0); attrValues['Red'][attrValues['Red'].length-1] =(buf[k]<<16)|(buf[k+1]<<8)|buf[k+2];
                attrValues['IR'].push(0); attrValues['IR'][attrValues['IR'].length-1] =(buf[k+3]<<16)|(buf[k+4]<<8)|buf[k+5];
                k+=6;
                i++;
                ;
            }            
        } else if (pollRespMetadata.c!.n === "lsm6ds_fifo") {
            const buf = msgBuffer.slice(msgBufIdx);
            if (buf.length < 4) {
                return attrValueVecs;
            }

            const fifoWords = ((buf[1] & 0x0f) << 8) | buf[0];
            const fifoPattern = ((buf[3] & 0x03) << 8) | buf[2];
            const skipWords = (6 - (fifoPattern % 6)) % 6;
            const alignedWords = fifoWords - skipWords;
            const availableSampleCount = Math.floor(Math.max(0, buf.length - 4 - skipWords * 2) / 12);
            let sampleCount = Math.floor(alignedWords / 6);
            sampleCount = Math.min(sampleCount, availableSampleCount);
            sampleCount = Math.max(0, sampleCount);

            let bufIdx = 4 + skipWords * 2;
            for (let sampleIdx = 0; sampleIdx < sampleCount; sampleIdx++) {
                attrValues['gx'].push(this.decodeLsm6dsValue(buf, bufIdx, this.getAttrDef(pollRespMetadata, 'gx')));
                attrValues['gy'].push(this.decodeLsm6dsValue(buf, bufIdx + 2, this.getAttrDef(pollRespMetadata, 'gy')));
                attrValues['gz'].push(this.decodeLsm6dsValue(buf, bufIdx + 4, this.getAttrDef(pollRespMetadata, 'gz')));
                attrValues['ax'].push(this.decodeLsm6dsValue(buf, bufIdx + 6, this.getAttrDef(pollRespMetadata, 'ax')));
                attrValues['ay'].push(this.decodeLsm6dsValue(buf, bufIdx + 8, this.getAttrDef(pollRespMetadata, 'ay')));
                attrValues['az'].push(this.decodeLsm6dsValue(buf, bufIdx + 10, this.getAttrDef(pollRespMetadata, 'az')));
                bufIdx += 12;
            }
        }
        return attrValueVecs;
    }

    private getAttrDef(pollRespMetadata: DeviceTypePollRespMetadata, attrName: string): DeviceTypeAttribute | undefined {
        return pollRespMetadata.a.find((attrDef) => attrDef.n === attrName);
    }

    private decodeLsm6dsValue(buf: Buffer, bufIdx: number, attrDef?: DeviceTypeAttribute): number {
        let value = buf[bufIdx] | (buf[bufIdx + 1] << 8);
        if (value & 0x8000) {
            value -= 0x10000;
        }
        if (attrDef?.d) {
            value /= attrDef.d;
        }
        if (attrDef?.a) {
            value += attrDef.a;
        }
        return value;
    }
}
