from datetime import datetime, timezone
from typing import Optional

from fastapi import APIRouter, Depends, HTTPException, Query
from sqlalchemy import select
from sqlalchemy.orm import Session
from sqlalchemy.util import defaultdict

from app.api.deps import get_db
from app.models import (
    Alert,
    Command,
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
    LatestTelemetryResponse,
    UplinkPayload,
)

router = APIRouter(prefix='/api/v1')


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

    # 3. Fetch the latest telemetry for EVERY node and sensor using DISTINCT ON
    telemetry_rows = db.execute(
        select(
            Telemetry, 
            NodeSensor.name.label("sensor_name"),
            NodeSensor.type.label("sensor_type") 
        )
        .join(NodeSensor, Telemetry.sensor_id == NodeSensor.id)
        .distinct(Telemetry.node_id, Telemetry.sensor_id, Telemetry.measurement_key)
        .order_by(
            Telemetry.node_id,
            Telemetry.sensor_id, 
            Telemetry.measurement_key, 
            Telemetry.time.desc()
        )
    ).all()

    # Group telemetry rows into a dictionary keyed by node_id
    telemetry_by_node = defaultdict(list)
    
    # Create a quick lookup for node names so we can inject them into the telemetry
    node_names = {node.node_id: node.name for node in nodes}

    for t_obj, s_name, s_type in telemetry_rows:
        telemetry_by_node[t_obj.node_id].append({
            'time': t_obj.time,
            'nodeId': t_obj.node_id,
            'nodeName': node_names.get(t_obj.node_id), # <-- Added to satisfy Pydantic schema
            'sensorId': t_obj.sensor_id,
            'sensorName': s_name,     
            'sensorType': s_type,     
            'key': t_obj.measurement_key,
            'unit': t_obj.unit,
            'valueNumeric': t_obj.value_numeric,
            'valueText': t_obj.value_text,
            'valueBool': t_obj.value_bool,
            'rssi': t_obj.rssi,               
            'snr': t_obj.snr,                 
            'batteryPct': t_obj.battery_pct,
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
def ingest_uplink(payload: UplinkPayload, db: Session = Depends(get_db)):
    # 1. Upsert Node
    node = db.get(Node, payload.nodeId)
    if node is None:
        node = Node(node_id=payload.nodeId, name=payload.nodeId)
        db.add(node)

    node.last_seen_at = payload.timestamp

    # 2. Process Measurements
    for measurement in payload.measurements:
        sensor = db.get(NodeSensor, measurement.sensorId)
        if sensor is None:
            sensor = NodeSensor(
                id=measurement.sensorId,
                node_id=payload.nodeId,
                name=f'Sensor {measurement.sensorId}',
                type=measurement.sensorType,  # FIXED: Changed from sensor_type to type
            )
            db.add(sensor)
        else:
            sensor.type = measurement.sensorType
            sensor.node_id = payload.nodeId 

        if measurement.batteryPct is not None:
            sensor.battery_pct = measurement.batteryPct

        # FIXED: Removed the non-existent 'sensor_type' argument
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


@router.post('/nodes/{node_id}/commands', response_model=CommandResponse)
def create_command(node_id: str, payload: CommandCreatePayload, db: Session = Depends(get_db)):
    node = db.get(Node, node_id)
    if not node:
        raise HTTPException(status_code=404, detail='Node not found')

    command = Command(
        node_id=node_id,
        requested_by=payload.requestedBy,
        command_type=payload.commandType,
        payload_json=payload.payload,
        status='queued',
        expires_at=payload.expiresAt,
    )
    db.add(command)
    db.commit()
    db.refresh(command)
    return {'status': 'ok', 'commandId': str(command.command_id)}