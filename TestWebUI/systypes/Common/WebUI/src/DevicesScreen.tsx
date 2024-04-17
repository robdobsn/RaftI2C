// Component which uses the DeviceList component to display the list of devices

import React, { useEffect, useState } from 'react';
import { DeviceAttribute, DevicesState, DeviceState } from './DeviceStates';
import { DeviceManager } from './DeviceManager';
import DeviceScreen from './DeviceScreen';
import { DevicesConfig } from './DevicesConfig';
import './styles.css';

const deviceManager = DeviceManager.getInstance();

export class DevicesScreenProps {
    constructor(
        public isEditingMode: boolean,
        public config: DevicesConfig
    ) { }
}

export default function DevicesScreen(props: DevicesScreenProps) {
    const [devicesState, setDevicesState] = useState<DevicesState>(new DevicesState());
    const [lastUpdated, setLastUpdated] = useState<number>(0);
    
    useEffect(() => {
        const onNewDevice = (deviceKey: string, newDeviceState: DeviceState) => {

            const debugPerfTimerStart = performance.now();

            setDevicesState((prevState) => {
                // Since prevState is an instance of DevicesState, we clone it and update it
                const newState = new DevicesState();
                Object.assign(newState, prevState);
                newState[deviceKey] = newDeviceState;
                return newState;
            });
            setLastUpdated(Date.now());

            const debugPerfTimerEnd = performance.now();
            console.log(`onNewDevice took ${debugPerfTimerEnd - debugPerfTimerStart} ms`);
        };

        deviceManager.onNewDevice(onNewDevice);

        const onNewAttribute = (deviceKey: string, attribute: DeviceAttribute) => {

            const debugPerfTimerStart = performance.now();

            setDevicesState((prevState) => {

                // TODO - refactor this as inefficient

                // Since prevState is an instance of DevicesState, we clone it and update it
                const newState = new DevicesState();
                Object.assign(newState, prevState);
                newState[deviceKey].deviceAttributes[attribute.name] = attribute;
                return newState;
            });
            setLastUpdated(Date.now());

            const debugPerfTimerEnd = performance.now();
            console.log(`onNewAttribute took ${debugPerfTimerEnd - debugPerfTimerStart} ms`);
        }

        deviceManager.onNewDeviceAttribute(onNewAttribute);

        const onNewAttributeData = (deviceKey: string, attribute: DeviceAttribute) => {
            
            const debugPerfTimerStart = performance.now();

            setDevicesState((prevState) => {

                // TODO - refactor this as inefficient

                // Since prevState is an instance of DevicesState, we clone it and update it
                const newState = new DevicesState();
                Object.assign(newState, prevState);
                newState[deviceKey].deviceAttributes[attribute.name] = attribute;
                return newState;
            });
            setLastUpdated(Date.now());

            const debugPerfTimerEnd = performance.now();
            console.log(`onNewAttributeData took ${debugPerfTimerEnd - debugPerfTimerStart} ms`);
        }

        deviceManager.onNewAttributeData(onNewAttributeData);

        // TODO: Fetch initial state
        // Example cleanup, adjust based on your actual implementation
        // return () => deviceManager.offStateChange(updateDeviceState);
    }, [devicesState]);

    return (
        <div className="devices-container">
        {Object.entries(devicesState).filter(([key, _]) => key !== 'getDeviceKey').map(([deviceKey, data]) => (
            <DeviceScreen key={deviceKey} deviceKey={deviceKey} data={data} lastUpdated={lastUpdated} />
        ))}
      </div>
    );
}
