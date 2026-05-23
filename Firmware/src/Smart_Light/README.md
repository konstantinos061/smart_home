# 💡 Smart Light Node

> Battery-powered ESP32 smart light node for the DTU IoT Smart Home System developed by GROUP 1 of the 34346 Networking technologies and application development for Internet of Things (IoT) course Spring 2026.  
> Detects motion via a PIR sensor, drives an LED indicator, and communicates battery and motion telemetry via a custom LoRa TDMA protocol.


## Hardware

| Component | Description | Interface |
|---|---|---|
| ESP32-WROOM-32E | Main microcontroller | — |
| RN2483A | LoRa radio module | UART2 (TX: GPIO18, RX: GPIO19, RST: GPIO15) |
| HC-SR501 | PIR motion sensor | GPIO27 |
| LED | Motion indicator | GPIO2 |
| LiPo Battery | Power supply | ADC1 CH7 (GPIO35) via voltage divider |


## Firmware Structure

| File | Responsibility |
|---|---|
| `Smart_Light.cpp` | TDMA protocol loop, beacon detection, deep sleep scheduling, PIR wake handling |
| `Smart_Light.h` | Hardware initialisation, PIR ISR, motion state management, battery reading, payload building |

The firmware follows a **deep-sleep driven architecture** split across two wakes per TDMA frame. The first wake listens for the beacon, then immediately sleeps again until the assigned TDMA slot. The second wake transmits and listens for an ACK, then sleeps until 2.5 s before the next beacon. The RN2483A is put to sleep via `sys sleep` during both sleep periods, and woken via a UART break + `0x55` autobaud trigger on each wake.

A key design feature is the **dual wake source**: in addition to the scheduled timer wake for beacon rounds, the node configures `ext0` to wake on any PIR state transition. This allows the LED to track motion in near real-time even while the CPU is sleeping, without the overhead of staying fully awake between beacons.


## Motion Detection & LED Control

The HC-SR501 requires a **60-second warm-up period** after power-on before its output is stable. To avoid spurious ext0 wakes during this window (which would trap the node in a wake loop and prevent it from ever reaching a beacon), PIR ext0 wakeup is only armed after at least one full sleep cycle has elapsed since POR (`g_pirArmed` flag, stored in RTC memory).

| Wake Source | Behaviour |
|---|---|
| **Timer wake** (scheduled) | Either listens for the beacon (then sleeps until TX slot) or transmits and waits for ACK (then sleeps until next beacon); attaches awake-window ISR to track PIR edges while active |
| **EXT0 wake** (PIR edge) | Updates `g_motionState`, drives the LED HIGH or LOW, increments motion counter, returns to sleep immediately |

The ext0 trigger level is always set to the **opposite** of the current PIR state at sleep entry, so every transition (rise and fall) wakes the node. This avoids a re-read of the bouncing pin.

LED state is held across deep sleep using `rtc_gpio_hold_en()` so the indicator remains accurate while the CPU is off.


## Uplink Payload Format

8 bytes, transmitted as raw hex over LoRa during the assigned TDMA slot:

| Byte | Field | Encoding |
|---|---|---|
| 0 | `NODE_ID` | Device identifier (MSBs = device type `010`) |
| 1 | Command | `0x01` uplink marker |
| 2 | Motion state | `uint8_t`, `1` if any PIR rising edge occurred since the last uplink; cleared on read |
| 3 | Motion count | `uint8_t`, number of PIR rising edges since last uplink (capped at 255); cleared on read |
| 4 | Battery | `uint8_t`, percentage (0–100), sampled at TX time via GPIO35 voltage divider |
| 5–7 | Padding | `0x00` |

The motion latch and count are atomically read and cleared under a `portMUX` critical section so no events are lost between the ISR and the uplink build.


## Battery Measurement

Battery voltage is measured via a resistor divider (R1 = R2 = 100 kΩ) on GPIO35 (ADC1 CH7). A calibration factor of `1.0324` corrects for measured vs. reported discrepancy. The raw ADC reading is converted to a percentage between the defined empty (`3.2 V`) and full (`4.2 V`) voltage thresholds.


## Power Management

The node uses ESP32 deep sleep between TDMA cycles to minimise battery drain:

- **Timer wakeup** (`esp_sleep_enable_timer_wakeup`) — used for both scheduled wakes: 2.5 s before the expected beacon, and at `beacon_time + TRANS_SLOT_MS` to hit the TDMA slot
- **EXT0 wakeup** (`esp_sleep_enable_ext0_wakeup`) — wakes on PIR level transitions to update LED state without requiring a full TDMA cycle; only armed after the first full sleep cycle
- **RN2483A** is placed in `sys sleep` for the same duration as the ESP32 and woken via a UART break + `0x55` autobaud trigger
- The RST pin is held HIGH across deep sleep via `rtc_gpio_hold_en()` so the RN2483A retains its `sys sleep` state
- TDMA state (`g_nextBeaconUs`, `g_lastBeaconUs`, `g_pendingTx`, `g_pirArmed`, etc.) and motion state (`g_motionState`, `g_motionLatched`, `g_motionCount`) are all stored in **RTC memory** to survive deep sleep


## TDMA Behaviour

```
[POR]──[first listen up to 5 min]──[beacon]──[sleep TRANS_SLOT_MS]──[TX 2s]──[ACK 2s]──[sleep ~55s]──[wake 2.5s early]──[beacon]──...
                                       ↑                                 ↑
                                 g_pendingTx=true               timer wake, g_pendingTx=true → skip listen, TX immediately
                    ↑ PIR ext0 wakes possible at any point (LED only, no LoRa)
```

Each TDMA cycle spans **two separate deep sleep wakes**:

1. **Listen wake** — wakes 2.5 s before the projected beacon, listens for up to 5 s. On success, sets `g_pendingTx = true` and schedules the next wake at `beacon_time + TRANS_SLOT_MS`, then sleeps through the slot wait.
2. **TX wake** — sees `g_pendingTx = true`, skips the listen phase entirely, builds and transmits the payload, listens for an ACK, then sleeps until 2.5 s before the next beacon.

On first boot the node listens for up to 5 minutes (`FIRST_LISTEN_MS = 300000 ms`) to catch the initial beacon. If a beacon is missed, the node projects the next expected time from the last known anchor using the 60-second `FRAME_SIZE_MS` cadence and retries.


## Build Flags

Set the following in `platformio.ini`:

```ini
build_flags =
    -D NODE_LIGHT
    -D NODE_ID=0x60
    -D TRANS_SLOT_MS=20000
```

- `NODE_ID`: unique identifier for this node (MSBs `011` = light type)
- `TRANS_SLOT_MS`: offset in ms from beacon at which this node transmits (e.g. `20000` = slot 2)