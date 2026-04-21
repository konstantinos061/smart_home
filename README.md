# Smart Home IoT Platform Starter

## Included
- FastAPI ingestion and control API
- Concrete SQLAlchemy models for gateways, nodes, telemetry, events, commands, and alerts
- TimescaleDB-backed PostgreSQL setup
- Mosquitto MQTT broker
- Node-RED container with a starter uplink-processing flow
- **React web frontend** for monitoring and control

## Services
- **API**: FastAPI backend on port 8000
- **Frontend**: React web UI on port 80
- **Database**: PostgreSQL with TimescaleDB on port 5433
- **MQTT Broker**: Mosquitto on port 1883
- **Node-RED**: Automation platform on port 1880
- **Adminer**: Database admin interface on port 8080

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

## Access the Application
- **Web Frontend**: http://localhost/
- **API Documentation**: http://localhost:8000/docs
- **Database Admin**: http://localhost:8080/
- **Node-RED**: http://localhost:1880/

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

## Simulate a ChirpStack uplink end-to-end
```bash
mosquitto_pub -h localhost -p 1883 \
  -t application/demo-app/device/70B3D57ED0061234/event/up \
  -m '{
    "time":"2026-03-12T18:00:00Z",
    "deviceInfo":{"devEui":"node-living-01"},
    "rxInfo":[{"rssi":-87,"snr":7.1}],
    "data":"0A1204FF",
    "object":{
      "gatewayId":"gw-home-01",
      "nodeId":"node-living-01",
      "frameCounter":15,
      "seqNo":99,
      "batteryVoltage":3.72,
      "batteryPct":61,
      "lowBattery":false,
      "measurements":[
        {"sensorKey":"temperature","type":"analog","value":23.4,"unit":"C"},
        {"sensorKey":"door_open","type":"digital","value":false}
      ]
    }
  }'
```

## Test low-battery flow through MQTT
```bash
mosquitto_pub -h localhost -p 1883 \
  -t application/demo-app/device/70B3D57ED0061234/event/up \
  -m '{
    "time":"2026-03-12T18:05:00Z",
    "deviceInfo":{"devEui":"node-living-01"},
    "rxInfo":[{"rssi":-92,"snr":5.8}],
    "data":"0A1204AA",
    "object":{
      "gatewayId":"gw-home-01",
      "nodeId":"node-living-01",
      "frameCounter":16,
      "seqNo":100,
      "batteryVoltage":3.31,
      "batteryPct":18,
      "lowBattery":true,
      "measurements":[
        {"sensorKey":"temperature","type":"analog","value":22.8,"unit":"C"}
      ]
    }
  }'
```

## Visualize Database
Visit: http://localhost:8080/

## ChirpStack integration note
The included Node-RED flow expects ChirpStack uplinks on MQTT topic:
`application/+/device/+/event/up`

It maps `msg.payload.object` into the backend `UplinkPayload` shape. You will usually customize that mapping once your exact gateway decoder format is fixed.
