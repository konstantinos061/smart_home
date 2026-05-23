# 🌐 Smart Home IoT — Cloud Infrastructure

A self-hosted IoT platform for monitoring and controlling the DTU Smart Home system, featuring a real-time React dashboard, FastAPI backend, and TimescaleDB for time-series data storage.

## Architecture

---ADD a photo here of the cloud infrastrutue from the lorawan gateway !!!!

### Backend (FastAPI)
- REST API for node registration, telemetry ingestion, and downlink commands
- Pydantic models for data validation
- SQLAlchemy ORM with TimescaleDB for efficient time-series queries
- Automatic API documentation at `/docs`

### Middleware (Node-RED)
- Subscribes to the ChirpStack MQTT broker
- Forwards incoming uplink payloads to FastAPI via REST POST requests
- Downlink commands bypass Node-RED entirely — FastAPI calls ChirpStack's REST API directly for low-latency Class C delivery

### Frontend (React + TypeScript)
- Real-time dashboard with a card per node, grouped by device type
- Interactive set-temperature controls for the thermostat
- Historical data charts via Recharts
- Battery level and signal strength (RSSI) indicators on every card

### Database (PostgreSQL + TimescaleDB)
- Time-series optimised storage for all sensor telemetry
- Relational tables for node registry and sensor metadata
- Adminer web interface for direct database inspection


## Dashboard

The dashboard groups nodes into three sections based on device type:

### 🌡️ Thermostats
Each thermostat card shows:
- **Current temperature** — large display, 0.1°C resolution
- **Set temperature** — adjustable with − / + buttons and a Send control; precision 0.1°C; sends a downlink setpoint command to the node via ChirpStack
- **Humidity** — current relative humidity (%)
- **Status** — connection health indicator
- **Last update** — timestamp of most recent uplink
- **Battery** — percentage with visual bar
- **Signal** — RSSI quality indicator
- **History** — opens an interactive dual-axis chart of temperature and humidity over time

### 🔒 Doors (Smart Lock)
Each door card shows:
- **HOMIES** — cumulative count of successful unlock events
- **IMPOSTORS** — cumulative count of failed access attempts (wrong PIN or unrecognised card)
- **Last updated** — timestamp of most recent uplink
- **Battery** — percentage with visual bar
- **Signal** — RSSI quality indicator
- **History** — opens a chart of unlock and failed-attempt counts over time

### 💡 Motion Sensors (Smart Light)
Each motion sensor card shows:
- **Motion state** — live indicator; highlights red with a walking-figure icon when motion is currently detected
- **Counter** — cumulative motion event count since last uplink
- **Last activity** — timestamp of the most recent PIR trigger
- **Battery** — percentage with visual bar
- **Signal** — RSSI quality indicator
- **History** — opens a chart of motion events over time


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

| Service | URL |
|---|---|
| Dashboard | http://localhost |
| API Documentation | http://localhost:8000/docs |
| Database Admin (Adminer) | http://localhost:8081 |
| Node-RED | http://10.0.0.1:1880 |
| ChirpStack | http://10.0.0.1:8080 |

### Add Test Data
```bash
python dummy-data.py
```


## API Reference

### Core Endpoints
- `GET /api/v1/node/latest` — get all nodes with their latest telemetry
- `POST /api/v1/uplink` — ingest sensor data from Node-RED
- `GET /api/v1/telemetry` — query historical telemetry
- `POST /api/v1/node` — register a new node

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
cd Cloud
python3 -m pip install -r requirements.txt
uvicorn app.main:app --reload
```

### Frontend Setup
```bash
cd frontend
npm install
npm run dev
```

### Database Access
- Host: `localhost:5433`
- User: `postgres`
- Password: `postgres`
- Database: `postgres`


## Project Structure

```
Cloud/
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
├── dummy-data.py           # Test data generator
└── README.md
```


## Technologies

| Layer | Stack |
|---|---|
| Backend | FastAPI 0.115.0, SQLAlchemy 2.0.35, Pydantic 2.9.2 |
| Frontend | React 19.2.5, TypeScript 6.0.2, Recharts 3.8.1 |
| Database | PostgreSQL 16, TimescaleDB |
| Middleware | Node-RED, Mosquitto MQTT |
| Infrastructure | Docker, Docker Compose, Adminer |
