# Smart Home IoT Dashboard

A modern IoT platform for monitoring and controlling smart home sensors, featuring a real-time React dashboard, FastAPI backend, and TimescaleDB for time-series data.

## Features

- **Real-time Sensor Monitoring**: Live dashboard with sensor cards for thermostats, door sensors, and pet detectors
- **Interactive Controls**: Adjust thermostat set temperatures with precision controls
- **Historical Data Visualization**: View sensor history with interactive charts using Recharts
- **Battery & Signal Monitoring**: Track device battery levels and RSSI for all sensors
- **RESTful API**: FastAPI backend for data ingestion and management
- **Time-Series Database**: PostgreSQL with TimescaleDB for efficient telemetry storage
- **Docker Deployment**: Complete containerized setup with Docker Compose
- **Dummy Data Generation**: Python script for populating test data

## Architecture

### Backend (FastAPI)
- REST API for node management, telemetry ingestion, and commands
- Pydantic models for data validation
- SQLAlchemy ORM with TimescaleDB for time-series queries
- Automatic API documentation at `/docs`

### Frontend (React + TypeScript)
- Responsive dashboard with sensor components
- Real-time data fetching with Axios
- Interactive charts for historical data
- Modular component architecture

### Database (PostgreSQL + TimescaleDB)
- Time-series optimized storage for sensor telemetry
- Relational data for nodes and sensors
- Adminer web interface for database management

## Quick Start

### Prerequisites
- Docker and Docker Compose
- Python 3.10+ (for dummy data script)

### Run the Application
```bash
# Clone the repository
git clone <repository-url>
cd smart_home

# Start all services
docker compose up --build
```

### Access Points
- **Dashboard**: http://localhost
- **API Documentation**: http://localhost:8000/docs
- **Database Admin**: http://localhost:8080
- **Node-RED**: http://localhost:1880

### Add Test Data
```bash
# Populate with sample nodes and telemetry
python dummy-data.py
```

## Dashboard Overview

The React frontend provides a comprehensive view of your smart home sensors:

### Sensor Cards
Each sensor displays:
- **Node Name**: Identifies the physical location
- **Current Readings**: Temperature, humidity, status, etc.
- **Battery Level**: Visual indicator with percentage
- **RSSI**: Signal strength indicator
- **History Button**: Opens modal with historical charts

### Thermostat Controls
- View current temperature and humidity
- Adjust set temperature with +/- buttons (0.1°C precision)
- Set temperature initializes from API data

### History Modals
- Interactive line charts for sensor data
- Dual-axis charts for temperature/humidity
- Time-based data visualization
- Sensor-specific chart types

## API Reference

### Core Endpoints
- `GET /api/v1/node/latest` - Get all nodes with latest telemetry
- `POST /api/v1/uplink` - Ingest sensor data
- `GET /api/v1/telemetry` - Query historical data
- `POST /api/v1/node` - Register new nodes

### Example: Get Latest Data
```bash
curl http://localhost:8000/api/v1/node/latest
```

### Example: Send Telemetry
```bash
curl -X POST http://localhost:8000/api/v1/uplink \
  -H 'Content-Type: application/json' \
  -d '{
    "nodeId": "living-room",
    "measurements": [
      {
        "sensorId": 1,
        "sensorType": "thermostat",
        "key": "temperature",
        "value": 22.5,
        "batteryPct": 85
      }
    ]
  }'
```

## Development

### Backend Setup
```bash
cd app
pip install -r ../requirements.txt
uvicorn main:app --reload
```

### Frontend Setup
```bash
cd frontend
npm install
npm run dev
```

### Database Access
- Host: localhost:5433
- User: postgres
- Password: postgres
- Database: postgres

## Project Structure

```
smart_home/
├── app/                    # FastAPI backend
│   ├── api/
│   ├── models/
│   ├── schemas/
│   └── services/
├── frontend/               # React dashboard
│   ├── src/
│   │   ├── components/
│   │   ├── pages/
│   │   └── services/
│   └── package.json
├── docker-compose.yml      # Container orchestration
├── dummy-data.py          # Test data generator
└── README.md
```

## Technologies Used

- **Backend**: FastAPI 0.115.0, SQLAlchemy 2.0.35, Pydantic 2.9.2
- **Frontend**: React 19.2.5, TypeScript 6.0.2, Recharts 3.8.1
- **Database**: PostgreSQL 16, TimescaleDB
- **Infrastructure**: Docker, Docker Compose
- **Tools**: Adminer, Node-RED, Mosquitto MQTT

## Contributing

1. Fork the repository
2. Create a feature branch
3. Make your changes
4. Test with dummy data
5. Submit a pull request

## License

MIT License - see LICENSE file for details
