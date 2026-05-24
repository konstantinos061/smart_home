# 🏠 Smart Home IoT Cloud Infrastructure

This folder contains the cloud-side services for the LoRa-based Smart Home IoT
system: a FastAPI backend, a React dashboard, a TimescaleDB database, and the
deployment files needed to run them locally with Docker Compose.

The cloud stack receives decoded LoRaWAN uplinks from ChirpStack through
Node-RED, stores telemetry in PostgreSQL/TimescaleDB, serves the dashboard, and
queues downlink commands back to devices through the ChirpStack REST API.

## Architecture

The cloud data path is split by traffic direction:

- **Uplinks:** devices send telemetry through the LoRaWAN gateway and
  ChirpStack. Node-RED subscribes to ChirpStack MQTT events, normalizes each
  message, and forwards it to FastAPI as an HTTP request.
- **Downlinks:** the dashboard sends commands to FastAPI. FastAPI encodes the
  command payload and directly enqueues a Class C downlink in ChirpStack through
  the ChirpStack REST API.
- **Persistence:** FastAPI writes nodes, sensors, status events, and telemetry
  to TimescaleDB.
- **Visualization:** the React dashboard reads latest and historical telemetry
  from FastAPI and receives real-time update notifications over WebSocket.

<p align="center">
  <img src="DataflowDiagram.png" alt="Cloud data flow" width="70%">
</p>

## Services

| Component | Technology | Responsibility |
|---|---|---|
| Dashboard | React, TypeScript, Vite, Nginx | Browser UI for live monitoring, charts, sensor management, and commands |
| API | FastAPI, Uvicorn, SQLAlchemy, Pydantic | Telemetry ingestion, validation, persistence, REST API, WebSocket updates, downlink encoding |
| Database | PostgreSQL 16, TimescaleDB | Time-series telemetry and relational node/sensor metadata |
| Database UI | Adminer | Direct database inspection during development |
| Middleware | Node-RED | External flow that bridges ChirpStack MQTT uplinks into FastAPI HTTP ingestion |
| Network server | ChirpStack | External LoRaWAN network server used for gateway/device registration and downlink queueing |

The Docker Compose file in this folder starts the dashboard, API, database, and
Adminer. ChirpStack and Node-RED are documented integration points rather than
fully bundled services in this repository.

## Dashboard Features

The dashboard groups sensors by device type and exposes controls appropriate to
each node.

### 🌡️ Thermostats

- Current temperature with 0.1 degree precision
- Humidity
- Current set temperature
- Setpoint controls that send a `setTemperature` downlink through ChirpStack
- Battery, RSSI, status, and last-update indicators
- Historical temperature and humidity charts

### 🔒 Door Locks

- Successful unlock count
- Failed access attempt count
- Battery, RSSI, status, and last-update indicators
- Historical access-attempt chart
- Remote unlock command support through the backend command API

### 💡 Motion Sensors

- Live motion state
- Motion event count
- Last activity timestamp
- Battery, RSSI, status, and last-update indicators
- Historical motion chart

## Quick Start

### Prerequisites

- Docker and Docker Compose
- Python 3.10+ if you want to run the optional data seeder

### Start the Local Cloud Stack

Run Docker Compose from this `Cloud/` directory:

```bash
cd Cloud
docker compose up --build
```

The first build creates the backend image, builds the React dashboard, and starts
TimescaleDB. FastAPI initializes the database schema on startup.

### Access Points

| Service | URL |
|---|---|
| Dashboard | http://localhost |
| API documentation | http://localhost:8000/docs |
| API health check | http://localhost:8000/api/v1/health |
| Adminer | http://localhost:8081 |
| PostgreSQL | `localhost:5433` |

Adminer database connection:

| Field | Value |
|---|---|
| System | PostgreSQL |
| Server | `postgres` when using Adminer, `localhost:5433` from the host |
| Username | `postgres` |
| Password | `postgres` |
| Database | `postgres` |

### Seed Test Data

With the API running, seed sample telemetry from the `Cloud/` directory:

```bash
python3 dummy-data.py
```

This is only for local dashboard testing. Real telemetry should come from the
Node-RED to FastAPI uplink flow.

## ChirpStack and Node-RED Integration

### ChirpStack

ChirpStack is used as the LoRaWAN network server. In the project deployment it
was hosted externally on Azure and used to register:

- the LoRaWAN gateway
- the gateway device EUI used by the local smart-home bridge
- application/device profiles required for Class C downlinks

The repository does not include the full ChirpStack Docker Compose setup because
the official ChirpStack project already provides a reproducible starter stack:

https://www.chirpstack.io/docs/getting-started/docker.html

FastAPI queues downlinks through:

```text
POST {CHIRPSTACK_BASE_URL}/api/devices/{devEui}/queue
```

The backend reads these optional environment variables:

| Variable | Purpose | Default in code |
|---|---|---|
| `CHIRPSTACK_BASE_URL` | Base URL for the ChirpStack API | `http://10.0.0.1:8090` |
| `CHIRPSTACK_API_TOKEN` | Bearer token used for queue API calls | Development token in `app/services/chirpstack.py` |

