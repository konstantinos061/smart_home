from datetime import datetime, timezone
from typing import Optional

from fastapi import APIRouter, Depends, HTTPException, Query, WebSocket, WebSocketDisconnect
from sqlalchemy import func, select
from sqlalchemy.orm import Session
from sqlalchemy.util import defaultdict

from app.api.deps import get_db
from app.models import (
    Node,
    NodeSensor,
    NodeStatusEvent,
    Telemetry,
)
from app.schemas import (
    CommandCreatePayload,
    CommandResponse,
    NodeCreatePayload,
    NodeLatestResponse,
    SensorNamePayload,
    StatusEventPayload,
    StatusResponse,
    TelemetryResponse,
    SensorsResponse,
    SensorCreatePayload,
    UplinkPayload,
)
from app.services.chirpstack import enqueue_device_queue_item
from app.services.downlink_encoder import encode_downlink_payload
from app.services.payload_decoder import decode_payload
from app.services.sensor_type_from_id import _sensor_type_from_sensor_id

router = APIRouter(prefix='/api/v1')

class ConnectionManager:
    def __init__(self):
        self.active_connections: set[WebSocket] = set()

    async def connect(self, websocket: WebSocket):
        await websocket.accept()
        self.active_connections.add(websocket)

    def disconnect(self, websocket: WebSocket):
        self.active_connections.discard(websocket)

    async def broadcast(self, message: dict):
        disconnected = []
        for connection in list(self.active_connections):
            try:
                await connection.send_json(message)
            except Exception:
                disconnected.append(connection)

        for connection in disconnected:
            self.disconnect(connection)


connection_manager = ConnectionManager()


@router.websocket('/ws')
async def dashboard_websocket(websocket: WebSocket):
    await connection_manager.connect(websocket)
    try:
        while True:
            await websocket.receive_text()
    except WebSocketDisconnect:
        connection_manager.disconnect(websocket)


@router.get('/health', response_model=StatusResponse)
def health(db: Session = Depends(get_db)):
    db.execute(select(Node).limit(1))
    return {'status': 'ok'}


@router.post('/node', response_model=StatusResponse)
def create_node(payload: NodeCreatePayload, db: Session = Depends(get_db)):
    node = Node(
        node_id=payload.nodeId,
        name=payload.name,
    )
    event=NodeStatusEvent(
        time=datetime.now(timezone.utc).isoformat(timespec='milliseconds').replace('+00:00', 'Z'),
        node_id=payload.nodeId,
        event_type='created',
    )
    db.merge(node)
    db.add(event)
    db.commit()
    return {'status': 'ok'}


@router.post('/node/status', response_model=StatusResponse)
def ingest_status(payload: StatusEventPayload, db: Session = Depends(get_db)):
    event = NodeStatusEvent(
        time=payload.timestamp,
        node_id=payload.nodeId,
        event_type=payload.eventType,
        battery_voltage=payload.battery.voltage if payload.battery else None,
        battery_pct=payload.battery.percentage if payload.battery else None,
        rssi=payload.rssi,
        snr=payload.snr,
        metadata_json=payload.metadata,
    )
    db.merge(event)
    db.commit()
    return {'status': 'ok'}


