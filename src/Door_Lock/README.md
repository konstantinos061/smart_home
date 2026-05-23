# 🔒 Smart Lock Node

> Mains-powered ESP32 smart lock node for the DTU IoT Smart Home System developed by GROUP 1 of the 34346 Networking technologies and application development for Internet of Things (IoT) course Spring 2026.  
> Implements two-factor physical access control (RFID + PIN) and supports encrypted remote unlock commands via LoRa downlink.


## Hardware

| Component | Description | Interface |
|---|---|---|
| ESP32-WROOM-32E | Main microcontroller | — |
| RN2483A | LoRa radio module | UART2 (TX: GPIO17, RX: GPIO16) |
| MFRC522 | 13.56 MHz RFID reader | SPI (SS: GPIO5, RST: GPIO22) |
| 4×4 Matrix Keypad | PIN entry | GPIO (Rows: 13,12,14,27 / Cols: 26,25,33,32) |
| LED | Unlock indicator (proxy for door latch) | GPIO2 |


## Firmware Structure

| File | Responsibility |
|---|---|
| `Door_Lock.cpp` | TDMA protocol loop, beacon detection, continuous RX mode |
| `Door_Lock.h` | Hardware initialisation, authentication state machine, payload building, downlink handling, AES-GCM crypto |

Unlike the battery-powered nodes, the smart lock runs **continuously** — it keeps its radio in receive mode at all times except during its assigned TDMA uplink slot, ensuring urgent remote unlock commands can be received at any time.


## Local Authentication Flow

Access control is implemented as a two-factor finite state machine:

| Phase | Action |
|---|---|
| **Phase 1 — RFID Scan** | MFRC522 continuously monitors for tags. Detected UID is verified against the locally stored authorised user list. |
| **Phase 2 — PIN Entry** | On valid card, system transitions to `SYS_WAITING_FOR_PIN`. User enters 4-digit PIN via keypad. 10-second timeout per keypress resets the session if exceeded. |
| **Phase 3 — Actuation** | If PIN matches, LED is activated for 2 seconds to indicate a successful unlock. Three consecutive wrong PINs increment the failed attempts counter and reset the session. |

**Key bindings:**
- `#` — confirm PIN entry
- `*` — cancel and reset session


## Uplink Payload Format

8 bytes, transmitted as raw hex over LoRa during the assigned TDMA slot:

| Byte | Field | Encoding |
|---|---|---|
| 0 | `NODE_ID` | Device identifier (MSBs = device type `011`) |
| 1 | Successful unlocks | `uint8_t`, cumulative count |
| 2 | Failed attempts | `uint8_t`, cumulative count |
| 3–7 | Padding | `0x00` |


## Remote Unlock — Downlink Format

The node accepts encrypted downlink commands from the gateway. All commands use **AES-GCM (128-bit)** with a Pre-Shared Key stored in the ESP32's non-volatile memory, never transmitted over the air.

The downlink packet is **10 bytes**:

| Bytes | Field | Description |
|---|---|---|
| 1–2 | Nonce/IV | Randomised 2-byte initialisation vector, zero-padded to 12 bytes for AES-GCM |
| 3–6 | Ciphertext | Encrypted 2-byte sequence number + 2-byte command |
| 7–10 | Authentication Tag | Truncated 4-byte GCM tag for integrity and authenticity verification |

**Supported commands:**

| Command | Action |
|---|---|
| `0x00FF` | Remote unlock — activates LED for 2 seconds |



## Cryptographic Verification Flow

Upon receiving a downlink, the node performs a three-stage verification before acting:

1. **Authentication tag check** — `mbedtls_gcm_auth_decrypt` verifies the GCM tag. If it does not match, the packet is discarded immediately as tampered or forged.
2. **Replay attack prevention** — the decrypted sequence number must be strictly greater than `lastAcceptedSequence`. Replayed packets with stale sequence numbers are rejected.
3. **Command dispatch** — only a verified `0x00FF` command activates the LED. Unknown commands are logged and ignored.


## TDMA Behaviour

```
[Beacon]──[continuous RX]──[TX slot 2s]──[ACK 2s]──[continuous RX]──[Beacon listen]──[continuous RX]──...
                                 ↑                                           ↑
                           TRANS_SLOT_MS                              re-sync every 60s
```

The lock stays in continuous receive mode throughout the frame, only switching to TX during its assigned slot. After the ACK window it returns to receive mode and also listens for the next beacon to re-synchronise for the following frame.


## Build Flags

Set the following in `platformio.ini`:

```ini
build_flags =
    -D NODE_LOCK
    -D NODE_ID=0x60
    -D TRANS_SLOT_MS=20000
```

- `NODE_ID`: unique identifier for this node (MSBs `011` = lock type)
- `TRANS_SLOT_MS`: offset in ms from beacon at which this node transmits (e.g. `20000` = slot 2)
