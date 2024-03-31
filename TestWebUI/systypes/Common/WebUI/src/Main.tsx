import React, { useEffect } from 'react';
import ReactDOM from 'react-dom';
import './Main.css';
import { DeviceManager } from './DeviceManager';
import DeviceScreen from './DevicesScreen';
import { DeviceConfig } from './DeviceConfig';

export default function Main() {

  const [isEditingMode, setEditingMode] = React.useState(false);

  useEffect(() => {
    const initDeviceManager = async () => {
      console.log(`initDeviceManager`);
      await DeviceManager.getInstance().init();
    };

    initDeviceManager().catch((err) => {
      console.error(`Error initializing DeviceManager: ${err}`);
    });
  }, []);

  return (
    <div className="h-full">
      <DeviceScreen isEditingMode={false} config={new DeviceConfig()} />
    </div>
  );
}

ReactDOM.render(<Main />, document.getElementById('root'));