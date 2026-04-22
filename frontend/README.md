# Smart Home IoT Platform - Frontend

A React-based web frontend for the Smart Home IoT Platform, providing a user-friendly interface to monitor and control IoT devices.

## Features

- **Dashboard**: Overview of system health, recent telemetry, and key metrics
- **Nodes**: Detailed view of all IoT nodes with their latest sensor data
- **Telemetry**: Interactive charts and data visualization for sensor readings
- **Commands**: Send control commands to IoT devices

## Technology Stack

- **React 19** with TypeScript
- **Vite** for fast development and building
- **React Router** for navigation
- **Recharts** for data visualization
- **Axios** for API communication
- **Docker** for containerization

## Development

### Prerequisites

- Node.js 18+
- npm or yarn

### Installation

```bash
npm install
```

### Development Server

```bash
npm run dev
```

The development server will start on `http://localhost:5173`

### Build for Production

```bash
npm run build
```

### Docker

The frontend is containerized and can be run with Docker Compose:

```bash
docker-compose up frontend
```

## API Integration

The frontend communicates with the backend API running on `http://localhost:8000`. The API provides:

- Telemetry data from IoT sensors
- Node management and status
- Command execution for device control
- System health monitoring

## Project Structure

```
src/
├── components/     # Reusable UI components
├── pages/         # Main application pages
├── services/      # API services and utilities
└── main.tsx       # Application entry point
```

## Environment Variables

- `VITE_API_URL`: Backend API URL (default: `http://localhost:8000`)

## Contributing

1. Follow the existing code style
2. Add TypeScript types for new features
3. Test API integrations thoroughly
4. Update documentation as needed
