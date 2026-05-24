/**
 * =============================================================================
 * thermostat.h — Thermostat Node Sensors & Payload
 * =============================================================================
 * Responsible: Filippo, Gabriel
 *
 * Exposes three functions called by the main protocol firmware:
 *   nodeSetup()            — initialise all thermostat hardware
 *   nodeBuildPayload()     — read sensors and return packet bytes
 *   nodeHandleDownlink()   — react to gateway commands (setpoint change)
 * =============================================================================
 */

#pragma once

#include <Arduino.h>
#include <Wire.h>
#include <Preferences.h>
#include <DHT.h>
#include <LiquidCrystal_I2C.h>
#include <driver/rtc_io.h>
// -----------------------------------------------------------------------------
// Pin definitions
// -----------------------------------------------------------------------------
#define PIN_DHT           5
#define PIN_EXT0          32
#define PIN_LCD_SDA       21
#define PIN_LCD_SCL       22
#define PIN_BATTERY_ADC   33
#define PIN_POT_ADC       36 

// -----------------------------------------------------------------------------
// Configuration
// -----------------------------------------------------------------------------
#define DHT_STARTUP_MS  2500
#define BEACON_FRAME_MS 59500 
#define SETPOINT_MIN    15
#define SETPOINT_MAX    30
#define UI_TIMEOUT_MS   10000
#define BATTERY_MAX_MV  4200.0f
#define BATTERY_MIN_MV  3000.0f
#define BATTERY_DIV_R1  100000.0f
#define BATTERY_DIV_R2  100000.0f

// -----------------------------------------------------------------------------
// Downlink commands for thermostat
// -----------------------------------------------------------------------------
#define CMD_SET_SETPOINT  0x01  // data[0] = new setpoint in °C

// -----------------------------------------------------------------------------
// Objects — defined here, used across the node
// -----------------------------------------------------------------------------
static DHT               _dht(PIN_DHT, DHT11);
static LiquidCrystal_I2C _lcd(0x27, 16, 2);

static Preferences     _prefs;

extern volatile bool g_uiActive;
extern volatile bool g_sleepPending;
extern SemaphoreHandle_t g_dhtMutex;

// RTC memory — survives deep sleep
extern RTC_DATA_ATTR float    thermoSetPoint;
extern RTC_DATA_ATTR bool     thermoFirstBoot;
extern RTC_DATA_ATTR bool     g_uiWasActive;

RTC_DATA_ATTR float   thermoSetPoint  = 20.0f;
RTC_DATA_ATTR bool    thermoFirstBoot = true;
RTC_DATA_ATTR bool    g_uiWasActive   = false;

// -----------------------------------------------------------------------------
// Internal helpers
// -----------------------------------------------------------------------------
static float _readTemperature() {
    float t = _dht.readTemperature();
    return isnan(t) ? 0.0f : t;
}

static float _readHumidity() {
    float h = _dht.readHumidity();
    return isnan(h) ? 0.0f : h;
}

static int16_t encodeTemp10(float t) {
    return (int16_t)(t * 10.0f + 0.5f);
}

static int _readBatteryPercent() {
#ifdef NO_BATTERY
    // For testing on mains power, return a fixed value instead of reading ADC
    return 100;
#else
    // Take multiple readings and average to reduce noise
    long sum = 0;
    for (int i = 0; i < 16; i++) { sum += analogRead(PIN_BATTERY_ADC); delay(2); }
    float adcV  = (sum / 16.0f) * (3.3f / 4095.0f);
    float batV  = adcV * ((BATTERY_DIV_R1 + BATTERY_DIV_R2) / BATTERY_DIV_R2);
    float batMv = batV * 1000.0f;
    int pct = (int)((batMv - BATTERY_MIN_MV) / (BATTERY_MAX_MV - BATTERY_MIN_MV) * 100.0f);
    return constrain(pct, 0, 100);
#endif
}

static void _displaySetPointScreen(float temp, float setpt) {
    _lcd.clear();
    _lcd.setCursor(0, 0);
    _lcd.printf("Now:  %.1f C", temp);
    _lcd.setCursor(0, 1);
    _lcd.printf("Set:  %.1f C", setpt);
}

static float readPotSetpoint() {
    long sum = 0;
    for (int i = 0; i < 8; i++) { sum += analogRead(PIN_POT_ADC); delay(1); }
    float raw = sum / 8.0f;
    return SETPOINT_MIN + (raw / 4095.0f) * (SETPOINT_MAX - SETPOINT_MIN);
}

// -----------------------------------------------------------------------------
// Public API
// -----------------------------------------------------------------------------

/**
 * Initialise thermostat hardware.
 * Called once from setup() on every wakeup.
 */
void nodeSetup() {
    if (thermoFirstBoot) {
        _prefs.begin("thermo", true);
        thermoSetPoint = _prefs.getFloat("setpt", 20.0f);
        _prefs.end();
        thermoFirstBoot = false;
        Serial.printf("[THERMO] Loaded setpoint %.1f°C from NVS\n", thermoSetPoint);
    }
    Wire.begin(PIN_LCD_SDA, PIN_LCD_SCL);
    _lcd.init();
    _lcd.noBacklight();
}

/*
 * Run the UI task — turn on LCD, allow setpoint adjustment via encoder.
 * Blocks until idle timeout, then saves setpoint to NVS and returns.
 * Called from main firmware on button wakeup.
 */

