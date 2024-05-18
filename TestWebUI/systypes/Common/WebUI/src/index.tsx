import React from 'react';
import { createRoot } from 'react-dom/client';
import Main from './Main';
import './styles.css';

// Find the root element
const rootElement = document.getElementById('root');

// Create a root
const root = createRoot(rootElement!);

// Render the Main component into the root
root.render(<Main />);
