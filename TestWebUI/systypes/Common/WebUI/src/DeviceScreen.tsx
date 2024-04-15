
import React from 'react';
import { DeviceState } from './DeviceStates';
import DeviceAttrsForm from './DeviceAttrsForm';
import { DeviceLineChart } from './DeviceLineChart';
import './styles.css';

export interface DeviceScreenProps {
    deviceKey: string;
    data: DeviceState;
}

const DeviceScreen = (props: DeviceScreenProps) => {
    return (
      <div className="device-screen">
        <div className="device-block-heading">
          <div className="device-block-heading-text">Device {props.data.deviceTypeInfo.name} Address {props.deviceKey}</div>
        </div>
        <div className="device-block-data">
          {/* <p>Data: {JSON.stringify(props.data)}</p> */}
          <div  className="device-attrs-form">
            <DeviceAttrsForm deviceState={props.data} />
          </div>
          <div className="device-line-chart">
            <DeviceLineChart deviceState={props.data} />
          </div>
        </div>
      </div>
    );
  };
  
export default DeviceScreen;
