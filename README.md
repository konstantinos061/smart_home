# Smart Home IoT Platform Starter

This starter package refactors your weather-ingestion prototype into a LoRa/LoRaWAN-ready backend skeleton.

## Included
- FastAPI ingestion and control API
- Concrete SQLAlchemy models for gateways, nodes, telemetry, events, commands, and alerts
- TimescaleDB-backed PostgreSQL setup
- Mosquitto MQTT broker
- Node-RED container with a starter uplink-processing flow

## Main API routes
- `POST /api/v1/gateways`
- `POST /api/v1/nodes`
- `POST /api/v1/uplinks`
- `POST /api/v1/status`
- `GET /api/v1/telemetry`
- `GET /api/v1/nodes/{node_id}/latest`
- `POST /api/v1/nodes/{node_id}/commands`
- `GET /api/v1/health`

## Start
```bash
docker compose up --build
```

## Register a gateway
```bash
curl -X POST http://localhost:8000/api/v1/gateways \
  -H 'Content-Type: application/json' \
  -d '{
    "gatewayId": "gw-home-01",
    "name": "Home Gateway 01",
    "lorawanDevEui": "70B3D57ED0061234"
  }'
```

## Register a node
```bash
curl -X POST http://localhost:8000/api/v1/nodes \
  -H 'Content-Type: application/json' \
  -d '{
    "nodeId": "node-living-01",
    "gatewayId": "gw-home-01",
    "name": "Living Room Node",
    "nodeType": "sensor",
    "batteryType": "liion",
    "sleepProfile": "deep-sleep-5m",
    "reportIntervalSec": 300
  }'
```

## Example uplink
```bash
curl -X POST http://localhost:8000/api/v1/uplinks \
  -H 'Content-Type: application/json' \
  -d '{
    "gatewayId": "gw-home-01",
    "nodeId": "node-living-01",
    "timestamp": "2026-03-12T12:30:00Z",
    "frameCounter": 12,
    "seqNo": 44,
    "rssi": -89,
    "snr": 7.2,
    "battery": {
      "voltage": 3.71,
      "percentage": 64,
      "lowBattery": false
    },
    "measurements": [
      {"sensorKey": "temperature", "type": "analog", "value": 22.9, "unit": "C"},
      {"sensorKey": "door_open", "type": "digital", "value": false}
    ],
    "rawPayloadHex": "0A1204FF"
  }'
```

## ChirpStack integration note
The included Node-RED flow expects ChirpStack uplinks on MQTT topic:
`application/+/device/+/event/up`

It maps `msg.payload.object` into the backend `UplinkPayload` shape. You will usually customize that mapping once your exact gateway decoder format is fixed.