For a real deployment, pass these values through environment variables or a
secret manager instead of relying on source-code defaults.

### Node-RED

Node-RED owns the long-running ChirpStack MQTT subscription for uplinks. The
exported flow is included at:

```text
nodered/flows.json
```

The flow should be imported into the Node-RED instance connected to the
ChirpStack MQTT broker. Its output is an HTTP POST to the FastAPI uplink
endpoint:

```text
POST /api/v1/uplink
```

Downlinks intentionally bypass Node-RED. This keeps command latency low and
keeps command encoding in the backend where the dashboard API already runs.

## API Reference

Interactive OpenAPI documentation is available at:

```text
http://localhost:8000/docs
```

Core endpoints:

| Method | Path | Purpose |
|---|---|---|
| `GET` | `/api/v1/health` | Check API and database connectivity |
| `POST` | `/api/v1/node` | Register or update a node |
| `POST` | `/api/v1/node/status` | Ingest node status events |
| `GET` | `/api/v1/node/latest` | Return all nodes with latest telemetry and status |
| `POST` | `/api/v1/uplink` | Ingest a ChirpStack-style base64 payload |
| `GET` | `/api/v1/telemetry` | Query historical telemetry |
| `GET` | `/api/v1/sensors` | List registered sensors |
| `POST` | `/api/v1/sensors` | Register a sensor and notify the gateway |
| `POST` | `/api/v1/sensors/{sensor_id}/name` | Rename a sensor |
| `POST` | `/api/v1/sensors/{sensor_id}/delete` | Delete a sensor and its telemetry |
| `POST` | `/api/v1/nodes/{node_id}/commands` | Encode and enqueue a downlink command |
| `WS` | `/api/v1/ws` | Dashboard real-time update channel |

### Example: Read Latest Node State

```bash
curl http://localhost:8000/api/v1/node/latest
```

### Example: Ingest an Uplink

The backend expects the same shape as a ChirpStack uplink event after Node-RED
normalization. The `data` field is a base64-encoded binary sensor record.

```bash
curl -X POST http://localhost:8000/api/v1/uplink \
  -H 'Content-Type: application/json' \
  -d '{
    "nodeId": "0004a30b01101ede",
    "timestamp": "2026-05-24T12:00:00.000Z",
    "data": "IAAA4TEA4VU=",
    "metadata": {
      "source": "manual-test"
    },
    "rxInfo": [
      {
        "rssi": -78,
        "snr": 7.5
      }
    ]
  }'
```

### Example: Send a Thermostat Setpoint

```bash
curl -X POST http://localhost:8000/api/v1/nodes/0004a30b01101ede/commands \
  -H 'Content-Type: application/json' \
  -d '{
    "commandType": "setTemperature",
    "confirmed": true,
    "flushQueue": false,
    "payload": {
      "sensorId": 32,
      "value": 22.5
    }
  }'
```

Supported command types in the backend:

| Command | Payload | Notes |
|---|---|---|
| `setTemperature` | `{ "sensorId": 32-63, "value": 22.5 }` | Encodes a thermostat setpoint downlink |
| `openDoor` | `{ "sensorId": 64-95 }` | Encodes an encrypted door unlock command |
| `addSensor` | `{ "sensorId": 32-95, "addCommand": true }` | Sends a gateway sensor registration command |

## Local Development

The Docker Compose setup is the simplest way to run all local services. You can
also run backend or frontend directly while using the Compose database.

### Backend

```bash
cd Cloud
python3 -m pip install -r requirements.txt
uvicorn app.main:app --reload
```

When running the backend outside Docker, configure the database URL for the host
port:

```bash
export DATABASE_URL=postgresql://postgres:postgres@localhost:5433/postgres
```

### Frontend

```bash
cd Cloud/frontend
npm install
npm run dev
```

The Vite dev server runs on:

```text
http://localhost:5173
```

By default, the frontend uses relative `/api/...` paths. For direct development
against a separately hosted backend, set:

```bash
export VITE_API_URL=http://localhost:8000
```

## Project Structure

```text
Cloud/
├── app/                    # FastAPI backend
│   ├── api/                # Routes and dependencies
│   ├── models/             # SQLAlchemy models
│   ├── schemas/            # Pydantic request/response schemas
│   └── services/           # Payload decoding, downlink encoding, ChirpStack client
├── frontend/               # React + TypeScript dashboard
│   ├── public/
│   └── src/
│       ├── components/
│       ├── pages/
│       └── services/
├── nodered/
│   └── flows.json          # Exported Node-RED uplink bridge flow
├── DataflowDiagram.png
├── docker-compose.yml      # Local API/frontend/database/Adminer orchestration
├── Dockerfile              # FastAPI image
├── dummy-data.py           # Optional local telemetry seeder
├── requirements.txt
└── README.md
```

## Technology Stack

| Layer | Stack |
|---|---|
| Backend | FastAPI, Uvicorn, SQLAlchemy, Pydantic, Requests |
| Frontend | React, TypeScript, Vite, Recharts, Axios |
| Database | PostgreSQL 16, TimescaleDB |
| Integration | ChirpStack, Node-RED, MQTT |
| Infrastructure | Docker, Docker Compose, Nginx, Adminer |
