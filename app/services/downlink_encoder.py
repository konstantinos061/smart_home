import base64
from typing import Any
from app.services.encrypt_lora_packet import encrypt_lora_packet, SECRET_KEY

DOWNLINK_FPORT = 1
SET_TEMPERATURE_COMMAND = 0x01
TEMPERATURE_SCALE = 100

DOOR_COMMAND_SEQ_NO = 0

def encode_downlink_payload(
    command_type: str,
    payload: dict[str, Any],
    *,
    confirmed: bool = True,
) -> dict[str, Any]:
    global DOOR_COMMAND_SEQ_NO

    if command_type == 'setTemperature':
        sensor_id = payload.get('sensorId')
        value = payload.get('value')

        if not isinstance(sensor_id, int):
            raise ValueError('setTemperature command payload must include integer sensorId.')
        if sensor_id < 32 or sensor_id > 63:
            raise ValueError('setTemperature command requires a thermostat sensorId between 32 and 63.')
        if not isinstance(value, (int, float)) or isinstance(value, bool):
            raise ValueError('setTemperature command payload must include numeric value.')

        raw_value = round(value * TEMPERATURE_SCALE)
        if raw_value < -32768 or raw_value > 32767:
            raise ValueError('setTemperature value is outside the int16 payload range.')

        payload_bytes = bytes([
            sensor_id & 0xFF,
            SET_TEMPERATURE_COMMAND,
        ]) + raw_value.to_bytes(2, byteorder='big', signed=True)

    if command_type == 'openDoor':
        sensor_id = payload.get('sensorId')
        
        if not isinstance(sensor_id, int):
            raise ValueError('openDoor command payload must include integer sensorId.')
        if sensor_id < 64 or sensor_id > 95:
            raise ValueError('openDoor command requires a door sensorId between 64 and 95.')

        payload_bytes = encrypt_lora_packet(
            node_id=sensor_id, 
            sequence=DOOR_COMMAND_SEQ_NO, 
            command=0x00FF, 
            secret_key_hex=SECRET_KEY
            )
        DOOR_COMMAND_SEQ_NO += 1

    if command_type == 'addSensor':
        sensor_id = payload.get('sensorId')
        add_bool = payload.get('addCommand')
        
        if not isinstance(sensor_id, int):
            raise ValueError('addSensor command payload must include integer sensorId.')
        if not isinstance(add_bool, bool):
            raise ValueError('addSensor command payload must include boolean addCommand.')

        add_cmd = 0x00
        if add_bool:
            add_cmd = 0x01
        else:
            add_cmd = 0x00

        payload_bytes = bytes([ 
            sensor_id & 0xFF,
            add_cmd & 0xFF
        ])
        

    return {
        'fPort': DOWNLINK_FPORT,
        'confirmed': confirmed,
        'data': base64.b64encode(payload_bytes).decode('ascii'),
        'payloadHex': payload_bytes.hex(),
    }
