from datetime import datetime
from typing import Any, Dict, List, Literal, Optional, Union
from pydantic import BaseModel, Field


class BatteryPayload(BaseModel):
    voltage: Optional[float] = None
    percentage: int = Field(ge=0, le=100)
    lowBattery: bool


class MeasurementPayload(BaseModel):
    sensorId: int
    sensorType: str
    key: str
    unit: Optional[str] = None
    value: Union[float, int, bool, str]
    batteryPct: Optional[int] = None
    rssi: Optional[int] = None
    snr: Optional[float] = None


class UplinkPayload(BaseModel):
    nodeId: str
    timestamp: datetime
    data: str
    metadata: Optional[Dict[str, Any]] = None
    rssi: Optional[int] = None
    snr: Optional[float] = None
    deviceInfo: Optional[Dict[str, Any]] = None
    rxInfo: Optional[List[Dict[str, Any]]] = None


class StatusEventPayload(BaseModel):
    nodeId: str
    timestamp: datetime
    eventType: Literal['created', 'active', 'idle']
    battery: Optional[BatteryPayload] = None
    rssi: Optional[int] = None
    snr: Optional[float] = None
    metadata: Optional[Dict[str, Any]] = None


class CommandCreatePayload(BaseModel):
    commandType: str
    payload: Dict[str, Any]
    requestedBy: Optional[str] = None
    expiresAt: Optional[datetime] = None
    confirmed: bool = False
    flushQueue: bool = True


class NodeCreatePayload(BaseModel):
    nodeId: str
    name: Optional[str] = None


class SensorNamePayload(BaseModel):
    name: str


class SensorCreatePayload(BaseModel):
    id: int
    name: Optional[str] = None


class StatusResponse(BaseModel):
    status: str


class CommandResponse(BaseModel):
    status: str
    devEui: str
    deviceQueueUrl: str
    body: Dict[str, Any]


class TelemetryResponse(BaseModel):
    time: datetime
    nodeId: str
    nodeName: Optional[str] = None
    sensorId: int
    sensorName: Optional[str] = None
    sensorType: str
    key: str
    unit: Optional[str] = None
    valueNumeric: Optional[float] = None
    valueText: Optional[str] = None
    valueBool: Optional[bool] = None
    rssi: Optional[int] = None
    snr: Optional[float] = None
    batteryPct: Optional[int] = None

class LatestTelemetryResponse(BaseModel):
    time: datetime
    nodeId: str
    sensorId: int
    sensorName: Optional[str] = None
    sensorType: str
    key: str
    unit: Optional[str] = None
    valueNumeric: Optional[float] = None
    valueText: Optional[str] = None
    valueBool: Optional[bool] = None
    rssi: Optional[int] = None
    snr: Optional[float] = None
    batteryPct: Optional[int] = None


class NodeLatestResponse(BaseModel):
    nodeId: str
    nodeName: Optional[str] = None
    lastSeenAt: Optional[datetime] = None
    isActive: bool
    latestEventType: Optional[str] = None
    batteryVoltage: Optional[float] = None
    batteryPct: Optional[int] = None
    rssi: Optional[int] = None
    snr: Optional[float] = None
    latestTelemetry: List[LatestTelemetryResponse] = []


class SensorsResponse(BaseModel):
    sensorId: int
    nodeId: Optional[str] = None
    sensorName: Optional[str] = None
    sensorType: str
    batteryVoltage: Optional[float] = None
    batteryPct: Optional[int] = None
    isActive: bool