@router.get('/node/latest', response_model=list[NodeLatestResponse])
def get_all_nodes_latest(db: Session = Depends(get_db)):
    
    # 1. Fetch all active nodes
    nodes = db.execute(select(Node)).scalars().all()
    
    # 2. Fetch the SINGLE latest event for EVERY node using DISTINCT ON
    latest_events = db.execute(
        select(NodeStatusEvent)
        .distinct(NodeStatusEvent.node_id)
        .order_by(NodeStatusEvent.node_id, NodeStatusEvent.time.desc())
    ).scalars().all()

    # Map events into a dictionary keyed by node_id for instant O(1) lookup
    events_by_node = {event.node_id: event for event in latest_events}

    # 3. Fetch the latest telemetry for EVERY node, sensor, and measurement key using a window function
    latest_telemetry_subq = (
        select(
            Telemetry.node_id.label('node_id'),
            Telemetry.sensor_id.label('sensor_id'),
            Telemetry.measurement_key.label('measurement_key'),
            Telemetry.time.label('time'),
            Telemetry.unit.label('unit'),
            Telemetry.value_numeric.label('value_numeric'),
            Telemetry.value_text.label('value_text'),
            Telemetry.value_bool.label('value_bool'),
            Telemetry.rssi.label('rssi'),
            Telemetry.snr.label('snr'),
            Telemetry.battery_pct.label('battery_pct'),
            func.row_number().over(
                partition_by=(Telemetry.node_id, Telemetry.sensor_id, Telemetry.measurement_key),
                order_by=Telemetry.time.desc()
            ).label('row_number')
        )
        .subquery()
    )

    telemetry_rows = db.execute(
        select(
            latest_telemetry_subq,
            NodeSensor.name.label('sensor_name'),
            NodeSensor.type.label('sensor_type')
        )
        .join(NodeSensor, latest_telemetry_subq.c.sensor_id == NodeSensor.id)
        .where(latest_telemetry_subq.c.row_number == 1)
    ).all()

    # Group telemetry rows into a dictionary keyed by node_id
    telemetry_by_node = defaultdict(list)
    
    # Create a quick lookup for node names so we can inject them into the telemetry
    node_names = {node.node_id: node.name for node in nodes}

    for row in telemetry_rows:
        # Unpack: subquery columns (13 total) + sensor_name + sensor_type
        node_id, sensor_id, measurement_key, time, unit, value_numeric, value_text, value_bool, rssi, snr, battery_pct, row_number, s_name, s_type = row
        
        telemetry_by_node[node_id].append({
            'time': time,
            'nodeId': node_id,
            'nodeName': node_names.get(node_id),
            'sensorId': sensor_id,
            'sensorName': s_name,     
            'sensorType': s_type,     
            'key': measurement_key,
            'unit': unit,
            'valueNumeric': value_numeric,
            'valueText': value_text,
            'valueBool': value_bool,
            'rssi': rssi,               
            'snr': snr,                 
            'batteryPct': battery_pct,
        })

    # 4. Assemble the final response
    response_payload = []
    for node in nodes:
        # Safely grab the event for this node (defaults to None if missing)
        event = events_by_node.get(node.node_id)
        
        response_payload.append({
            'nodeId': node.node_id,
            'nodeName': node.name,
            'lastSeenAt': node.last_seen_at,
            'isActive': node.is_active,

            'latestEventType': event.event_type if event else None,
            'batteryVoltage': event.battery_voltage if event else None,
            'batteryPct': event.battery_pct if event else None,
            'rssi': event.rssi if event else None,
            'snr': event.snr if event else None,

            # Safely grab the telemetry list for this node (defaults to empty list)
            'latestTelemetry': telemetry_by_node.get(node.node_id, [])
        })

    return response_payload


@router.post('/uplink', response_model=StatusResponse)
async def ingest_uplink(payload: UplinkPayload, db: Session = Depends(get_db)):
    try:
        measurements = decode_payload(payload)
    except ValueError as exc:
        raise HTTPException(status_code=400, detail=str(exc)) from exc

    # 1. Upsert Node
    node = db.get(Node, payload.nodeId)
    if node is None:
        node = Node(node_id=payload.nodeId, name=payload.nodeId)
        db.add(node)

    node.last_seen_at = payload.timestamp

    # 2. Process Measurements
    sensors_by_id: dict[int, NodeSensor] = {}
    for measurement in measurements:
        sensor = sensors_by_id.get(measurement.sensorId)
        if sensor is None:
            sensor = db.get(NodeSensor, measurement.sensorId)
        if sensor is None:
            sensor = NodeSensor(
                id=measurement.sensorId,
                node_id=payload.nodeId,
                name=f'Sensor #{measurement.sensorId}/{measurement.sensorType}',
                type=measurement.sensorType,  # FIXED: Changed from sensor_type to type
            )
            db.add(sensor)
            sensors_by_id[measurement.sensorId] = sensor
        else:
            sensor.type = measurement.sensorType
            sensor.node_id = payload.nodeId
            sensors_by_id[measurement.sensorId] = sensor

        if measurement.batteryPct is not None:
            sensor.battery_pct = measurement.batteryPct

        telemetry = Telemetry(
            time=payload.timestamp,
            node_id=payload.nodeId,
            sensor_id=measurement.sensorId,
            measurement_key=measurement.key,
            unit=measurement.unit,
            value_numeric=float(measurement.value) if isinstance(measurement.value, (int, float)) and not isinstance(measurement.value, bool) else None,
            value_text=measurement.value if isinstance(measurement.value, str) else None,
            value_bool=measurement.value if isinstance(measurement.value, bool) else None,
            rssi=measurement.rssi,
            snr=measurement.snr,
            battery_pct=measurement.batteryPct,
        )
        db.merge(telemetry)

    db.commit()
    await connection_manager.broadcast({
        'type': 'uplink',
        'nodeId': payload.nodeId,
        'timestamp': payload.timestamp.isoformat(),
    })
    return {'status': 'ok'}


