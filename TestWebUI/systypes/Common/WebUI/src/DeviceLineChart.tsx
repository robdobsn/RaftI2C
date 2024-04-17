import React, { useEffect, useState, memo } from "react";
import { Chart as ChartJS, CategoryScale, LinearScale, PointElement, LineElement, ArcElement, Tooltip, Legend } from 'chart.js';
import { Line } from "react-chartjs-2";
import { DeviceState } from "./DeviceStates";
import { DeviceManager } from "./DeviceManager";

const deviceManager = DeviceManager.getInstance();

ChartJS.register(
    CategoryScale,
    LinearScale,
    PointElement,
    LineElement,
    ArcElement,
    Tooltip,
    Legend
);

export interface DeviceLineChartProps {
    deviceKey: string;
    lastUpdated: number;
}

interface ChartJSData {
    labels: string[];
    datasets: {
        label: string;
        data: number[];
        fill: boolean;
        borderColor: string;
        backgroundColor: string;
    }[];
}

const DeviceLineChart: React.FC<DeviceLineChartProps> = memo(({ deviceKey, lastUpdated }) => {

    const deviceState: DeviceState = deviceManager.getDeviceState(deviceKey);
    const { deviceAttributes, deviceTimeline } = deviceState;
    const MAX_DATA_POINTS = 100;
    const [chartData, setChartData] = useState<ChartJSData>({
        labels: [],
        datasets: []
    });

    const options = {
        responsive: true,
        maintainAspectRatio: false,
        animation: {
            duration: 1, // default is 1000ms
        },
    };

    const colourMap: { [key: string]: string } = {
        prox: "hsl(60, 70%, 60%)",
        als: "hsl(0, 70%, 60%)",
        white: "hsl(120, 70%, 60%)",
        x: "hsl(240, 70%, 60%)",
        y: "hsl(300, 70%, 60%)",
        z: "hsl(0, 70%, 60%)",
        temperature: "hsl(360, 70%, 60%)",
        humidity: "hsl(200, 70%, 60%)",
    };

    useEffect(() => {
        const labels = deviceTimeline.slice(-MAX_DATA_POINTS).map(String);
        const datasets = Object.entries(deviceAttributes).map(([attributeName, attributeDetails]) => {
            const data = attributeDetails.values.slice(-MAX_DATA_POINTS);
            const colour = colourMap[attributeName] || `hsl(${Math.random() * 360}, 70%, 60%)`;
            return {
                label: attributeName,
                data: data,
                fill: false,
                borderColor: colour,
                backgroundColor: colour
            };
        });
        setChartData({ labels, datasets });
    }, [lastUpdated]);

    return <Line data={chartData} options={options} />;
});

export default DeviceLineChart;
