import jest from 'jest';
import AttributeHandler from "../src/AttributeHandler"
import { DeviceTypeInfoTestJsonFile, DeviceTypePollRespMetadata } from '../src/DeviceInfo';
import { DeviceAttributesState, DeviceTimeline } from '../src/DeviceStates';
import { randomInt } from 'crypto';

function genBuffer(...args: Buffer[]): Buffer {
    return Buffer.concat(args);
}

function genTs(ts: number): Buffer {
    let buffer = Buffer.alloc(2);
    buffer.writeUint16BE(ts);
    return buffer;
}

function genUInt16BE(val: number): Buffer {
    let buffer = Buffer.alloc(2);
    buffer.writeUInt16BE(val);
    return buffer;
}

function genUInt16LE(val: number): Buffer {
    let buffer = Buffer.alloc(2);
    buffer.writeUInt16LE(val);
    return buffer;
}

function genInt16LE(val: number): Buffer {
    let buffer = Buffer.alloc(2);
    buffer.writeInt16LE(val);
    return buffer;
}

function genUInt24BE(val: number): Buffer {
    let buffer = Buffer.alloc(4);
    buffer.writeUInt32BE(val);
    return buffer.slice(1);
}

function genUInt24LE(val: number): Buffer {
    let buffer = Buffer.alloc(4);
    buffer.writeUInt32LE(val);
    return buffer.slice(0, 3);
}

function genUInt2x20BE(val1: number, val2: number): Buffer {
    let buffer = Buffer.alloc(5);
    buffer.writeUInt32BE((val1<<12) + (val2>>8));
    buffer.writeUInt8(val2 & 0xff, 4);
    return buffer;
}

function genB(val: number): Buffer {
    let buffer = Buffer.alloc(1);
    buffer.writeUInt8(val);
    return buffer;
}

interface DevAttr {
    v: number[];
}

interface DevAttrs {
    [key: string]: DevAttr;
}

interface TestCase {
    devType: string;
    buffer: Buffer;
    timestamps: number[];
    attrs: DevAttrs;
    nextIdx: number;
}

const testCases: TestCase [] = [
    {
        devType: "VCNL4040",  
        buffer: genBuffer(genTs(1234), genUInt16LE(5678), genUInt16LE(9013), genUInt16LE(19726)),
        timestamps: [1234000],
        attrs: {
            "prox": {       v: [5678]           },
            "als": {        v: [901.3]          },
            "white": {      v: [1972.6]         }
        },
        nextIdx: 8
    },
    {
        devType: "VL6180",
        buffer: genBuffer(genTs(38171), genB(0x56), genB(175)),
        timestamps: [38171000],
        attrs: {
            "valid": {      v: [1]              },
            "dist":  {      v: [175]            }
        },
        nextIdx: 4
    },
    {
        devType: "MAX30101",
        buffer: genBuffer(genTs(1234), 
                    genB(0x08), genB(0x00), genB(0x00), 
                    genUInt24BE(123456), genUInt24BE(123456), 
                    genUInt24BE(123456), genUInt24BE(123456), 
                    genUInt24BE(123456), genUInt24BE(123456),
                    genUInt24BE(123456), genUInt24BE(123456), 
                    genUInt24BE(123456), genUInt24BE(123456), 
                    genUInt24BE(123456), genUInt24BE(123456), 
                    genUInt24BE(123456), genUInt24BE(123456),
                    genUInt24BE(123456), genUInt24BE(123456)),
        timestamps: [1234000, 1234040, 1234080, 1234120, 1234160, 1234200, 1234240, 1234280],
        attrs: {
            "Red": {        v: [123456,123456,123456,123456,123456,123456,123456,123456]              },
            "IR": {         v: [123456,123456,123456,123456,123456,123456,123456,123456]              }
        },
        nextIdx: 53
    },
    {
        devType: "ADXL313",
        buffer: genBuffer(genTs(1234), genInt16LE(Math.floor(3.5*1024)), genInt16LE(Math.floor(-1.6*1024)), genInt16LE(Math.floor(1.8*1024))),
        timestamps: [1234000],
        attrs: {
            "x": {          v: [Math.floor(3.5*1024)/1024]          },
            "y": {          v: [Math.floor(-1.6*1024)/1024]         },
            "z": {          v: [Math.floor(1.8*1024)/1024]          }
        },
        nextIdx: 8
    },
    {
        devType: "AHT20",
        buffer: genBuffer(genTs(1234), genB(0x56), genUInt2x20BE(98274, 87295)),
        timestamps: [1234000],
        attrs: {
            "status": {       v: [0x56]           },
            "humidity": {        v: [100*98274/(2**20)]          },
            "temperature": {      v: [200*87295/(2**20)-50]         }
        },
        nextIdx: 8
    },
    {
        devType: "MCP9808",
        buffer: genBuffer(genTs(9824), genUInt16BE(0xf603)),
        timestamps: [9824000],
        attrs: {
            "temperature": {       v: [(0xf603 & 0x1000) != 0 ? 256-(0xf603 & 0x1fff)/16 : (0xf603 & 0x0fff)/16]           }
        },
        nextIdx: 4
    },
    {
        devType: "LPS25",
        buffer: genBuffer(genTs(9824), genB(0x12), genUInt24LE(123*4096), genUInt16LE((100.4-42.5)*480)),
        timestamps: [9824000],
        attrs: {
            "status":   {        v: [0x12]           },
            "pressure": {        v: [123]          },
            "temperature": {     v: [100.4]          }
        },
        nextIdx: 8
    }
];

testCases.forEach(testCase => {

    let deviceTypeRecs: DeviceTypeInfoTestJsonFile;
    let attributeHandler: AttributeHandler;
    let buffer: Buffer;
    let deviceTimeline: DeviceTimeline = { timestampsUs: [], lastReportTimestampUs: 0, reportTimestampOffsetUs: 0 };
    let pollRespMetadata: DeviceTypePollRespMetadata;


    describe(`Testing ${testCase.devType}`, () => {
        
        beforeAll(async () => {
            const jsonData = await import('../../../../../DeviceTypeRecords/DeviceTypeRecords.json');
            deviceTypeRecs = jsonData.default as DeviceTypeInfoTestJsonFile;
        });

        beforeEach(() => {
            attributeHandler = new AttributeHandler();
            buffer = testCase.buffer;
            pollRespMetadata = deviceTypeRecs?.devTypes[testCase.devType].devInfoJson.resp!;
        });

        test('poll response extraction', () => {
            // Assuming you know the structure and that attrDefs is properly defined and accessible here
            let attributeValues: DeviceAttributesState = {};
            let newMsgBufIdx = attributeHandler.processMsgAttrGroup(buffer, 0, deviceTimeline, pollRespMetadata!, attributeValues, 100);

            console.log(`${testCase.devType} testCase.nextIdx: ${testCase.nextIdx} newMsgBufIdx: ${newMsgBufIdx}`);
            console.log(`deviceAttributes: ${JSON.stringify(attributeValues)}`);
            console.log(`testCase.attrs: ${JSON.stringify(testCase.attrs)}`);

            // Check test case values
            Object.keys(testCase.attrs).forEach(attrName => {
                expect(attributeValues[attrName]).toBeDefined();
                expect(attributeValues[attrName].values).toEqual(testCase.attrs[attrName].v);
            });

            // Check timeline values
            expect(deviceTimeline.timestampsUs).toEqual(testCase.timestamps);

            // Check next index
            expect(newMsgBufIdx).toEqual(testCase.nextIdx);
        });
    });
});
