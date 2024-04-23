import React from 'react';
import { DeviceState, deviceAttrGetLatestFormatted } from './DeviceStates';
import { DeviceManager } from "./DeviceManager";

const deviceManager = DeviceManager.getInstance();

type DeviceAttributesTableProps = {
    deviceKey: string;
    lastUpdated: number;
};

const DeviceAttrsForm: React.FC<DeviceAttributesTableProps> = ({ deviceKey, lastUpdated }) => {
    const deviceState = deviceManager.getDeviceState(deviceKey);
    const { deviceAttributes } = deviceState;

    return (
        <table>
            <thead>
                <tr>
                    <th>Name</th>
                    <th>Value</th>
                    <th>Units</th>
                </tr>
            </thead>
            <tbody>
                {Object.entries(deviceAttributes)
                    .filter(([attributeName, attributeDetails]) => attributeDetails.display !== false)
                    .map(([attributeName, attributeDetails]) => {
                        const valStr = deviceAttrGetLatestFormatted(attributeDetails)
                        return (
                            <tr key={attributeName}>
                                <td>{attributeName}</td>
                                <td>{valStr}</td>
                                <td>{attributeDetails.units}</td>
                            </tr>
                        );
                })}
            </tbody>
        </table>
    );
};

export default DeviceAttrsForm;
