import base64
import binascii
from typing import Optional

from app.schemas import MeasurementPayload, UplinkPayload
from app.services.sensor_type_from_id import _sensor_type_from_sensor_id


THERMOSTAT_SCALE = 10.0
SENSOR_RECORD_SIZE = 8



def decode_payload(payload: UplinkPayload) -> list[MeasurementPayload]:
    try:
        raw_payload = base64.b64decode(payload.data, validate=True)
    except (binascii.Error, ValueError) as exc:
        raise ValueError('Invalid base64 payload in ChirpStack data field.') from exc

    if not raw_payload:
        raise ValueError('Raw payload must contain at least one sensor record.')
    if len(raw_payload) % SENSOR_RECORD_SIZE != 0:
        raise ValueError(f'Raw payload length must be a multiple of {SENSOR_RECORD_SIZE} bytes.')

    rssi = _get_rxInfo_field(payload, 'rssi')
    snr = _get_rxInfo_field(payload, 'snr')
    measurements: list[MeasurementPayload] = []

    for offset in range(0, len(raw_payload), SENSOR_RECORD_SIZE):
        record = raw_payload[offset:offset + SENSOR_RECORD_SIZE]
        measurements.extend(_decode_sensor_record(record, rssi, snr))

    return measurements


def _decode_sensor_record(
    record: bytes,
    rssi: Optional[int],
    snr: Optional[float],
) -> list[MeasurementPayload]:
    header = record[0]
    sensor_type_code = (header >> 5) & 0b111
    sensor_type = _sensor_type_from_sensor_id(header)

    if sensor_type == 'thermostat':
        return _decode_thermostat(header, record, rssi, snr)
    if sensor_type == 'motion':
        return _decode_motion(header, record, rssi, snr)
    # if sensor_type == 'door':
    #     return _decode_door(header, raw_payload, rssi, snr, battery_pct)
    # if sensor_type == 'pet':
    #     return _decode_pet(header, raw_payload, rssi, snr, battery_pct)

    raise ValueError(f'Unsupported sensor type code {sensor_type_code}.')


def _decode_thermostat(
    sensor_id: int,
    raw_payload: bytes,
    rssi: Optional[int],
    snr: Optional[float],
) -> list[MeasurementPayload]:
    if len(raw_payload) != SENSOR_RECORD_SIZE:
        raise ValueError('Thermostat payload must be 8 bytes: header + temperature + humidity + set temperature + battery.')

    current_temp = _read_double_scaled(raw_payload[2:4])
    humidity = _read_single_scaled(raw_payload[4:5])
    set_temp = _read_double_scaled(raw_payload[5:7])
    battery_pct = _read_battery_pct(raw_payload[7])

    return [
        MeasurementPayload(
            sensorId=sensor_id,
            sensorType='thermostat',
            key='temperature',
            unit='celsius',
            value=current_temp,
            batteryPct=battery_pct,
            rssi=rssi,
            snr=snr,
        ),
        MeasurementPayload(
            sensorId=sensor_id,
            sensorType='thermostat',
            key='humidity',
            unit='percent',
            value=humidity,
            batteryPct=battery_pct,
            rssi=rssi,
            snr=snr,
        ),
        MeasurementPayload(
            sensorId=sensor_id,
            sensorType='thermostat',
            key='setTemperature',
            unit='celsius',
            value=set_temp,
            batteryPct=battery_pct,
            rssi=rssi,
            snr=snr,
        ),
    ]


def _decode_motion(
    sensor_id: int,
    raw_payload: bytes,
    rssi: Optional[int],
    snr: Optional[float],
) -> list[MeasurementPayload]:
    if len(raw_payload) != SENSOR_RECORD_SIZE:
        raise ValueError('Motion payload must be 8 bytes: header + motion + battery.')

    motionStatus = _read_single_scaled(raw_payload[1:2])
    ledStatus = _read_single_scaled(raw_payload[2:3])
    battery_pct = _read_battery_pct(raw_payload[3])

    return [
        MeasurementPayload(
            sensorId=sensor_id,
            sensorType='motion',
            key='motionStatus',
            unit='state',
            value=motionStatus,
            batteryPct=battery_pct,
            rssi=rssi,
            snr=snr,
        ),
        MeasurementPayload(
            sensorId=sensor_id,
            sensorType='motion',
            key='ledStatus',
            unit='state',
            value=ledStatus,
            batteryPct=battery_pct,
            rssi=rssi,
            snr=snr,
        )
    ]

# def _decode_door(
#     sensor_id: int,
#     raw_payload: bytes,
#     rssi: Optional[int],
#     snr: Optional[float],
#     battery_pct: Optional[int],
# ) -> list[MeasurementPayload]:
#     if len(raw_payload) < 2:
#         raise ValueError('Door payload must be 2 bytes: header + status byte.')

#     return [
#         MeasurementPayload(
#             sensorId=sensor_id,
#             sensorType='door',
#             key='status',
#             unit='state',
#             value=raw_payload[1] != 0,
#             batteryPct=battery_pct,
#             rssi=rssi,
#             snr=snr,
#         )
#     ]


# def _decode_pet(
#     sensor_id: int,
#     raw_payload: bytes,
#     rssi: Optional[int],
#     snr: Optional[float],
#     battery_pct: Optional[int],
# ) -> list[MeasurementPayload]:
#     if len(raw_payload) < 2:
#         raise ValueError('Pet payload must be 2 bytes: header + presence byte.')

#     return [
#         MeasurementPayload(
#             sensorId=sensor_id,
#             sensorType='pet',
#             key='presence',
#             unit='state',
#             value=raw_payload[1] != 0,
#             batteryPct=battery_pct,
#             rssi=rssi,
#             snr=snr,
#         )
#     ]


def _read_double_scaled(value: bytes) -> float:
    return int.from_bytes(value, byteorder='big', signed=False) / THERMOSTAT_SCALE


def _read_single_scaled(value: bytes) -> float:
    return int.from_bytes(value, byteorder='big', signed=False)


def _read_battery_pct(value: int) -> int:
    if value > 100:
        raise ValueError('Battery percentage must be between 0 and 100.')
    return value


def _get_rxInfo_field(payload: UplinkPayload, field: str) -> Optional[int]:
    if not payload.rxInfo:
        return None

    value = payload.rxInfo[0].get(field)
    try:
        return int(value) if value is not None else None
    except (TypeError, ValueError):
        return None

