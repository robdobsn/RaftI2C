import React from "react";
import { Chart as ChartJS, CategoryScale, LinearScale, PointElement, LineElement, ArcElement, Tooltip, Legend } from 'chart.js';
import { Line } from "react-chartjs-2";
import { DeviceState } from "./DeviceStates";

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
    deviceState: DeviceState;
}

export const DeviceLineChart: React.FC<DeviceLineChartProps> = ({ deviceState }) => {
    const { deviceAttributes } = deviceState;
    const MAX_DATA_POINTS = 100;

    // Extract the data series (attribute values)
    const dataLabels = deviceState.deviceTimeline.slice(-MAX_DATA_POINTS);

    // Create a dataset for each attribute found in deviceAttributes
    const datasets = Object.entries(deviceAttributes).map(([attributeName, attributeDetails]) => {
        const data = attributeDetails.values.slice(-MAX_DATA_POINTS);
        // Generate a random color for each attribute line
        const color = `hsl(${Math.random() * 360}, 70%, 60%)`;

        return {
            label: attributeName,
            data: data,
            fill: false,
            borderColor: color,
            backgroundColor: color
        };
    });

    const chartData = {
        labels: dataLabels,
        datasets: datasets
    };

    return <Line data={chartData} />;
};
