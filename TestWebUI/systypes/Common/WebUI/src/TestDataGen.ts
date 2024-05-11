class TestDataGen {

    // Start generating test data and use the callback provided to handle the data JSON string
    public start(handleDeviceMsgJson: (msg: string) => void) {
        // Start timer for testing which sends randomized data
        let psVals = [50, 150];
        let iterCount = 0;
        let psValAltCount = 0;
        let alsVal = 1000;
        let alsValInc = 1;
        let whiteVal = 1000;
        let whiteValInc = 10;
        let x = 0;
        let y = 0;
        let z = 0;
        setInterval(() => {
            // Performance testing
            const debugPerfTimerStart = performance.now();

            // Vars for VSNL4040
            const psVar = psVals[psValAltCount] + Math.floor(Math.random() * 10) - 5;
            const alsVar = alsVal;
            const whiteVar = whiteVal;
            iterCount++;
            if (iterCount % 10 === 0) {
                psValAltCount = (psValAltCount + 1) % 2;
            }
            alsVal += alsValInc + Math.floor(Math.random() * 10) - 5;
            if (iterCount % 1000 === 0) {
                alsValInc = -alsValInc;
            }
            whiteVal += whiteValInc + Math.floor(Math.random() * 10) - 5;
            if (iterCount % 100 === 0) {
                whiteValInc = -whiteValInc;
            }
            const tsHexHighLow = ((Date.now()) & 0xffff).toString(16).padStart(4, '0');
            const tsHexNextHighLow = ((Date.now()+1) & 0xffff).toString(16).padStart(4, '0');
            const psHexLowHigh = ((psVar & 0xff) << 8 | (psVar >> 8)).toString(16).padStart(4, '0');
            const alsHexLowHigh = ((alsVar & 0xff) << 8 | (alsVar >> 8)).toString(16).padStart(4, '0');
            const whiteHexLowHigh = ((whiteVar & 0xff) << 8 | (whiteVar >> 8)).toString(16).padStart(4, '0');

            // Vars for ADXL313
            x += Math.floor(Math.random() * 10) - 5;
            y += Math.floor(Math.random() * 10) - 5;
            z += Math.floor(Math.random() * 10) - 5;
            // Ensure x,y,z in range -512 to 511
            x = Math.floor(Math.min(511, Math.max(-512, x)));
            y = Math.floor(Math.min(511, Math.max(-512, y)));
            z = Math.floor(Math.min(511, Math.max(-512, z)));
            // Convert x,y,z to 10-bit signed values
            x = x & 0x3ff;
            y = y & 0x3ff;
            z = z & 0x3ff;
            const xHexLowHigh = ((x & 0xff) << 8 | (x >> 8)).toString(16).padStart(4, '0');
            const yHexLowHigh = ((y & 0xff) << 8 | (y >> 8)).toString(16).padStart(4, '0');
            const zHexLowHigh = ((z & 0xff) << 8 | (z >> 8)).toString(16).padStart(4, '0');

            interface DevMsg {
                x: string;
                _t: string;
                _o: boolean;
            }
            
            interface DevMsgs {
                [address: string]: DevMsg;
            }

            // Dev messages
            let devMsgs: DevMsgs = {
                "0x60@1": {
                    x: `${tsHexHighLow}${psHexLowHigh}${alsHexLowHigh}${whiteHexLowHigh}`,
                    _t: "VCNL4040",
                    _o: this.onlineFrom(iterCount, 0, 100)
                },
                "0x38@1": {
                    x: `${tsHexHighLow}${xHexLowHigh}${yHexLowHigh}${zHexLowHigh}`,
                    _t: "ADXL313",
                    _o: this.onlineFrom(iterCount, 0, 150)
                },
                "0x6f@41": {
                    x: `${tsHexHighLow}${this.toHex(this.randInt(0,1)*4,1)}`,
                    _t: "QwiicButton",
                    _o: this.onlineFrom(iterCount, 0, 120)
                },
                "0x6f@42": {
                    x: `${tsHexHighLow}${this.toHex(this.randInt(0,1)*4,1)}`,
                    _t: "QwiicButton",
                    _o: this.onlineFrom(iterCount, 0, 90)
                },
                "0x23@0": {
                    x: "",
                    _t: "QwiicLEDStick",
                    _o: this.onlineFrom(iterCount, 100, 180)
                },
                "0x28@0": {
                    x: `${tsHexHighLow}${this.toHex(this.randInt(0,7),2)}${tsHexNextHighLow}${this.toHex(this.randInt(0,7),2)}`,
                    _t: "CAP1203",
                    _o: true
                },
                "0x57@0": {
                    x: `${tsHexHighLow}${this.toHex(this.randInt(0,7),2)}${this.toHex(0x05,2)}${this.toHex(0x00,2)}${this.toHex(0x08,2)}${this.toHex(0x00,100)}`,
                    _t: "MAX30101",
                    _o: true
                }
            };

            interface DevMsgsEnabled {
                [key: string]: boolean;
            }

            const devMsgsEnabled:DevMsgsEnabled = { "0x6f@42":true };
            const I2CA: DevMsgs = Object.keys(devMsgsEnabled).length === 0 ? devMsgs : {};
            for (const key in devMsgs) {
                if (devMsgsEnabled[key]) {
                    I2CA[key] = devMsgs[key];
                }
            }
 
            const msg = JSON.stringify({I2CA: I2CA});

            // Performance testing
            const debugPerfTimerEnd = performance.now();

            // Call the callback to handle the data JSON string
            const debugHandleMsgStart = performance.now();
            handleDeviceMsgJson(msg);
            const debugHandleMsgEnd = performance.now();

            // console.log(`iterCount ${iterCount} genTestDataTime ${debugPerfTimerEnd - debugPerfTimerStart} handleTestDataTime ${debugHandleMsgEnd - debugHandleMsgStart}`);

            // console.log(`iterCount ${iterCount} x ${x} Test message sent: ${JSON.stringify(JSON.parse(msg))}`);
        }, 200);
    }

    private toHex(val: number, numBytes: number) {
        return val.toString(16).padStart(numBytes * 2, '0');
    }
    private randInt(min: number, max: number) {
        return Math.floor(Math.random() * (max - min + 1) + min);
    }
    private onlineFrom(iterCount: number, start: number, end: number) {
        const ic = iterCount % 200;
        return ic >= start && ic <= end;
    }
}

export default TestDataGen;