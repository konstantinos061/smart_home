/**
 * light_node.h — Lighting Node
 * Sensors: PIR motion detector, LED output
 *
 * Implements the three functions called by Nodes.cpp:
 *   nodeSetup()          — initialise GPIO
 *   nodeBuildPayload()   — read motion sensor, build uplink packet
 *   nodeHandleDownlink() — react to gateway LED commands
 *
 * Payload format (4 bytes):
 *   [0] NODE_ID
 *   [1] 0x01  (uplink marker)
 *   [2] motion detected (0 = no, 1 = yes)
 *   [3] LED state       (0 = off, 1 = on)
 */

#pragma once
#include <Arduino.h>

// -----------------------------------------------------------------------------
// Pin definitions — adjust to your wiring
// -----------------------------------------------------------------------------
#define PIN_MOTION  13
#define PIN_LED     12

// -----------------------------------------------------------------------------
// Downlink commands
// -----------------------------------------------------------------------------
#define CMD_LED_ON   0x01
#define CMD_LED_OFF  0x02

// -----------------------------------------------------------------------------
// Internal state
// -----------------------------------------------------------------------------
static bool _ledState = false;

// -----------------------------------------------------------------------------
// Public API
// -----------------------------------------------------------------------------
void nodeSetup() {
    pinMode(PIN_MOTION, INPUT);
    pinMode(PIN_LED, OUTPUT);
    digitalWrite(PIN_LED, LOW);
    Serial.println("[LIGHT] Hardware init OK");
}

void nodeBuildPayload(uint8_t nodeId, uint8_t* buf, uint8_t* len) {
    bool motion = digitalRead(PIN_MOTION) == HIGH;

    Serial.printf("[LIGHT] Motion: %s  LED: %s\n",
                  motion ? "YES" : "no",
                  _ledState ? "ON" : "off");

    buf[0] = nodeId;
    buf[1] = 0x01;
    buf[2] = (uint8_t)motion;
    buf[3] = (uint8_t)_ledState;
    *len   = 4;
}

void AAAAAAAnodeHandleDownlink(uint8_t cmd, uint8_t* data, uint8_t dataLen) {
    if (cmd == CMD_LED_ON) {
        _ledState = true;
        digitalWrite(PIN_LED, HIGH);
        Serial.println("[LIGHT] LED ON via downlink");
    } else if (cmd == CMD_LED_OFF) {
        _ledState = false;
        digitalWrite(PIN_LED, LOW);
        Serial.println("[LIGHT] LED OFF via downlink");
    }
}
