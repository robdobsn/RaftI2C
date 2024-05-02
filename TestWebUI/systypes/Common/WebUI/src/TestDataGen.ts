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
            
            // Buttons
            const but1Val = Math.floor(Math.random() * 2).toString(16).padStart(2, '0');
            const but2Val = Math.floor(Math.random() * 2).toString(16).padStart(2, '0');
            
            // Online / offline status
            const online1Value = Math.floor(iterCount / 100) % 2 === 0;
            const online2Value = Math.floor(iterCount / 150) % 2 === 1;
            const online3Value = Math.floor(iterCount / 200) % 2 === 0;
            const online4Value = Math.floor(iterCount / 300) % 2 === 1;
            const dev2MsgPresent = iterCount % 500 > 250;
            const dev3MsgPresent = iterCount % 200 < 150;
            const dev4MsgPresent = iterCount % 200 > 100;

            // Templates
            const dev1Msg = `
                    "0x60@1": {
                        "x": "${tsHexHighLow}${psHexLowHigh}${alsHexLowHigh}${whiteHexLowHigh}",
                        "_t": "VCNL4040"
                        ${online1Value ? ', "_o": 1' : ', "_o": 0'}
                    }
                `;
                const dev2Msg = `
                "0x60@2": {
                    "x": "${tsHexHighLow}${psHexLowHigh}${alsHexLowHigh}${whiteHexLowHigh}",
                    "_t": "VCNL4040"
                    ${online1Value ? ', "_o": 1' : ', "_o": 0'}
                }
            `;
        // const dev2Msg = `
            //         "0x38@1": {
            //             "x": "${tsHexHighLow}${xHexLowHigh}${yHexLowHigh}${zHexLowHigh}",
            //             "_t": "ADXL313"
            //             ${online2Value ? ', "_o": 1' : ', "_o": 0'}
            //         }
            //     `;
            const dev3Msg = `
                "0x6f@41": {
                    "x": "${tsHexHighLow}${but1Val}",
                    "_t": "QWIICBUTTON"
                    ${online3Value ? ', "_o": 1' : ', "_o": 0'}
                }
            `;
            const dev4Msg = `
                "0x6f@42": {
                    "x": "${tsHexHighLow}${but2Val}",
                    "_t": "QWIICBUTTON"
                    ${online4Value ? ', "_o": 1' : ', "_o": 0'}
                }
            `;
            const msg = `{
                "I2CA":
                    {
                        ${dev1Msg}
                        ${dev2MsgPresent ? ',' + dev2Msg : ''}
                        ${dev3MsgPresent ? ',' + dev3Msg : ''}
                        ${dev4MsgPresent ? ',' + dev4Msg : ''}
                    }
                }`;

            // Performance testing
            const debugPerfTimerEnd = performance.now();

            // Call the callback to handle the data JSON string
            const debugHandleMsgStart = performance.now();
            handleDeviceMsgJson(msg);
            const debugHandleMsgEnd = performance.now();

            console.log(`iterCount ${iterCount} genTestDataTime ${debugPerfTimerEnd - debugPerfTimerStart} handleTestDataTime ${debugHandleMsgEnd - debugHandleMsgStart}`);

            // console.log(`iterCount ${iterCount} x ${x} Test message sent: ${JSON.stringify(JSON.parse(msg))}`);
        }, 200);
    }
}

export default TestDataGen;