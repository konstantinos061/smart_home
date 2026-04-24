from sqlalchemy import (
    Boolean,
    Column,
    DateTime,
    Double,
    Enum,
    ForeignKey,
    Integer,
    JSON,
    String,
    Text,
    func,
)
from sqlalchemy.dialects.postgresql import UUID
from sqlalchemy.schema import PrimaryKeyConstraint
import uuid

from .base import Base



class Node(Base):
    __tablename__ = 'nodes'

    node_id = Column(String(64), primary_key=True)
    name = Column(String(128), nullable=True)
    last_seen_at = Column(DateTime(timezone=True), nullable=True)
    is_active = Column(Boolean, nullable=False, server_default='true')
    created_at = Column(DateTime(timezone=True), nullable=False, server_default=func.now())


class NodeSensor(Base):
    __tablename__ = 'node_sensors'

    id = Column(Integer, primary_key=True)
    node_id = Column(String(64), ForeignKey('nodes.node_id'), nullable=True)
    name = Column(String(128), nullable=False)
    type = Column(
        Enum('thermostat', 'door', 'pet', 'unknown', name='sensor_type_enum'),
        nullable=False,
    )
    battery_pct = Column(Integer, nullable=True)
    is_active = Column(Boolean, nullable=False, server_default='true')


class Telemetry(Base):
    __tablename__ = 'telemetry'
    __table_args__ = (
        PrimaryKeyConstraint('node_id', 'sensor_id', 'measurement_key', 'time', name='pk_telemetry'),
    )

    time = Column(DateTime(timezone=True), nullable=False)
    node_id = Column(String(64), ForeignKey('nodes.node_id'), nullable=False)
    sensor_id = Column(Integer, ForeignKey('node_sensors.id'), nullable=False)
    measurement_key = Column(String(128), nullable=False)
    unit = Column(String(64), nullable=True)
    value_numeric = Column(Double, nullable=True)
    value_text = Column(Text, nullable=True)
    value_bool = Column(Boolean, nullable=True)
    rssi = Column(Integer, nullable=True)
    snr = Column(Double, nullable=True)
    battery_pct = Column(Integer, nullable=True)


class NodeStatusEvent(Base):
    __tablename__ = 'node_status_events'
    __table_args__ = (
        PrimaryKeyConstraint('node_id', 'event_type', 'time', name='pk_node_status_events'),
    )

    time = Column(DateTime(timezone=True), nullable=False)
    node_id = Column(String(64), ForeignKey('nodes.node_id'), nullable=False)
    event_type = Column(
        Enum('idle', 'active', 'created', name='status_event_type_enum'),
        nullable=False,
    )
    battery_voltage = Column(Double, nullable=True)
    battery_pct = Column(Integer, nullable=True)
    rssi = Column(Integer, nullable=True)
    snr = Column(Double, nullable=True)
    metadata_json = Column(JSON, nullable=True)


class Command(Base):
    __tablename__ = 'commands'

    command_id = Column(UUID(as_uuid=True), primary_key=True, default=uuid.uuid4)
    node_id = Column(String(64), ForeignKey('nodes.node_id'), nullable=False)
    requested_by = Column(String(128), nullable=True)
    command_type = Column(String(64), nullable=False)
    payload_json = Column(JSON, nullable=False)
    status = Column(String(32), nullable=False, server_default='queued')
    created_at = Column(DateTime(timezone=True), nullable=False, server_default=func.now())
    queued_at = Column(DateTime(timezone=True), nullable=True)
    sent_at = Column(DateTime(timezone=True), nullable=True)
    acked_at = Column(DateTime(timezone=True), nullable=True)
    expires_at = Column(DateTime(timezone=True), nullable=True)


class CommandResult(Base):
    __tablename__ = 'command_results'

    id = Column(Integer, primary_key=True, autoincrement=True)
    command_id = Column(UUID(as_uuid=True), ForeignKey('commands.command_id'), nullable=False)
    node_id = Column(String(64), ForeignKey('nodes.node_id'), nullable=False)
    result_code = Column(String(64), nullable=True)
    result_message = Column(Text, nullable=True)
    time = Column(DateTime(timezone=True), nullable=False, server_default=func.now())
    metadata_json = Column(JSON, nullable=True)


class Alert(Base):
    __tablename__ = 'alerts'

    alert_id = Column(UUID(as_uuid=True), primary_key=True, default=uuid.uuid4)
    node_id = Column(String(64), ForeignKey('nodes.node_id'), nullable=True)
    alert_type = Column(String(64), nullable=False)
    severity = Column(String(16), nullable=False)
    title = Column(String(200), nullable=False)
    description = Column(Text, nullable=True)
    status = Column(String(32), nullable=False, server_default='open')
    created_at = Column(DateTime(timezone=True), nullable=False, server_default=func.now())
    resolved_at = Column(DateTime(timezone=True), nullable=True)
