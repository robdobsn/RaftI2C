
// export class DeviceMsg {
//     busName: string = "";
//     address: string = "";
//     rxTime: number = 0;
//     msg: Uint8Array = new Uint8Array(0);
// }

export interface DeviceMsgJsonElem {
    [attrGroupName: string]: string; // Attribute group name and value in hexadecimal
}

export interface DeviceMsgJsonBus {
    [devAddr: string]: DeviceMsgJsonElem;
}
  
export interface DeviceMsgJson {
    [busName: string]: DeviceMsgJsonBus;
}
