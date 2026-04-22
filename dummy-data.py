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

def send_uplink(node_id: str, minutes_ago: int, measurements: list):
    """Sends a telemetry uplink to the API."""
    url = f"{API_BASE_URL}/uplink"
    
    payload = {
        "nodeId": node_id,
        "timestamp": get_iso_timestamp(minutes_ago),
        "measurements": measurements,
        # "rawPayloadHex": "0A0B0C0D", # Dummy payload
        "metadata": {"simulated": True}
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
        {"id": "123", "name": "Test Node"},
        {"id": "node-beta-002", "name": "Front Gate Controller"}
    ]
    
    for node in nodes:
        create_node(node["id"], node["name"])

    time.sleep(1) # Brief pause before hammering the telemetry endpoint
    print("\n📊 Generating Telemetry Data...\n")

    # 2. Generate historical telemetry (from 60 minutes ago up to now)
    # This ensures your frontend line charts actually have a timeline to display!
    for minutes_ago in range(60, -1, -15):
        
        # --- Node Alpha (Thermostat Simulation) ---
        alpha_measurements = [
            {
                "sensorId": 1,
                "sensorType": "thermostat",
                "key": "temperature",
                "unit": "celsius",
                "value": round(random.uniform(20.5, 23.5), 2),
                "batteryPct": 85, # Healthy battery,
                "rssi": random.randint(-120, -50),  # Realistic LoRaWAN RSSI values
                "snr": round(random.uniform(-10.0, 10.0), 1),
            },
            {
                "sensorId": 1,
                "sensorType": "thermostat",
                "key": "humidity",
                "unit": "percent",
                "value": round(random.uniform(40.0, 55.0), 1),
                "batteryPct": 85,
                "rssi": random.randint(-120, -50),  # Realistic LoRaWAN RSSI values
                "snr": round(random.uniform(-10.0, 10.0), 1),
            }
        ]
        send_uplink(nodes[0]["id"], minutes_ago, alpha_measurements)

        # --- Node Beta (Door & Pet Sensor Simulation) ---
        # Let's simulate a dying battery to trigger your Alert logic!
        beta_battery = max(0, 25 - int((60 - minutes_ago) / 5)) 

        beta_measurements = [
            {
                "sensorId": 2,
                "sensorType": "door",
                "key": "status",
                "unit": "state",
                "value": random.choice([True, False]), # Door open/closed
                "batteryPct": beta_battery,
                "rssi": random.randint(-120, -50),  # Realistic LoRaWAN RSSI values
                "snr": round(random.uniform(-10.0, 10.0), 1),
            },
            {
                "sensorId": 3,
                "sensorType": "pet",
                "key": "presence",
                "unit": "state",
                "value": "detected" if random.random() > 0.99 else "clear",
                "batteryPct": 99, # Pet collar battery is fine
                "rssi": random.randint(-120, -50),  # Realistic LoRaWAN RSSI values
                "snr": round(random.uniform(-10.0, 10.0), 1),
            }
        ]
        send_uplink(nodes[0]["id"], minutes_ago, beta_measurements)

    print("\n✅ Data seeding complete. Check your frontend dashboard!")

if __name__ == "__main__":
    main()