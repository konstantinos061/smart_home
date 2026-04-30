SENSOR_TYPE_FROM_HEADER = {
    0b001: 'thermostat',
    0b010: 'door',
    0b011: 'motion',
    0b100: 'pet',
}


def _sensor_type_from_sensor_id(sensor_id: int) -> str:
    return SENSOR_TYPE_FROM_HEADER.get((sensor_id >> 5) & 0b111, 'unknown')
