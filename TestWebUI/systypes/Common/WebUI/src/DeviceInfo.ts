
export const AttrTypeBytes: AttrTypeBytesType = {
    "uint8": 1,
    "uint16": 2,
    "uint32": 4,
    "int8": 1,
    "int16": 2,
    "int32": 4,
    "float": 4,
    "double": 8,
    "string": 0
};

export type AttrTypeBytesType = {
    [key: string]: number;
    uint8: number;
    uint16: number;
    uint32: number;
    int8: number;
    int16: number;
    int32: number;
    float: number;
    double: number;
    string: number;
};

type AttrTypeBytes = {
    [key: string]: number;
    uint8: number;
    uint16: number;
    uint32: number;
    int8: number;
    int16: number;
    int32: number;
    float: number;
    double: number;
    string: number;
};

export interface DeviceTypeAttribute {
    n: string;                  // Attribute name
    t: string;                  // Attribute type (e.g. uint16 - defines number of bytes used to store the attribute value)
    u: string;                  // Attribute unit
    r: number[];                // Attribute range (either min, max or min, max, step or discrete values)
    d: number;                  // Number of digits after the decimal point
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
