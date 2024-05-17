import React, { useEffect, useRef, useState } from 'react';
import { DeviceState } from './DeviceStates';
import DeviceAttrsForm from './DeviceAttrsForm';
import DeviceLineChart from './DeviceLineChart';
import './styles.css';
import { DeviceManager } from './DeviceManager';
import DeviceCmdsForm from './DeviceActionsForm';

const deviceManager = DeviceManager.getInstance();

export interface DeviceScreenProps {
    deviceKey: string;
    lastUpdated: number;
}

const DeviceScreen = ({ deviceKey, lastUpdated }: DeviceScreenProps) => {
    const data: DeviceState = deviceManager.getDeviceState(deviceKey);

    // Gray out the device screen if the device is offline
    const offlineClass = data.isOnline ? '' : 'offline';

    const [timedChartUpdate, setTimedChartUpdate] = useState<number>(0);
    const [menuOpen, setMenuOpen] = useState<boolean>(false);
    const menuRef = useRef<HTMLDivElement>(null);

    useEffect(() => {
        const startTime = Date.now();
        const updateChart = () => {
            setTimedChartUpdate(Date.now());
        };
        const updateTimer = setInterval(updateChart, 500);
        return () => clearInterval(updateTimer);
    }, []);

    useEffect(() => {
        const handleClickOutside = (event: MouseEvent) => {
            if (menuRef.current && !menuRef.current.contains(event.target as Node)) {
                setMenuOpen(false);
            }
        };
        document.addEventListener("mousedown", handleClickOutside);
        return () => {
            document.removeEventListener("mousedown", handleClickOutside);
        };
    }, []);

    const handleCopyToClipboard = () => {
      const headers = ["Timestamp (us)"];
      const rows: string[][] = [];

      const timestamps = data.deviceTimeline.timestampsUs;
      const attributes = data.deviceAttributes;

      // Collect headers and initialize rows with timestamps
      Object.keys(attributes).forEach(attrName => {
          headers.push(attrName);
      });

      timestamps.forEach((timestamp, index) => {
          const row: string[] = [timestamp.toString()];
          Object.keys(attributes).forEach(attrName => {
              const values = attributes[attrName].values;
              row.push(values[index]?.toString() || "");
          });
          rows.push(row);
      });

      // Create a tab-separated string
      const csvContent = [headers.join("\t"), ...rows.map(row => row.join("\t"))].join("\n");

      navigator.clipboard.writeText(csvContent);
      // alert("Device values copied to clipboard!");
      setMenuOpen(false);
  };

    const handleSettings = () => {
        alert("Settings clicked!");
        setMenuOpen(false);
    };

    return (
        <div className={`device-screen ${offlineClass}`}>
            <div className="device-block-heading">
                <div className="device-block-heading-text">Device {data.deviceTypeInfo.name} Address {deviceKey}{!data.isOnline ? " (Offline)" : ""}</div>
                <div className="menu-icon always-enabled" onClick={() => setMenuOpen(!menuOpen)}>☰</div>
                {menuOpen && (
                    <div className="dropdown-menu" ref={menuRef}>
                        <div className="menu-item always-enabled" onClick={handleCopyToClipboard}>Copy Values to Clipboard</div>
                        <div className="menu-item always-enabled" onClick={handleSettings}>Settings</div>
                    </div>
                )}
            </div>
            <div className={`device-block-data`}>
                <div className="device-attrs-and-actions">
                    <DeviceAttrsForm deviceKey={deviceKey} lastUpdated={lastUpdated} />
                    <DeviceCmdsForm deviceKey={deviceKey} />
                </div>
                <DeviceLineChart deviceKey={deviceKey} lastUpdated={timedChartUpdate} />
            </div>
        </div>
    );
};

export default DeviceScreen;
