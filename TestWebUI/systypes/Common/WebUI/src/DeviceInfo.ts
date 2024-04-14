
export const AttrTypeBytes: AttrTypeBytesType = {
    "c": 1,
    "b": 1,
    "B": 1,
    "?": 1,
    "h": 2,
    "H": 2,
    ">h": 2,
    "<h": 2,
    ">H": 2,
    "<H": 2,
    "i": 4,
    "I": 4,
    ">i": 4,
    "<i": 4,
    ">I": 4,
    "<I": 4,
    "l": 4,
    "L": 4,
    ">l": 4,
    "<l": 4,
    ">L": 4,
    "<L": 4,
    "q": 8,
    "Q": 8,
    ">q": 8,
    "<q": 8,
    ">Q": 8,
    "<Q": 8,
    "f": 4,
    ">f": 4,
    "<f": 4,
    "d": 8,
    ">d": 8,
    "<d": 8
};

export type AttrTypeBytesType = {
    [key: string]: number;
    c: number;
    b: number;
    B: number;
    "?": number;
    h: number;
    H: number;
    ">h": number;
    "<h": number;
    ">H": number;
    "<H": number;
    i: number;
    I: number;
    ">i": number;
    "<i": number;
    ">I": number;
    "<I": number;
    l: number;
    L: number;
    ">l": number;
    "<l": number;
    ">L": number;
    "<L": number;
    q: number;
    Q: number;
    ">q": number;
    "<q": number;
    ">Q": number;
    "<Q": number;
    f: number;
    ">f": number;
    "<f": number;
    d: number;
    ">d": number;
    "<d": number;
};

export interface DeviceTypeAttribute {
    n: string;                  // Attribute name
    t: string;                  // Attribute type (e.g. uint16 - defines number of bytes used to store the attribute value)
    u: string;                  // Attribute unit
    r: number[];                // Attribute range (either min, max or min, max, step or discrete values)
    d: number;                  // Divisor to convert the raw attribute value to the actual value
    m?: number;                 // Bit mask to extract the attribute value from the message
}

export interface DeviceTypeAttributeGroups {
    [groupName: string]: DeviceTypeAttribute[];
}

export interface DeviceTypeInfo {
    name: string;
    desc: string;
    manu: string;
    type: string;
    attr: DeviceTypeAttributeGroups;
}

export interface DeviceTypeInfoTestJsonRec {
    addresses?: string;
    devInfoJson: DeviceTypeInfo;
}

export interface DeviceTypeInfoTestJsonElem {
    [devType: string]: DeviceTypeInfoTestJsonRec;
}

export interface DeviceTypeInfoTestJsonFile {
    devTypes: DeviceTypeInfoTestJsonElem;
}
