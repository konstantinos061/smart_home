import base64
import requests
import random
import time
from datetime import datetime, timezone, timedelta

# Update this if your FastAPI server is running on a different port
API_BASE_URL = "http://localhost:8000/api/v1"

def get_iso_timestamp(minutes_ago=0):
    """Generates a perfectly formatted ISO 8601 UTC timestamp."""
    dt = datetime.now(timezone.utc) - timedelta(minutes=minutes_ago)
    return dt.isoformat(timespec='milliseconds').replace('+00:00', 'Z')

def create_node(node_id: str, name: str):
    """Registers a new node via the API."""
    url = f"{API_BASE_URL}/node"
    payload = {"nodeId": node_id, "name": name}
    
    response = requests.post(url, json=payload)
    if response.status_code == 200:
        print(f"✅ Created Node: {name} ({node_id})")
    else:
        print(f"❌ Failed to create node {node_id}: {response.text}")

def encode_thermostat_payload(
    sensor_id: int,
    temperature: float,
    humidity: float,
    set_temperature: float,
    battery_pct: int,
) -> str:
    """Encodes one thermostat sensor record into the API's base64 uplink format."""
    if sensor_id < 0 or sensor_id > 63:
        raise ValueError("Thermostat sensor IDs must be between 0 and 63.")
    if battery_pct < 0 or battery_pct > 100:
        raise ValueError("Battery percentage must be between 0 and 100.")

    payload_bytes = bytes([sensor_id & 0x3F])
    payload_bytes += round(temperature * 100).to_bytes(2, byteorder="big", signed=True)
    payload_bytes += round(humidity * 100).to_bytes(2, byteorder="big", signed=False)
    payload_bytes += round(set_temperature * 100).to_bytes(2, byteorder="big", signed=True)
    payload_bytes += bytes([battery_pct])

    return base64.b64encode(payload_bytes).decode("ascii")


def send_uplink(node_id: str, minutes_ago: int, data: str, rssi: int, snr: float):
    """Sends a telemetry uplink to the API."""
    url = f"{API_BASE_URL}/uplink"
    
    payload = {
        "nodeId": node_id,
        "timestamp": get_iso_timestamp(minutes_ago),
        "data": data,
        "metadata": {"simulated": True},
        "rxInfo": [{"rssi": rssi, "snr": snr}],
    }

    response = requests.post(url, json=payload)
    if response.status_code == 200:
        print(f"📡 Sent uplink for {node_id} (-{minutes_ago} mins ago)")
    else:
        print(f"❌ Failed to send uplink for {node_id}: {response.text}")

def main():
    print("🚀 Starting API Data Seeder...\n")

    # 1. Register our nodes
    nodes = [
        {"id": "0004a30b0106480e", "name": "Test Node"},
        # {"id": "node-beta-002", "name": "Front Gate Controller"}
    ]
    
    for node in nodes:
        create_node(node["id"], node["name"])

    time.sleep(1) # Brief pause before hammering the telemetry endpoint
    print("\n📊 Generating Telemetry Data...\n")

    # 2. Generate historical telemetry (from 60 minutes ago up to now)
    # This ensures your frontend line charts actually have a timeline to display!
    for minutes_ago in range(60, -1, -15):
        
        # --- Node Alpha (Thermostat Simulation) ---
        # Each uplink contains one 8-byte thermostat sensor record.
        for sensor_id in [0, 63]:
            rssi = random.randint(-120, -50)
            snr = round(random.uniform(-10.0, 10.0), 1)
            data = encode_thermostat_payload(
                sensor_id=sensor_id,
                temperature=round(random.uniform(20.5, 23.5), 2),
                humidity=round(random.uniform(40.0, 55.0), 1),
                set_temperature=22.0,
                battery_pct=85,
            )
            send_uplink(nodes[0]["id"], minutes_ago, data, rssi, snr)

    print("\n✅ Data seeding complete. Check your frontend dashboard!")

if __name__ == "__main__":
    main()
