import React, { useState } from 'react';
import DispOneLED from './DispOneLed';

interface LEDGridProps {
  rows: number;
  cols: number;
}

const DispLEDGrid: React.FC<LEDGridProps> = ({ rows, cols }) => {
  // Initialize the grid with all LEDs turned off (black)
  const [colors, setColors] = useState<string[][]>(
    Array.from({ length: rows }, () => Array.from({ length: cols }, () => '#000000'))
  );

  const handleLEDClick = (row: number, col: number) => {
    const newColor = prompt('Enter a new RGB color (hex code):', colors[row][col]);
    if (newColor) {
      const newColors = colors.map((rowColors, rowIndex) =>
        rowColors.map((color, colIndex) =>
          rowIndex === row && colIndex === col ? newColor : color
        )
      );
      setColors(newColors);
    }
  };

  return (
    <div style={{
            display: 'grid',
            gridTemplateColumns: `repeat(${cols}, 24px)`, // Defines 10 columns if cols is 10
            gridTemplateRows: `${rows === 1 ? '1fr' : `repeat(${rows}, 24px)`}`, // Explicitly setting rows
            justifyContent: 'center',
            width: `${cols * 24}px` // Ensures the container is wide enough
          }}
    >
      {colors.map((row, rowIndex) =>
        row.map((color, colIndex) => (
          <DispOneLED key={`${rowIndex}-${colIndex}`} color={color} onClick={() => handleLEDClick(rowIndex, colIndex)} />
        ))
      )}
    </div>
  );
};

export default DispLEDGrid;
