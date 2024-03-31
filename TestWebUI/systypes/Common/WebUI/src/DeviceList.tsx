import React, { useEffect, useState } from 'react';
import { DeviceStates } from './DeviceStates';

class DeviceListProps {
    deviceStates: DeviceStates = new DeviceStates();
}

export default function DeviceList(props: DeviceListProps) {
    const [devices, setDevices] = useState<DeviceStates>(new DeviceStates());

    return (
        <div>
            <h2>Device List</h2>
            <ul>
                {devices.devices.map((device) => {
                    return (
                        <li key={device.i2cAddress}>
                            <span>{device.deviceType}</span>
                            <span>{device.i2cAddress}</span>
                            <span>{device.i2cSlot}</span>
                            <span>{device.active}</span>
                            <span>{device.lastReportTimeMs}</span>
                            <span>{device.lastReportData}</span>
                        </li>
                    );
                })}
            </ul>
        </div>
    );
};
