import React from 'react';
import { DeviceState } from './DeviceStates';

type DeviceAttributesTableProps = {
    deviceState: DeviceState;
};

const DeviceAttrsForm: React.FC<DeviceAttributesTableProps> = ({ deviceState }) => {
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
                {Object.entries(deviceAttributes).map(([attributeName, attributeDetails]) => {
                    const latestValue = attributeDetails.values.length > 0 ? attributeDetails.values[attributeDetails.values.length - 1] : 'N/A';
                    return (
                        <tr key={attributeName}>
                            <td>{attributeName}</td>
                            <td>{latestValue}</td>
                            <td>{attributeDetails.units}</td>
                        </tr>
                    );
                })}
            </tbody>
        </table>
    );
};

export default DeviceAttrsForm;
