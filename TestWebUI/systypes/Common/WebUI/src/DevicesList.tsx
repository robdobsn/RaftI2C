import React, { useEffect, useState } from 'react';
import { DevicesState } from './DeviceStates';

class DeviceListProps {
    deviceStates: DevicesState = new DevicesState();
}

export default function DevicesList(props: DeviceListProps) {
    const [devices, setDevices] = useState<DevicesState>(new DevicesState());

    return (
        <div>
            {/* <h2>Device List</h2>
            <ul>
                {devices.devices.map((devices) => {
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
            </ul> */}
        </div>
    );
};
