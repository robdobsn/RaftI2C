
export const AttrTypeBits: AttrTypeBitsType = {
    "c": 8,
    "b": 8,
    "B": 8,
    "?": 8,
    "h": 16,
    "H": 16,
    ">h": 16,
    "<h": 16,
    ">H": 16,
    "<H": 16,
    "i": 32,
    "I": 32,
    ">i": 32,
    "<i": 32,
    ">I": 32,
    "<I": 32,
    "l": 32,
    "L": 32,
    ">l": 32,
    "<l": 32,
    ">L": 32,
    "<L": 32,
    "q": 64,
    "Q": 64,
    ">q": 64,
    "<q": 64,
    ">Q": 64,
    "<Q": 64,
    "f": 32,
    ">f": 32,
    "<f": 32,
    "d": 64,
    ">d": 64,
    "<d": 64,
};

export type AttrTypeBitsType = {
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
    "t20": number;
};

export function isAttrTypeSigned(attrType: string): boolean {
    const attrStr = attrType.charAt(0) === ">" || attrType.charAt(0) === "<" ? attrType.slice(1).charAt(0) : attrType.charAt(0);
    return attrStr === "b" || attrStr === "h" || attrStr === "i" || attrStr === "l" || attrStr === "q";
}

export function decodeAttrUnitsEncoding(attr: string): string {
    // Replace instances of HTML encoded chars like &deg; with the actual char
    return attr.replace(/&deg;/g, "°");
}

export interface DeviceTypeAttribute {
    n: string;                  // Attribute name
    t: string;                  // Attribute type (e.g. uint16 - defines number of bytes used to store the attribute value)
    u: string;                  // Attribute unit
    r: number[];                // Attribute range (either min, max or min, max, step or discrete values)
    m?: number | string;        // Bit mask to extract the attribute value from the message
    s?: number;                 // Shift value to shift the attribute value to the right (or left if negative)
    d?: number;                 // Divisor to convert the raw attribute value (after operations above) to the actual value
    a?: number;                 // Value to add after division
    f?: string;                 // Format string similar to C printf format string (e.g. %d, %x, %f, %04d, %08x, %08.2f etc.), %b = boolean (0 iff 0, else 1)
    disp?: boolean | number;    // Display attribute value in the UI
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
