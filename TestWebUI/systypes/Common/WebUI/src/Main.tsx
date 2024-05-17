import React, { useEffect, useRef, useState } from 'react';
import ReactDOM from 'react-dom';
import './Main.css';
import { DeviceManager } from './DeviceManager';
import SettingsManager from './SettingsManager';
import DevicesScreen from './DevicesScreen';
import './styles.css';

const settingsManager = SettingsManager.getInstance();

export default function Main() {

    const [menuOpen, setMenuOpen] = useState<boolean>(false);
    const [settingsOpen, setSettingsOpen] = useState<boolean>(false);
    const [settings, setSettings] = useState(settingsManager.getSettings(true));
    const menuRef = useRef<HTMLDivElement>(null);

    useEffect(() => {
        const initDeviceManager = async () => {
            console.log(`initDeviceManager`);
            await DeviceManager.getInstance().init();
        };

        initDeviceManager().catch((err) => {
            console.error(`Error initializing DeviceManager: ${err}`);
        });
    }, []);

    const handleSettings = () => {
        setSettingsOpen(true);
        setMenuOpen(false);
    };

    const handleSaveSettings = () => {
        settingsManager.setSettings(settings);
        setSettingsOpen(false);
    };

    return (
        <>
            <div>
                <h1>Raft I2C Auto-identification and Polling Pub-Sub</h1>
                <div className="menu-icon always-enabled" onClick={() => setMenuOpen(!menuOpen)}>☰</div>
                {menuOpen && (
                    <div className="dropdown-menu" ref={menuRef}>
                        <div className="menu-item always-enabled" onClick={handleSettings}>Settings</div>
                    </div>
                )}
            </div>
            <div className="h-full">
                <DevicesScreen />
            </div>
            {settingsOpen && (
                <div className="settings-modal">
                    <div className="settings-content">
                        <h3>Settings</h3>
                        <label>
                            Max Data Points in Chart:
                            <input
                                type="number"
                                value={settings.maxChartPoints}
                                onChange={(e) => setSettings({ ...settings, maxChartPoints: parseInt(e.target.value) })}
                            />
                        </label>
                        <label>
                            Max Data Points to Store:
                            <input
                                type="number"
                                value={settings.maxStoredPoints}
                                onChange={(e) => setSettings({ ...settings, maxStoredPoints: parseInt(e.target.value) })}
                            />
                        </label>
                        <button onClick={handleSaveSettings}>Save</button>
                        <button onClick={() => setSettingsOpen(false)}>Close</button>
                    </div>
                </div>
            )}
        </>
    );
}

ReactDOM.render(<Main />, document.getElementById('root'));