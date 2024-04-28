import React, { useEffect } from 'react';
import ReactDOM from 'react-dom';
import './Main.css';
import { DeviceManager } from './DeviceManager';
import DevicesScreen from './DevicesScreen';
import { DevicesConfig } from './DevicesConfig';
import './styles.css';

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
    <>
    <h1>Raft I2C Auto-identification and Polling Pub-Sub</h1>
    <div className="h-full">
      <DevicesScreen isEditingMode={false} config={new DevicesConfig()} />
    </div>
    </>
  );
}

ReactDOM.render(<Main />, document.getElementById('root'));