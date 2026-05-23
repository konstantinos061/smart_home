# 🌡️ Smart Thermostat Node

> Battery-powered ESP32 thermostat node for the LoRa Based Smart Home System developed by GROUP 1 of the 34346 Networking technologies and application development for Internet of Things (IoT) course Spring 2026.  
> Measures temperature and humidity, supports local setpoint adjustment, and communicates via a custom LoRa TDMA protocol.



## Hardware

| Component | Description | Interface |
|---|---|---|
| ESP32-WROOM-32E | Main microcontroller | — |
| RN2483A | LoRa radio module | UART2 (TX: GPIO17, RX: GPIO16, RST: GPIO25) |
| DHT11 | Temperature & humidity sensor | GPIO5 |
| LCD 16x2 (I²C) | Local display (address 0x27) | I²C (SDA: GPIO21, SCL: GPIO22) |
| Rotary Encoder | Setpoint adjustment | CLK: GPIO34, DT: GPIO35, SW: GPIO32 |
| LiPo Battery | Power supply | ADC1 CH5 (GPIO33) via voltage divider |


## Firmware Structure

| File | Responsibility |
|---|---|
| `Thermostat.cpp` | TDMA protocol loop, beacon detection, deep sleep scheduling |
| `thermostat.h` | Hardware initialisation, sensor reading, payload building, downlink handling, sleep API |

The firmware follows a **deep-sleep driven architecture** — the ESP32 boots, completes one full TDMA cycle (beacon listen → TX → ACK), then enters deep sleep until just before the next beacon. The RN2483A is also put to sleep via `sys sleep` during this period.


## Uplink Payload Format

8 bytes, transmitted as raw hex over LoRa:

| Byte | Field | Encoding |
|---|---|---|
| 0 | `NODE_ID` | Device identifier (MSBs = device type `001`) |
| 1 | Command | `0x00` = uplink |
| 2–3 | Temperature | `int16_t`, value × 10 (0.1°C resolution) |
| 4 | Humidity | `uint8_t`, integer % |
| 5–6 | Setpoint | `int16_t`, value × 10 (0.1°C resolution) |
| 7 | Battery | `uint8_t`, percentage (0–100) |


## Downlink — Setpoint Command

Command byte: `CMD_SET_SETPOINT = 0x01`, piggybacked onto the ACK frame.

| Byte | Field |
|---|---|
| 0 | Integer part of new setpoint (°C) |
| 1 | Fractional part in hundredths (e.g. `50` = 0.50°C) |

The setpoint is constrained to **15–30°C** and saved to NVS so it survives power cycles.


## Power Management

The node uses ESP32 deep sleep between TDMA cycles to minimise battery drain:

- **Timer wakeup** (`esp_sleep_enable_timer_wakeup`) — wakes the node just before the next beacon window
- **EXT0 wakeup** (`esp_sleep_enable_ext0_wakeup`) — wakes on encoder button press to allow local setpoint adjustment at any time
- **RN2483A** is placed in `sys sleep` for the same duration as the ESP32
- Sync state (`g_tdmaSynced`) and sleep timing (`g_sleepStartTick`, `g_sleepMs`) are stored in **RTC memory** to survive deep sleep
- Setpoint is stored in **NVS** on first boot and cached in RTC memory for subsequent wakeups, avoiding costly flash reads every cycle


## Local UI

When the encoder button is pressed (either as a wakeup cause or during the active window), the LCD backlight turns on and the user can adjust the setpoint using the rotary encoder. After **10 seconds of inactivity**, the new setpoint is saved to NVS and the UI closes.


## Build Flags

Set the following in `platformio.ini`:

```ini
build_flags =
    -D NODE_THERMOSTAT
    -D NODE_ID=0x20
    -D TRANS_SLOT_MS=10000
```

- `NODE_ID`: unique identifier for this node (MSBs `001` = thermostat type)
- `TRANS_SLOT_MS`: offset in ms from beacon at which this node transmits (e.g. `10000` = slot 1)


## TDMA Timing

```
[wake 2.5s early]──[Beacon]──[awake: build payload]──[TX 2s]──[ACK 2s]──[sleep ~55s]──[wake 2.5s early]──...
                                                         ↑
                                                    transmit here
```

The node wakes 2.5s before the expected beacon, listens for up to 5s to catch it, then stays awake building the sensor payload until its assigned TX slot. After the ACK window it sleeps until 2.5s before the next beacon.