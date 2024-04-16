
import React from 'react';
import { DeviceState } from './DeviceStates';
import DeviceAttrsForm from './DeviceAttrsForm';
import { DeviceLineChart } from './DeviceLineChart';
import './styles.css';

export interface DeviceScreenProps {
    deviceKey: string;
    data: DeviceState;
    lastUpdated: number;
}

const DeviceScreen = ({ deviceKey, data, lastUpdated }: DeviceScreenProps) => {

    // Gray out the device screen if the device is offline
    const isOnline = data.deviceIsOnline;
    const offlineClass = isOnline ? '' : 'offline';

    return (
      <div className={`device-screen ${offlineClass}`}>
        <div className="device-block-heading">
          <div className="device-block-heading-text">Device {data.deviceTypeInfo.name} Address {deviceKey}</div>
        </div>
        <div className="device-block-data">
          {/* <p>Data: {JSON.stringify(data)}</p> */}
          <div  className="device-attrs-form">
            <DeviceAttrsForm deviceState={data} />
          </div>
          <div className="device-line-chart">
            <DeviceLineChart deviceState={data} lastUpdated={lastUpdated} />
          </div>
        </div>
      </div>
    );
  };
  
export default DeviceScreen;
