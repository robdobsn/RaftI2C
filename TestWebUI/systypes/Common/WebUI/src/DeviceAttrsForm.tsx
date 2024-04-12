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
                    <th>Attribute Name</th>
                    <th>Latest Value</th>
                </tr>
            </thead>
            <tbody>
                {Object.entries(deviceAttributes).map(([attributeName, attributeDetails]) => {
                    const latestValue = attributeDetails.values.length > 0 ? attributeDetails.values[attributeDetails.values.length - 1] : 'N/A';
                    return (
                        <tr key={attributeName}>
                            <td>{attributeName}</td>
                            <td>{latestValue}</td>
                        </tr>
                    );
                })}
            </tbody>
        </table>
    );
};

export default DeviceAttrsForm;
