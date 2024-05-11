import React, { useEffect, useRef, useState } from 'react';
import { DeviceManager } from "./DeviceManager";
import { DeviceTypeAction } from './DeviceInfo';
import DispLEDGrid from './DispLedGrid';

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
        const actions: DeviceTypeAction[] = deviceTypeInfo.actions || [];
        setDeviceActions(actions);
        // Initialize input values
        const initialValues: InputValues = actions.reduce((acc, action) => {
            acc = { ...acc, [action.n]: action.d ? action.d : (action.r ? action.r[0] | 0 : 0) };
            return acc;
        }, {});
        setInputValues(initialValues);
        queueSendAction(initialValues);
    }, [deviceKey]);

    const handleInputChange = (name: string, value: number) => {
        const newValues = { ...inputValues, [name]: value };
        setInputValues(newValues);
        queueSendAction(newValues);
    };

    const queueSendAction = (newValues: InputValues) => {
        if (sendTimer.current) {
            clearTimeout(sendTimer.current);
        }
        // console.log(`queueSendAction new values ${JSON.stringify(newValues)}`);
        sendTimer.current = setTimeout(() => {
            deviceActions.forEach(action => {
                const currentValue = newValues[action.n];
                const lastSentValue = lastSentValues[action.n];
                if (currentValue !== lastSentValue) {
                    // console.log(`queueSendAction timeout ${action.n} ${currentValue}`);
                    handleSendAction(action, currentValue);
                    setLastSentValues(prev => ({ ...prev, [action.n]: currentValue }));
                }
            });
        }, 300);
    };
    
    const handleSendAction = (action: DeviceTypeAction, value: number) => {
        // Send action to device
        deviceManager.sendAction(deviceKey, action, [value]);
    };

    if (deviceActions.length === 0) {
        return <></>;
    }

    return (
        <div className="device-actions-form">
            <table>
                <thead>
                    <tr>
                        <th>Name</th>
                        <th>Value</th>
                        <th>Send</th>
                    </tr>
                </thead>
                <tbody>
                    {deviceActions.map((action) => {
                        if (action.f === "LEDPIX") {
                            return (
                                <tr key={action.n}>
                                    <td>{action.n}</td>
                                    <td colSpan={2}>
                                        <DispLEDGrid
                                            rows={action.NY || 1}
                                            cols={action.NX || 1}
                                            deviceKey={deviceKey}
                                            deviceAction={action}
                                        />
                                    </td>
                                </tr>
                            );
                        } else {
                            return (
                                <tr key={action.n}>
                                    <td>{action.n}</td> 
                                    <td>
                                        {action.t ? 
                                            <input type="number" 
                                                min={action.r?.[0] ?? 0} 
                                                max={action.r?.[1] ?? 100} 
                                                value={inputValues[action.n]} 
                                                onChange={e => {
                                                        console.log(`input change ${action.n} ${e.target.value}`)
                                                        handleInputChange(action.n, parseInt(e.target.value));
                                                    }}
                                            />
                                            : <></>
                                        }
                                    </td>
                                    <td>
                                        <button onClick={() => handleSendAction(action, inputValues[action.n])}>Send</button>
                                    </td>
                                </tr>
                            );
                        }
                    })}                        
                </tbody>
            </table>
        </div>
    );
};

export default DeviceActionsForm;
