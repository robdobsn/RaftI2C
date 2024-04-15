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
    
    useEffect(() => {
        const onNewDevice = (deviceKey: string, newDeviceState: DeviceState) => {
            setDevicesState((prevState) => {
                // Since prevState is an instance of DevicesState, we clone it and update it
                const newState = new DevicesState();
                Object.assign(newState, prevState);
                newState[deviceKey] = newDeviceState;
                return newState;
            });
        };

        deviceManager.onNewDevice(onNewDevice);

        const onNewAttribute = (deviceKey: string, attribute: DeviceAttribute) => {
            setDevicesState((prevState) => {

                // TODO - refactor this as inefficient

                // Since prevState is an instance of DevicesState, we clone it and update it
                const newState = new DevicesState();
                Object.assign(newState, prevState);
                newState[deviceKey].deviceAttributes[attribute.name] = attribute;
                return newState;
            });
        }

        deviceManager.onNewDeviceAttribute(onNewAttribute);

        const onNewAttributeData = (deviceKey: string, attribute: DeviceAttribute) => {
            setDevicesState((prevState) => {

                // TODO - refactor this as inefficient

                // Since prevState is an instance of DevicesState, we clone it and update it
                const newState = new DevicesState();
                Object.assign(newState, prevState);
                newState[deviceKey].deviceAttributes[attribute.name] = attribute;
                return newState;
            });
        }

        deviceManager.onNewAttributeData(onNewAttributeData);

        // TODO: Fetch initial state
        // Example cleanup, adjust based on your actual implementation
        // return () => deviceManager.offStateChange(updateDeviceState);
    }, [devicesState]);

    return (
        <div className="devices-container">
        {Object.entries(devicesState).filter(([key, _]) => key !== 'getDeviceKey').map(([deviceKey, data]) => (
            <DeviceScreen key={deviceKey} deviceKey={deviceKey} data={data} />
        ))}
      </div>
    );
}
