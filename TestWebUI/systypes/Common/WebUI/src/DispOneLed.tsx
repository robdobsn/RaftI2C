import React from 'react';

interface LEDProps {
  color: string;
  onClick: () => void;
}

const DispOneLED: React.FC<LEDProps> = ({ color, onClick }) => {
  return (
    <div style={{
      width: '20px',
      height: '20px',
      backgroundColor: color,
      margin: '2px',
      cursor: 'pointer'
    }} onClick={onClick} />
  );
};

export default DispOneLED;
