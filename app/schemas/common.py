from datetime import datetime
from typing import Any, Dict, List, Literal, Optional, Union
from pydantic import BaseModel, Field


class BatteryPayload(BaseModel):
    voltage: float
    percentage: int = Field(ge=0, le=100)
    lowBattery: bool


class MeasurementPayload(BaseModel):
    sensorKey: str
    type: Literal['analog', 'digital']
    value: Union[float, int, bool, str]
    unit: Optional[str] = None


class UplinkPayload(BaseModel):
    gatewayId: str
    nodeId: str
    timestamp: datetime
    frameCounter: int
    seqNo: int
    rssi: Optional[int] = None
    snr: Optional[float] = None
    battery: BatteryPayload
    measurements: List[MeasurementPayload]
    rawPayloadHex: Optional[str] = None
    metadata: Optional[Dict[str, Any]] = None


class StatusEventPayload(BaseModel):
    gatewayId: str
    nodeId: str
    timestamp: datetime
    eventType: str
    severity: Literal['info', 'warning', 'critical']
    message: Optional[str] = None
    battery: Optional[BatteryPayload] = None
    rssi: Optional[int] = None
    snr: Optional[float] = None
    metadata: Optional[Dict[str, Any]] = None


class CommandCreatePayload(BaseModel):
    commandType: str
    payload: Dict[str, Any]
    requestedBy: Optional[str] = None
    expiresAt: Optional[datetime] = None


class NodeCreatePayload(BaseModel):
    nodeId: str
    gatewayId: str
    name: Optional[str] = None
    nodeType: str = 'sensor'
    firmwareVersion: Optional[str] = None
    protocolVersion: str = '1'
    batteryType: Optional[str] = None
    sleepProfile: Optional[str] = None
    reportIntervalSec: Optional[int] = None
    mode: str = 'normal'


class GatewayCreatePayload(BaseModel):
    gatewayId: str
    name: str
    lorawanDevEui: Optional[str] = None
    firmwareVersion: Optional[str] = None
