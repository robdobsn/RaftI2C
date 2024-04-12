
import React from 'react';
import { DeviceState } from './DeviceStates';
import DeviceAttrsForm from './DeviceAttrsForm';

export interface DeviceScreenProps {
    deviceKey: string;
    data: DeviceState;
}

const DeviceScreen = (props: DeviceScreenProps) => {
    return (
      <div>
        <h2>Device {props.data.deviceTypeInfo.name} Address {props.deviceKey}</h2>
        {/* <p>Data: {JSON.stringify(props.data)}</p> */}
        <DeviceAttrsForm deviceState={props.data} />
      </div>
    );
  };
  
export default DeviceScreen;
