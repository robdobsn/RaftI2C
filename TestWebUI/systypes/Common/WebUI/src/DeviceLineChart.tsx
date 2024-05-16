import React, { useEffect, useState, memo, useRef } from "react";
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
        yAxisID: string;
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
        scales: {}
    };

    const colourMapRef = useRef<{ [key: string]: string }>({
        prox: "hsl(60, 70%, 60%)",
        als: "hsl(0, 70%, 60%)",
        white: "hsl(120, 70%, 60%)",
        x: "hsl(240, 70%, 60%)",
        y: "hsl(300, 70%, 60%)",
        z: "hsl(0, 70%, 60%)",
        dist: "hsl(60, 70%, 60%)",
        temperature: "hsl(360, 70%, 60%)",
        humidity: "hsl(200, 70%, 60%)",
    });

    useEffect(() => {
        const labels = deviceTimeline.timestampsUs.slice(-MAX_DATA_POINTS).map(time => {
            const seconds = time / 1e6; // Convert microseconds to seconds
            const secondsStr = seconds.toFixed(3); // Format decimal places
            return secondsStr;
        });

        const uniqueAxes = new Map<string, { range: [number, number], units: string }>();
        const datasets = Object.entries(deviceAttributes)
            .filter(([attributeName, attributeDetails]) => attributeDetails.visibleSeries !== false)
            .map(([attributeName, attributeDetails]) => {
                const data = attributeDetails.values.slice(-MAX_DATA_POINTS);
                let colour = colourMapRef.current[attributeName];
                if (!colour) {
                    colour = `hsl(${Math.random() * 360}, 70%, 60%)`;
                    colourMapRef.current[attributeName] = colour;
                }
                const axisKey = `${attributeDetails.range}-${attributeDetails.units}`;
                if (!uniqueAxes.has(axisKey)) {
                    uniqueAxes.set(axisKey, { range: attributeDetails.range, units: attributeDetails.units });
                }
                return {
                    label: attributeName,
                    data: data,
                    fill: false,
                    borderColor: colour,
                    backgroundColor: colour,
                    yAxisID: axisKey
                };
            });

        const scales: { [key: string]: any } = {};
        uniqueAxes.forEach((axis, key) => {
            scales[key] = {
                type: 'linear',
                display: true,
                position: 'left',
                scaleLabel: {
                    display: true,
                    labelString: axis.units
                },
                ticks: {
                    min: axis.range[0],
                    max: axis.range[1]
                }
            };
        }
        );
        options.scales = scales;
        setChartData({ labels, datasets });
    }
        , [lastUpdated]);

    if (Object.keys(deviceAttributes).length === 0) {
        return <></>;
    }

    return (
        <div className="device-line-chart">
            <Line data={chartData} options={options} />
        </div>
    );
});

export default DeviceLineChart;
