// Component which uses the DeviceList component to display the list of devices

import React, { useEffect, useState } from 'react';
import { DeviceAttributeState, DevicesState, DeviceState } from './DeviceStates';
import { DeviceManager } from './DeviceManager';
import DeviceScreen from './DeviceScreen';
import './styles.css';

const deviceManager = DeviceManager.getInstance();

export class DevicesScreenProps {
    constructor(
    ) { }
}

export default function DevicesScreen(props: DevicesScreenProps) {
    const [lastUpdated, setLastUpdated] = useState<number>(0);
    
    useEffect(() => {
        const onNewDevice = (deviceKey: string, newDeviceState: DeviceState) => {
            setLastUpdated(Date.now());
        };

        deviceManager.onNewDevice(onNewDevice);

        const onNewAttribute = (deviceKey: string, attribute: DeviceAttributeState) => {
            setLastUpdated(Date.now());
        }

        deviceManager.onNewDeviceAttribute(onNewAttribute);

        const onNewAttributeData = (deviceKey: string, attribute: DeviceAttributeState) => {
            setLastUpdated(Date.now());
        }

        deviceManager.onNewAttributeData(onNewAttributeData);

    }, [lastUpdated]);

    const devicesState: DevicesState = deviceManager.getDevicesState();
    
    return (
        <div className="devices-container">
        {Object.entries(devicesState).filter(([key, _]) => key !== 'getDeviceKey').map(([deviceKey, data]) => (
            <DeviceScreen key={deviceKey} deviceKey={deviceKey} lastUpdated={lastUpdated} />
        ))}
      </div>
    );
}
