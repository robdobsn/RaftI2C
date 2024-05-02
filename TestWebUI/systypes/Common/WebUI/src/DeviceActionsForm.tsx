import React, { useEffect, useRef, useState } from 'react';
import { DeviceManager } from "./DeviceManager";
import { DeviceTypeAction } from './DeviceInfo';

const deviceManager = DeviceManager.getInstance();

type DeviceActionsTableProps = {
    deviceKey: string;
};

interface InputValues {
    [key: string]: number;
}

const DeviceActionsForm: React.FC<DeviceActionsTableProps> = ({ deviceKey }) => {
    const [deviceActions, setDeviceActions] = useState<DeviceTypeAction[]>([]);
    const [inputValues, setInputValues] = useState<InputValues>({});
    const [lastSentValues, setLastSentValues] = useState<InputValues>({});
    const sendTimer = useRef<NodeJS.Timeout | null>(null);
    
    useEffect(() => {
        const deviceState = deviceManager.getDeviceState(deviceKey);
        const { deviceTypeInfo } = deviceState;
        const actions: DeviceTypeAction[] = deviceTypeInfo.actions?.x || [];
        setDeviceActions(actions);
        // Initialize input values
        const initialValues: InputValues = actions.reduce((acc, action) => {
            acc = { ...acc, [action.n]: action.r[0] };
            return acc;
        }, {});
        setInputValues(initialValues);
        queueSendAction();
    }, [deviceKey]);

    const handleInputChange = (name: string, value: number) => {
        setInputValues(prev => ({ ...prev, [name]: value }));
        queueSendAction();
    };

    const queueSendAction = () => {
        if (sendTimer.current) {
            clearTimeout(sendTimer.current);
        }
        sendTimer.current = setTimeout(() => {
            deviceActions.forEach(action => {
                const currentValue = inputValues[action.n];
                const lastSentValue = lastSentValues[action.n];
                if (currentValue !== lastSentValue) {
                    handleSendAction(action);
                    setLastSentValues(prev => ({ ...prev, [action.n]: currentValue })); // Update last sent values
                }
            });
        }, 500);  // Queue a delayed action every 500ms (2 per second)
    };
    
    const handleSendAction = (action: DeviceTypeAction) => {
        const value = inputValues[action.n];

        // Send action to device
        deviceManager.sendAction(deviceKey, action, value);
    };

    if (deviceActions.length === 0) {
        return <p></p>;
    }

    return (
        <table>
            <thead>
                <tr>
                    <th>Name</th>
                    <th>Value</th>
                    <th>Send</th>
                </tr>
            </thead>
            <tbody>
                {deviceActions.map((action) => (
                    <tr key={action.n}>
                        <td>{action.n}</td>
                        <td>
                            <input type="number" 
                                   min={action.r[0]} 
                                   max={action.r[1]} 
                                   value={inputValues[action.n]} 
                                   onChange={e => {
                                        handleInputChange(action.n, parseInt(e.target.value));
                                    }}
                            />
                        </td>
                        <td>
                            <button onClick={() => handleSendAction(action)}>Send</button>
                        </td>
                    </tr>
                ))}
            </tbody>
        </table>
    );
};

export default DeviceActionsForm;
