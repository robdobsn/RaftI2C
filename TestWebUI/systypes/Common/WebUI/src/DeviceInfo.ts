
const attrTypeBits: { [key: string]: number } = {
    "c": 8, "b": 8, "B": 8, "?": 8,
    "h": 16, "H": 16, ">h": 16, "<h": 16, ">H": 16, "<H": 16,
    "i": 32, "I": 32, ">i": 32, "<i": 32, ">I": 32, "<I": 32, "l": 32, "L": 32, ">l": 32, "<l": 32, ">L": 32, "<L": 32,
    "q": 64, "Q": 64, ">q": 64, "<q": 64, ">Q": 64, "<Q": 64,
    "f": 32, ">f": 32, "<f": 32,
    "d": 64, ">d": 64, "<d": 64,
    };

export function getAttrTypeBits(attrType: string): number {
    if (attrType in attrTypeBits) {
        return attrTypeBits[attrType];
    }
    return 8;
}

export function isAttrTypeSigned(attrType: string): boolean {
    const attrStr = attrType.charAt(0) === ">" || attrType.charAt(0) === "<" ? attrType.slice(1).charAt(0) : attrType.charAt(0);
    return attrStr === "b" || attrStr === "h" || attrStr === "i" || attrStr === "l" || attrStr === "q";
}

export function decodeAttrUnitsEncoding(unitsEncoding: string): string {
    // Replace instances of HTML encoded chars like &deg; with the actual char
    return unitsEncoding.replace(/&deg;/g, "°");
}

export interface DeviceTypeAttribute {
    n: string;                      // Name
    t: string;                      // Type in python struct module format (e.g. 'H' uint16, 'h' int16, 'f' float etc.)
    at?: number;                    // Start pos in buffer (after timestamp) if present (otherwise use relative position)
    u?: string;                     // Units (e.g. mm)
    r?: number[];                   // Range (either min, max or min, max, step or discrete values)
    m?: number | string;            // Bit mask to extract the attribute value from the message
    s?: number;                     // Shift value to shift the attribute value to the right (or left if negative)
    sb?: number;                    // Sign-bit position (0-based)
    ss?: number;                    // Sign-bit subtraction value
    d?: number;                     // Divisor to convert the raw attribute value (after operations above) to the actual value
    a?: number;                     // Value to add after division
    f?: string;                     // Format string similar to C printf format string (e.g. %d, %x, %f, %04d, %08x, %08.2f etc.), %b = boolean (0 iff 0, else 1)
    o?: string;                     // Type of output value (e.g. 'bool', 'uint8', 'float')
    v?: boolean | number;           // Visibility of the attribute in all locations (mainly used to hide attributes that are not useful to the user)
    vs?: boolean | number;          // Display attribute value in time-series graphs
    vf?: boolean | number;          // Display attribute value in the device info panel
}

export interface CustomFunctionDefinition {
    n: string;                      // Function name
    c: string;                      // Function pseudo-code
}

export interface DeviceTypePollRespMetadata {
    b: number;                      // Size of polled response data block in bytes (excluding timestamp)
    a: DeviceTypeAttribute[];       // Attributes in the polled response
    c?: CustomFunctionDefinition;   // Custom function definition
    us?: number;                    // Time between consecutive samples in microseconds 
}

export interface DeviceTypeAction {
    n: string;                      // Action name
    t?: string;                     // Action type using python struct module format (e.g. 'H' for unsigned short, 'h' for signed short, 'f' for float etc.)
    w: string;                      // Prefix to write to cmd API
    r?: number[];                   // Range of valid values for the action
    f?: string;                     // Custom formatting options (e.g. LEDPIX for LED pixel grid)
    NX?: number;                    // Number of X in the LED pixel grid
    NY?: number;                    // Number of Y in the LED pixel grid
    concat?: boolean;               // Concatenate the all values into a single command
    d?: number;                     // Default value
}

export interface DeviceTypeInfo {
    name: string;
    desc: string;
    manu: string;
    type: string;
    resp?: DeviceTypePollRespMetadata;
    actions?: DeviceTypeAction[];
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

export interface DeviceTypeInfoRecs {
    [devType: string]: DeviceTypeInfo;
}
