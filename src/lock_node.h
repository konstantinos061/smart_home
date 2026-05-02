/**
 * lock_node.h — Smart Lock Node
 * Sensors: RFID reader (SPI), keypad (GPIO)
 * Actuator: relay for door latch
 *
 * Implements the three functions called by Nodes.cpp:
 *   nodeSetup()          — initialise RFID, keypad, relay
 *   nodeBuildPayload()   — report lock state and last event
 *   nodeHandleDownlink() — lock / unlock via gateway command
 *
 * Payload format (4 bytes):
 *   [0] NODE_ID
 *   [1] 0x01         (uplink marker)
 *   [2] lock state   (0x00 = locked, 0x01 = unlocked)
 *   [3] last event   (0x00 = none, 0x01 = RFID, 0x02 = keypad, 0x03 = remote)
 *
 * TODO: add the MFRC522 or PN532 library to lib_deps and wire
 *       up the RFID reader; add keypad matrix scanning below.
 */

#pragma once
#include <Arduino.h>

// -----------------------------------------------------------------------------
// Pin definitions — adjust to your wiring
// -----------------------------------------------------------------------------
#define PIN_RELAY     14   // controls door latch (HIGH = open)
#define PIN_RFID_SS    5   // RFID SPI chip-select
#define PIN_RFID_RST   4   // RFID reset

// -----------------------------------------------------------------------------
// Downlink commands
// -----------------------------------------------------------------------------
#define CMD_LOCK    0x01
#define CMD_UNLOCK  0x02

// -----------------------------------------------------------------------------
// Internal state
// -----------------------------------------------------------------------------
static bool    _lockOpen   = false;   // false = locked
static uint8_t _lastEvent  = 0x00;

// -----------------------------------------------------------------------------
// Public API
// -----------------------------------------------------------------------------
void nodeSetup() {
    pinMode(PIN_RELAY, OUTPUT);
    digitalWrite(PIN_RELAY, LOW);   // start locked

    // TODO: initialise RFID reader here, e.g.:
    //   SPI.begin();
    //   rfid.PCD_Init(PIN_RFID_SS, PIN_RFID_RST);

    // TODO: initialise keypad rows/columns here

    Serial.println("[LOCK] Hardware init OK (locked)");
}

void nodeBuildPayload(uint8_t nodeId, uint8_t* buf, uint8_t* len) {
    // TODO: poll RFID reader — if new card detected, validate UID
    //   and set _lockOpen / _lastEvent accordingly.
    // TODO: poll keypad — if valid PIN entered, unlock.

    Serial.printf("[LOCK] State: %s  Last event: 0x%02X\n",
                  _lockOpen ? "UNLOCKED" : "LOCKED", _lastEvent);

    buf[0] = nodeId;
    buf[1] = 0x01;
    buf[2] = (uint8_t)_lockOpen;
    buf[3] = _lastEvent;
    *len   = 4;

    _lastEvent = 0x00;   // clear after reporting
}

void AAAAAAAAAAnodeHandleDownlink(uint8_t cmd, uint8_t* data, uint8_t dataLen) {
    if (cmd == CMD_UNLOCK) {
        _lockOpen  = true;
        _lastEvent = 0x03;
        digitalWrite(PIN_RELAY, HIGH);
        Serial.println("[LOCK] Unlocked via downlink");
    } else if (cmd == CMD_LOCK) {
        _lockOpen  = false;
        _lastEvent = 0x03;
        digitalWrite(PIN_RELAY, LOW);
        Serial.println("[LOCK] Locked via downlink");
    }
}
