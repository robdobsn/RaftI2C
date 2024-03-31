// Component which uses the DeviceList component to display the list of devices

import React, { useEffect, useState } from 'react';
import { DeviceStates } from './DeviceStates';
import { DeviceManager } from './DeviceManager';
import DeviceList from './DeviceList';
import { DeviceConfig } from './DeviceConfig';

const deviceManager = DeviceManager.getInstance();

export class DeviceScreenProps {
    constructor(
        public isEditingMode: boolean,
        public config: DeviceConfig
    ) { }
}

export default function DeviceScreen(props: DeviceScreenProps) {
    const [deviceStates, setDeviceStates] = useState<DeviceStates>(new DeviceStates());
    
    useEffect(() => {
        deviceManager.onStateChange((newDeviceStates) => {
            setDeviceStates(newDeviceStates);
        });
    }, []);
    
    return (
        <DeviceList deviceStates={deviceStates} />
    );
}
