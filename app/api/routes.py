from datetime import datetime
from typing import Optional

from fastapi import APIRouter, Depends, HTTPException, Query
from sqlalchemy import select
from sqlalchemy.orm import Session

from app.api.deps import get_db
from app.models import (
    Alert,
    Command,
    Gateway,
    Node,
    NodeStatusEvent,
    Telemetry,
)
from app.schemas import (
    CommandCreatePayload,
    GatewayCreatePayload,
    NodeCreatePayload,
    StatusEventPayload,
    UplinkPayload,
)

router = APIRouter(prefix='/api/v1')


@router.post('/gateways')
def create_gateway(payload: GatewayCreatePayload, db: Session = Depends(get_db)):
    gateway = Gateway(
        gateway_id=payload.gatewayId,
        name=payload.name,
        lorawan_dev_eui=payload.lorawanDevEui,
        firmware_version=payload.firmwareVersion,
        status='online',
    )
    db.merge(gateway)
    db.commit()
    return {'status': 'ok', 'gatewayId': payload.gatewayId}


@router.post('/nodes')
def create_node(payload: NodeCreatePayload, db: Session = Depends(get_db)):
    gateway = db.get(Gateway, payload.gatewayId)
    if not gateway:
        raise HTTPException(status_code=404, detail='Gateway not found')

    node = Node(
        node_id=payload.nodeId,
        gateway_id=payload.gatewayId,
        name=payload.name,
        node_type=payload.nodeType,
        firmware_version=payload.firmwareVersion,
        protocol_version=payload.protocolVersion,
        battery_type=payload.batteryType,
        sleep_profile=payload.sleepProfile,
        report_interval_sec=payload.reportIntervalSec,
        mode=payload.mode,
    )
    db.merge(node)
    db.commit()
    return {'status': 'ok', 'nodeId': payload.nodeId}


@router.post('/uplinks')
def ingest_uplink(payload: UplinkPayload, db: Session = Depends(get_db)):
    gateway = db.get(Gateway, payload.gatewayId)
    if not gateway:
        gateway = Gateway(gateway_id=payload.gatewayId, name=payload.gatewayId, status='online')
        db.add(gateway)

    node = db.get(Node, payload.nodeId)
    if not node:
        node = Node(node_id=payload.nodeId, gateway_id=payload.gatewayId, name=payload.nodeId)
        db.add(node)

    gateway.last_seen_at = payload.timestamp
    gateway.status = 'online'
    node.last_seen_at = payload.timestamp

    inserted = 0
    for measurement in payload.measurements:
        telemetry = Telemetry(
            time=payload.timestamp,
            node_id=payload.nodeId,
            gateway_id=payload.gatewayId,
            sensor_key=measurement.sensorKey,
            value_numeric=float(measurement.value) if isinstance(measurement.value, (int, float)) and not isinstance(measurement.value, bool) else None,
            value_text=measurement.value if isinstance(measurement.value, str) else None,
            value_bool=measurement.value if isinstance(measurement.value, bool) else None,
            unit=measurement.unit,
            rssi=payload.rssi,
            snr=payload.snr,
            battery_voltage=payload.battery.voltage,
            battery_pct=payload.battery.percentage,
            frame_counter=payload.frameCounter,
            seq_no=payload.seqNo,
            raw_payload_hex=payload.rawPayloadHex,
        )
        db.merge(telemetry)
        inserted += 1

    if payload.battery.lowBattery or payload.battery.percentage < 20:
        event = NodeStatusEvent(
            time=payload.timestamp,
            node_id=payload.nodeId,
            gateway_id=payload.gatewayId,
            event_type='low_battery',
            severity='warning',
            message='Battery below threshold',
            battery_voltage=payload.battery.voltage,
            battery_pct=payload.battery.percentage,
            rssi=payload.rssi,
            snr=payload.snr,
            metadata_json=payload.metadata,
        )
        db.merge(event)

        alert = Alert(
            node_id=payload.nodeId,
            gateway_id=payload.gatewayId,
            alert_type='low_battery',
            severity='warning',
            title=f'Low battery on {payload.nodeId}',
            description=f'Battery at {payload.battery.percentage}% ({payload.battery.voltage} V)',
        )
        db.add(alert)

    db.commit()
    return {'status': 'ok', 'telemetryRowsInserted': inserted}


@router.post('/status')
def ingest_status(payload: StatusEventPayload, db: Session = Depends(get_db)):
    event = NodeStatusEvent(
        time=payload.timestamp,
        node_id=payload.nodeId,
        gateway_id=payload.gatewayId,
        event_type=payload.eventType,
        severity=payload.severity,
        message=payload.message,
        battery_voltage=payload.battery.voltage if payload.battery else None,
        battery_pct=payload.battery.percentage if payload.battery else None,
        rssi=payload.rssi,
        snr=payload.snr,
        metadata_json=payload.metadata,
    )
    db.merge(event)
    db.commit()
    return {'status': 'ok'}


@router.get('/telemetry')
def get_telemetry(
    nodeId: Optional[str] = Query(None),
    gatewayId: Optional[str] = Query(None),
    sensorKey: Optional[str] = Query(None),
    start: Optional[datetime] = Query(None),
    end: Optional[datetime] = Query(None),
    limit: int = Query(200, ge=1, le=5000),
    db: Session = Depends(get_db),
):
    stmt = select(Telemetry)
    if nodeId:
        stmt = stmt.where(Telemetry.node_id == nodeId)
    if gatewayId:
        stmt = stmt.where(Telemetry.gateway_id == gatewayId)
    if sensorKey:
        stmt = stmt.where(Telemetry.sensor_key == sensorKey)
    if start:
        stmt = stmt.where(Telemetry.time >= start)
    if end:
        stmt = stmt.where(Telemetry.time <= end)
    stmt = stmt.order_by(Telemetry.time.desc()).limit(limit)

    rows = db.execute(stmt).scalars().all()
    return [
        {
            'time': r.time,
            'nodeId': r.node_id,
            'gatewayId': r.gateway_id,
            'sensorKey': r.sensor_key,
            'valueNumeric': r.value_numeric,
            'valueText': r.value_text,
            'valueBool': r.value_bool,
            'unit': r.unit,
            'rssi': r.rssi,
            'snr': r.snr,
            'batteryVoltage': r.battery_voltage,
            'batteryPct': r.battery_pct,
            'frameCounter': r.frame_counter,
            'seqNo': r.seq_no,
        }
        for r in rows
    ]


@router.get('/nodes/{node_id}/latest')
def get_node_latest(node_id: str, db: Session = Depends(get_db)):
    node = db.get(Node, node_id)
    if not node:
        raise HTTPException(status_code=404, detail='Node not found')

    telemetry = db.execute(
        select(Telemetry)
        .where(Telemetry.node_id == node_id)
        .order_by(Telemetry.time.desc())
        .limit(20)
    ).scalars().all()

    return {
        'nodeId': node.node_id,
        'gatewayId': node.gateway_id,
        'mode': node.mode,
        'lastSeenAt': node.last_seen_at,
        'latestTelemetry': [
            {
                'time': t.time,
                'sensorKey': t.sensor_key,
                'valueNumeric': t.value_numeric,
                'valueText': t.value_text,
                'valueBool': t.value_bool,
                'batteryPct': t.battery_pct,
            }
            for t in telemetry
        ],
    }


@router.post('/nodes/{node_id}/commands')
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


@router.get('/health')
def health(db: Session = Depends(get_db)):
    db.execute(select(Gateway).limit(1))
    return {'status': 'ok'}
