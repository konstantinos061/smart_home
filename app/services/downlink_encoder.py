import base64
from typing import Any


DOWNLINK_FPORT = 1
SET_TEMPERATURE_COMMAND = 0x01
TEMPERATURE_SCALE = 100


def encode_downlink_payload(
    command_type: str,
    payload: dict[str, Any],
    *,
    confirmed: bool = False,
) -> dict[str, Any]:
    if command_type != 'setTemperature':
        raise ValueError(f'Unsupported command type: {command_type}.')

    sensor_id = payload.get('sensorId')
    value = payload.get('value')

    if not isinstance(sensor_id, int):
        raise ValueError('setTemperature command payload must include integer sensorId.')
    if sensor_id < 0 or sensor_id > 63:
        raise ValueError('setTemperature command requires a thermostat sensorId between 0 and 63.')
    if not isinstance(value, (int, float)) or isinstance(value, bool):
        raise ValueError('setTemperature command payload must include numeric value.')

    raw_value = round(value * TEMPERATURE_SCALE)
    if raw_value < -32768 or raw_value > 32767:
        raise ValueError('setTemperature value is outside the int16 payload range.')

    payload_bytes = bytes([
        sensor_id & 0xFF,
        SET_TEMPERATURE_COMMAND,
    ]) + raw_value.to_bytes(2, byteorder='big', signed=True)

    return {
        'fPort': DOWNLINK_FPORT,
        'confirmed': confirmed,
        'data': base64.b64encode(payload_bytes).decode('ascii'),
        'payloadHex': payload_bytes.hex(),
    }
