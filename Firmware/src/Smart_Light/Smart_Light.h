/**
 * Architecture:
 *   - Chip deep sleeps between beacon rounds. Wake sources:
 *       * Timer wake  => run a beacon listen / TX / ACK round.
 *       * Ext0 wake on PIN_MOTION => set LED, latch motion, sleep again.
 *   - Motion state is kept in RTC slow memory across deep sleep.
 *   - LED state is held across deep sleep via rtc_gpio_hold_en().
 *
 * Uplink payload (8 bytes, zero-padded):
 *   [0] NODE_ID
 *   [1] 0x01           uplink marker
 *   [2] motion-since-last-tx (1 = at least one PIR rising edge since the
 *                             previous uplink; cleared on read)
 *   [3] motion count   (how many motions over the window period going to 255; cleared on read)
 *   [4] battery %      (0..100, sampled at TX time via GPIO35 divider)
 *   [5..7] 0x00        padding
 * Author: Gabriel Crawford
 */

#pragma once
#include <Arduino.h>
#include <string.h>
#include "esp_attr.h"
#include "driver/rtc_io.h"

// -----------------------------------------------------------------------------
// Pin definitions
// -----------------------------------------------------------------------------
#define PIN_MOTION  27
#define PIN_LED     2
#define PIN_BATTERY 35   // ADC1_CH7, input only divider: Vbatt -- R1 -- GPIO35 -- R2 -- GND

static constexpr float BATT_R1            = 100000.0f;
static constexpr float BATT_R2            = 100000.0f;
static constexpr float BATT_REF_V         = 3.3f;
static constexpr float BATT_CAL           = 1.0324f;  // 4.14 V meter / 4.01 V reported
static constexpr float BATT_V_EMPTY       = 3.2f;
static constexpr float BATT_V_FULL        = 4.2f;

// -----------------------------------------------------------------------------
// Motion state: RTC retained so it survives deep sleep.
//   g_motionState   : last sampled PIR level
//   g_motionLatched : bit set on any PIR HIGH event since last uplink
//   g_motionCount   : edge count since last uplink up to 255
// All three are reset to 0/false on power on (initializer applied at POR only).
// -----------------------------------------------------------------------------
RTC_DATA_ATTR static volatile bool    g_motionState   = false;
RTC_DATA_ATTR static volatile bool    g_motionLatched = false;
RTC_DATA_ATTR static volatile uint8_t g_motionCount   = 0;

static portMUX_TYPE g_motionMux = portMUX_INITIALIZER_UNLOCKED;

// -----------------------------------------------------------------------------
// Battery read divider on GPIO35, return charge as 0..100 %.
// -----------------------------------------------------------------------------
static uint8_t readBatteryPercent() {
    int   raw       = analogRead(PIN_BATTERY);
    float pinV      = (raw / 4095.0f) * BATT_REF_V;
    float battV     = pinV * ((BATT_R1 + BATT_R2) / BATT_R2) * BATT_CAL;
    float pct       = (battV - BATT_V_EMPTY) * 100.0f /
                      (BATT_V_FULL - BATT_V_EMPTY);
    if (pct < 0.0f)   pct = 0.0f;
    if (pct > 100.0f) pct = 100.0f;
    return (uint8_t)pct;
}

// -----------------------------------------------------------------------------
// Service a PIR wake from deep sleep which runs in setup() context
//
// Takes the new motion state as an explicit argument rather than reading
// the pin again. The wake fired because the configured ext0 level was reached, but
// PIR chatter can bounce the line back within microseconds of a fresh
// digitalRead often sampling the rebound which leaves the LED stuck
// -----------------------------------------------------------------------------
static void lightHandlePIRWake(bool motion) {
    g_motionState = motion;
    digitalWrite(PIN_LED, motion ? HIGH : LOW);
    if (motion) {
        g_motionLatched = true;
        if (g_motionCount < 255) g_motionCount++;
    }
}

// -----------------------------------------------------------------------------
// Awake window ISR only attached during the beacon/TX/ACK phase so the LED
// can track PIR while the chip is up. Detached again before deep sleep
// -----------------------------------------------------------------------------
static void IRAM_ATTR pirEdgeISR() {
    bool motion = digitalRead(PIN_MOTION) == HIGH;
    g_motionState = motion;
    digitalWrite(PIN_LED, motion ? HIGH : LOW);
    if (motion) {
        portENTER_CRITICAL_ISR(&g_motionMux);
        g_motionLatched = true;
        if (g_motionCount < 255) g_motionCount++;
        portEXIT_CRITICAL_ISR(&g_motionMux);
    }
}

// -----------------------------------------------------------------------------
// Public API called from Nodes.cpp
// -----------------------------------------------------------------------------
void nodeSetup() {
    // Release any deep sleep hold from a previous wake before driving the LED.
    rtc_gpio_hold_dis((gpio_num_t)PIN_LED);

    pinMode(PIN_MOTION, INPUT);
    pinMode(PIN_LED, OUTPUT);
    analogReadResolution(12);

    Serial.println("[LIGHT] Hardware init OK");
}

void nodeBuildPayload(uint8_t nodeId, uint8_t* buf, uint8_t* len) {
    // Atomically read & clear the PIR latch bit and counter so events between
    // uplinks aren't lost. Uses _ISR variant since the awake window PIR
    // ISR may fire concurrently.
    portENTER_CRITICAL(&g_motionMux);
    bool    latched = g_motionLatched;
    uint8_t count   = g_motionCount;
    g_motionLatched = false;
    g_motionCount   = 0;
    portEXIT_CRITICAL(&g_motionMux);

    memset(buf, 0, 8);
    buf[0] = nodeId;
    buf[1] = 0x01;
    buf[2] = (uint8_t)latched;
    buf[3] = count;
    buf[4] = readBatteryPercent();
    *len   = 8;
}
