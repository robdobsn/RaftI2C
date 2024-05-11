import { DeviceTypeAttribute, DeviceTypeAttributeGroup } from "./DeviceInfo";

export default class CustomAttrHandler {
    
    public handleAttr(attrGroup: DeviceTypeAttributeGroup, msgBuffer: Buffer, msgBufIdx: number, attrGroupBytes: number): number[][] {
        if (attrGroup.c!.n === "max30101_fifo") {
            console.log(`CustomAttrHandler handleAttr ${attrGroup.c!.n} msgBuffer: ${msgBuffer}`);
            let d = msgBuffer.slice(msgBufIdx);
            if (d.length < attrGroupBytes) {
                return [];
            }
            let r: number[][] = [[],[]];
            let N = (d[0]+32-d[2])%32;
            let p = 3;
            for (let i = 0; i <= N-1; i++) {
                r[0].push((d[p]<<16)|(d[p+1]<<8|d[p+2]));
                r[1].push((d[p+3]<<16)|(d[p+4]<<8|d[p+5]));
                p+=6;
            }
            return r;
        }
        return [];
    }
}