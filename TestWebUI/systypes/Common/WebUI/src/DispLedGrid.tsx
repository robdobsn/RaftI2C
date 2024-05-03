import React, { useState } from 'react';
import DispOneLED from './DispOneLed';
import { CirclePicker } from 'react-color';
import { DeviceManager } from './DeviceManager';
import { DeviceTypeAction } from './DeviceInfo';

const deviceManager = DeviceManager.getInstance();

interface LEDGridProps {
  rows: number;
  cols: number;
  deviceKey: string;
  deviceAction: DeviceTypeAction;
}

const customColors = [
    '#000000', // black
    '#FFFFFF', // white
    '#FF0000', // red
    '#00FF00', // green
    '#0000FF', // blue
    '#FFFF00', // yellow
    '#FF00FF', // pink
    '#00FFFF', // cyan
    '#FFA500', // orange
    '#800080', // purple
    '#808080', // gray
    '#A52A2A', // brown
    '#008000', // dark green
    '#800000', // maroon
    '#008080', // teal
    '#000080', // navy
    '#FFD700', // gold
    '#FF4500', // orange red
    '#FF6347', // tomato
    // Add more custom colors as needed
  ];
  
const DispLEDGrid: React.FC<LEDGridProps> = ({ rows, cols, deviceKey, deviceAction }) => {
  // Initialize the grid with all LEDs turned off (black)
  const [colors, setColors] = useState<string[][]>(
    Array.from({ length: rows }, () => Array.from({ length: cols }, () => '#000000'))
  );
  const [activeLED, setActiveLED] = useState<{row: number, col: number} | null>(null);

  const handleLEDClick = (row: number, col: number) => {
    setActiveLED({ row, col });
  };

  const hexToRgb = (hex: string): number[] => {
    const r = parseInt(hex.slice(1, 3), 16);
    const g = parseInt(hex.slice(3, 5), 16);
    const b = parseInt(hex.slice(5, 7), 16);
    return [r, g, b];
  };

  const handleChangeComplete = (color: any) => {
    if (activeLED) {
      const { row, col } = activeLED;
      const newColors = colors.map((rowColors, rowIndex) =>
        rowColors.map((colColor, colIndex) =>
          rowIndex === row && colIndex === col ? color.hex : colColor
        )
      );
      setColors(newColors);
      setActiveLED(null); // Optionally close the picker automatically

      // Convert into a list of RGB values
      let colourList = [];
      for (let i = 0; i < rows; i++) {
        for (let j = 0; j < cols; j++) {
          colourList.push(hexToRgb(newColors[i][j]));
        }
      }

      // Send result
      deviceManager.sendCompoundAction(deviceKey, deviceAction, colourList);
    }
  };

  return (
    <div>
      <div style={{
        display: 'grid',
        gridTemplateColumns: `repeat(${cols}, 24px)`,
        gridTemplateRows: `${rows === 1 ? '1fr' : `repeat(${rows}, 24px)`}`,
        justifyContent: 'center',
        width: `${cols * 24}px`
      }}>
        {colors.map((row, rowIndex) =>
          row.map((color, colIndex) => (
            <DispOneLED key={`${rowIndex}-${colIndex}`} color={color} onClick={() => handleLEDClick(rowIndex, colIndex)} />
          ))
        )}
      </div>
      {activeLED && (
        <CirclePicker 
          color={colors[activeLED.row][activeLED.col]}
          colors={customColors}
          onChangeComplete={handleChangeComplete}
        />
      )}
    </div>
  );
};

export default DispLEDGrid;