@router.get('/telemetry', response_model=list[TelemetryResponse])
def get_telemetry(
    nodeId: Optional[str] = Query(None),
    sensorId: Optional[int] = Query(None),
    start: Optional[datetime] = Query(None),
    end: Optional[datetime] = Query(None),
    limit: int = Query(200, ge=1, le=5000),
    db: Session = Depends(get_db),
):
    # 1. Select the entity and distinctly labeled columns
    stmt = select(
        Telemetry,
        NodeSensor.name.label("sensor_name"), # <-- FIXED: Distinct label
        Node.name.label("node_name"),         # <-- FIXED: Distinct label
        NodeSensor.type.label("sensor_type")  # <-- FIXED: Distinct label
    )
    
    # 2. Chain the INNER JOINs
    stmt = stmt.join(NodeSensor, Telemetry.sensor_id == NodeSensor.id)
    stmt = stmt.join(Node, Telemetry.node_id == Node.node_id)

    # 3. Apply the dynamic filters
    if nodeId:
        stmt = stmt.where(Telemetry.node_id == nodeId)
    if sensorId is not None:
        stmt = stmt.where(Telemetry.sensor_id == sensorId)
    if start:
        stmt = stmt.where(Telemetry.time >= start)
    if end:
        stmt = stmt.where(Telemetry.time <= end)
        
    stmt = stmt.order_by(Telemetry.time.desc()).limit(limit)

    # 4. Execute and fetch rows
    rows = db.execute(stmt).all()
    
    # 5. Cleanly unpack the tuples in the comprehension
    return [
        {
            'time': t_obj.time,             # Injected from the Telemetry object
            'nodeId': t_obj.node_id,
            'nodeName': n_name,             # Injected from the tuple unpack
            'sensorId': t_obj.sensor_id,
            'sensorName': s_name,           # Injected from the tuple unpack
            'sensorType': s_type,           # Injected from the tuple unpack
            'key': t_obj.measurement_key,
            'unit': t_obj.unit,
            'valueNumeric': t_obj.value_numeric,
            'valueText': t_obj.value_text,
            'valueBool': t_obj.value_bool,
            'rssi': t_obj.rssi,
            'snr': t_obj.snr,
            'batteryPct': t_obj.battery_pct,
        }
        # FIXED: We explicitly unpack the 4 parts of the Row tuple here!
        for t_obj, s_name, n_name, s_type in rows 
    ]


@router.post('/sensors/{sensor_id}/name', response_model=StatusResponse)
def rename_sensor(sensor_id: int, payload: SensorNamePayload, db: Session = Depends(get_db)):
    sensor = db.get(NodeSensor, sensor_id)
    if not sensor:
        raise HTTPException(status_code=404, detail='Sensor not found')

    sensor.name = payload.name
    db.commit()
    return {'status': 'ok'}


@router.get('/sensors', response_model=list[SensorsResponse])
def get_sensors(
    nodeId: Optional[str] = Query(None),
    db: Session = Depends(get_db),
):
    stmt = select(
        NodeSensor.id.label("sensor_id"),
        NodeSensor.node_id.label("node_id"),
        NodeSensor.name.label("sensor_name"),
        NodeSensor.type.label("sensor_type"),
        NodeSensor.battery_pct.label("battery_pct"),
        NodeSensor.is_active.label("is_active")
    )

    if nodeId:
        stmt = stmt.where(NodeSensor.node_id == nodeId)

    rows = db.execute(stmt).all()

    return [
        {
            'sensorId': s_id,
            'nodeId': n_id,
            'sensorName': s_name,
            'sensorType': s_type,
            'batteryPct': b_pct,
            'isActive': is_active
        }
        for s_id, n_id, s_name, s_type, b_pct, is_active in rows
    ]


@router.post('/sensors', response_model=StatusResponse)
def create_sensor(payload: SensorCreatePayload, db: Session = Depends(get_db)):
    # The sensor type lives in the 3 leftmost bits of the header byte.
    sensor_type = _sensor_type_from_sensor_id(payload.id)
    if sensor_type == 'unknown':
        raise HTTPException(status_code=400, detail='Unsupported sensor ID prefix.')

    sensor = NodeSensor(
        id=payload.id,
        name=payload.name if payload.name else f'Sensor #{payload.id}',
        type=sensor_type
    )
    db.add(sensor)
    db.commit()
    return {'status': 'ok'}


@router.post('/sensors/{sensor_id}/delete', response_model=StatusResponse)
def delete_sensor(sensor_id: int, db: Session = Depends(get_db)):
    sensor = db.get(NodeSensor, sensor_id)
    if not sensor:
        raise HTTPException(status_code=404, detail='Sensor not found')
    db.delete(sensor)
    # delete all telemetry related to that sensor
    db.query(Telemetry).filter(Telemetry.sensor_id == sensor_id).delete()
    db.commit()
    return {'status': 'ok'}

@router.post("/nodes/{node_id}/commands", response_model=CommandResponse)
def create_command(node_id: str, payload: CommandCreatePayload,):
    try:
        downlink = encode_downlink_payload(
            payload.commandType,
            payload.payload,
            confirmed=payload.confirmed,
        )
    except ValueError as exc:
        raise HTTPException(status_code=400, detail=str(exc)) from exc

    chirpstack_result = enqueue_device_queue_item(
        node_id,
        {
            "confirmed": downlink["confirmed"],
            "data": downlink["data"],
            "fCntDown": payload.fCntDown,
            "fPort": downlink["fPort"],
            "id": payload.id,
            "isEncrypted": payload.isEncrypted,
            "isPending": payload.isPending,
        },
        flush_queue=payload.flushQueue,
    )

    return {
        "body": chirpstack_result["body"],
        "deviceQueueUrl": f"/api/devices/{node_id}/queue",
        "devEui": node_id,
        "status": "ok",
        "chirpstackResponse": chirpstack_result["chirpstackResponse"],
    }