void nodeRunUI() {
    g_uiActive = true;
    Wire.begin(PIN_LCD_SDA, PIN_LCD_SCL);
    _lcd.init();
    _lcd.backlight();

    float temp = 0.0f;
    if (xSemaphoreTake(g_dhtMutex, pdMS_TO_TICKS(8000)) == pdTRUE) {
        _dht.begin();
        delay(DHT_STARTUP_MS);
        temp = _readTemperature();
        xSemaphoreGive(g_dhtMutex);
    }

    _displaySetPointScreen(temp, thermoSetPoint);

    pinMode(PIN_EXT0,  INPUT_PULLUP);

    unsigned long lastActivity = millis();

    while (true) {
        if (g_sleepPending || millis() - lastActivity >= UI_TIMEOUT_MS) {
            _prefs.begin("thermo", false);
            _prefs.putFloat("setpt", thermoSetPoint);
            _prefs.end();
            Serial.printf("[THERMO] Setpoint saved, closing UI\n");
            break;
        }

        float newSetpt = readPotSetpoint();
        newSetpt = roundf(newSetpt * 10.0f) / 10.0f;
        if (fabsf(newSetpt - thermoSetPoint) >= 0.1f) {
            thermoSetPoint = constrain(newSetpt, SETPOINT_MIN, SETPOINT_MAX);
            _displaySetPointScreen(temp, thermoSetPoint);
            lastActivity = millis();
            Serial.printf("[THERMO] Setpoint -> %.1f°C\n", thermoSetPoint);
        }

        delay(5);
    }

    _lcd.clear();
    _lcd.noBacklight();
    g_uiActive = false;
}

/*
 * Read all sensors and build the uplink payload.
 *
 * Payload format (8 bytes):
 *   [0] NODE_ID
 *   [1] 0x01 (uplink marker)
 *   [2] temp * 2  (0.5°C resolution)
 *   [3] humidity  (%)
 *   [4] setpoint  (°C)
 *   [5] battery   (%)
 *
 * Sets *len to the number of bytes written.
 * Caller must provide a buffer of at least 6 bytes.
 */
void nodeBuildPayload(uint8_t nodeId, uint8_t* buf, uint8_t* len) {

    float temp = 0.0f, humidity = 0.0f;
    if (xSemaphoreTake(g_dhtMutex, pdMS_TO_TICKS(8000)) == pdTRUE) {
        _dht.begin();
        delay(DHT_STARTUP_MS);
        _dht.readTemperature();  // discard first read
        _dht.readHumidity();
        delay(2000);
        temp     = _readTemperature();
        humidity = _readHumidity();
        xSemaphoreGive(g_dhtMutex);
    }
    int   battPct  = _readBatteryPercent();

    int16_t temp_enc = encodeTemp10(temp);
    int16_t set_enc  = encodeTemp10(thermoSetPoint);

    Serial.printf("[THERMO] Temp: %.1f°C  Hum: %.0f%%  Setpt: %.1f°C  Batt: %d%%\n",
         temp, humidity, thermoSetPoint, battPct);

    buf[0] = nodeId;
    buf[1] = 0x01;
    buf[2] = (temp_enc >> 8) & 0xFF;
    buf[3] = temp_enc & 0xFF;
    buf[4] = (uint8_t)humidity;
    buf[5] = (set_enc >> 8) & 0xFF;
    buf[6] = set_enc & 0xFF;
    buf[7] = (uint8_t)battPct;

    *len = 8;
}

/*
 * Handle a downlink command from the gateway.
 *
 * CMD_SET_SETPOINT (0x01): data[0] = int part of new setpoint, data[1] = fractional part in hundredths
 */

void nodeHandleDownlink(uint8_t cmd, uint8_t* data, uint8_t dataLen) {
    Serial.print("Command received: ");
    Serial.println(cmd);
    
    if (cmd == CMD_SET_SETPOINT && dataLen >= 4) {
        // data contains ASCII hex: ['0', 'A', '7', '8']
        // Convert to string and parse as hex
        char hexStr[5] = {(char)data[0], (char)data[1], (char)data[2], (char)data[3], '\0'};
        uint16_t rawValue = (uint16_t)strtol(hexStr, nullptr, 16);
        float newSetPoint = rawValue / 100.0f;
        
        thermoSetPoint = constrain(newSetPoint, SETPOINT_MIN, SETPOINT_MAX);
        _prefs.begin("thermo", false);
        _prefs.putFloat("setpt", thermoSetPoint);
        _prefs.end();
        
        Serial.printf("[THERMO] Setpoint updated to %.1f°C via downlink\n", thermoSetPoint);
    }
}

// -----------------------------------------------------------------------------
// Deep-sleep support — called from Nodes.cpp setup()
// -----------------------------------------------------------------------------
#include "esp_sleep.h"
#include <soc/rtc.h>

// Survives deep sleep: true once we have successfully synced to the gateway beacon
RTC_DATA_ATTR bool     g_tdmaSynced     = false;
RTC_DATA_ATTR uint64_t g_sleepStartTick = 0;  // RTC ticks when we entered deep sleep
RTC_DATA_ATTR uint32_t g_sleepMs        = 0;  // how long we planned to sleep (ms)

// Put the ESP32 into deep sleep for sleepMs milliseconds.
// Timer wakeup resumes the TDMA cycle; EXT0 on encoder button runs the UI.
// Caller is responsible for putting LoRa to sleep before calling this.
void nodeGoSleep(uint32_t sleepMs) {
    g_sleepStartTick = rtc_time_get();
    g_sleepMs        = sleepMs;
    Serial.printf("[SLEEP] %u ms\n", sleepMs);
    Serial.flush();
    esp_sleep_enable_timer_wakeup((uint64_t)sleepMs * 1000ULL);
    esp_sleep_enable_ext0_wakeup((gpio_num_t)PIN_EXT0, 0);  // press = LOW
    esp_deep_sleep_start();
}

// Check wakeup cause. If the encoder button was pressed, run the UI and return true.
bool nodeHandleWakeup() {
    if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT0) {
        return true;
    }
    return false;
}