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
#include <DHT.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Preferences.h>

// -----------------------------------------------------------------------------
// Pin definitions
// -----------------------------------------------------------------------------
#define PIN_DHT           5
#define PIN_ENC_CLK       34
#define PIN_ENC_DT        35
#define PIN_ENC_SW        32
#define PIN_LCD_BACKLIGHT 27
#define PIN_BATTERY_ADC   33

// -----------------------------------------------------------------------------
// Configuration
// -----------------------------------------------------------------------------
#define DHT_STARTUP_MS  2500
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
static DHT             _dht(PIN_DHT, DHT11);
static LiquidCrystal_I2C _lcd(0x27, 16, 2);
static Preferences     _prefs;

// RTC memory — survives deep sleep
extern RTC_DATA_ATTR float    thermoSetPoint;
extern RTC_DATA_ATTR bool     thermoFirstBoot;

RTC_DATA_ATTR float   thermoSetPoint  = 20.0f;
RTC_DATA_ATTR bool    thermoFirstBoot = true;

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
    return 100;
#else
    long sum = 0;
    for (int i = 0; i < 16; i++) { sum += analogRead(PIN_BATTERY_ADC); delay(2); }
    float adcV  = (sum / 16.0f) * (3.3f / 4095.0f);
    float batV  = adcV * ((BATTERY_DIV_R1 + BATTERY_DIV_R2) / BATTERY_DIV_R2);
    float batMv = batV * 1000.0f;
    int pct = (int)((batMv - BATTERY_MIN_MV) / (BATTERY_MAX_MV - BATTERY_MIN_MV) * 100.0f);
    return constrain(pct, 0, 100);
#endif
}

static void _lcdBacklight(bool on) {
    pinMode(PIN_LCD_BACKLIGHT, OUTPUT);
    digitalWrite(PIN_LCD_BACKLIGHT, on ? HIGH : LOW);
    if (on) _lcd.backlight();
    else    _lcd.noBacklight();
}

static void _displaySetPointScreen(float temp, float setpt) {
    _lcd.clear();
    _lcd.setCursor(0, 0);
    _lcd.printf("Now:  %.1f C", temp);
    _lcd.setCursor(0, 1);
    _lcd.printf("Set:  %.1f C", setpt);
}

static float _readEncoder() {
    static int lastClk = HIGH;
    int clk = digitalRead(PIN_ENC_CLK);
    int dt  = digitalRead(PIN_ENC_DT);
    float delta = 0.0f;
    if (clk != lastClk && clk == LOW) {
        delta = (dt != clk) ? -0.5f : +0.5f;
    }
    lastClk = clk;
    return delta;
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
}

/**
 * Run the UI task — turn on LCD, allow setpoint adjustment via encoder.
 * Blocks until idle timeout, then saves setpoint to NVS and returns.
 * Called from main firmware on button wakeup.
 */
void nodeRunUI() {
    Wire.begin(21, 22);
    _lcd.init();
    _lcd.begin(16, 2);
    _lcdBacklight(true);

    _dht.begin();
    delay(DHT_STARTUP_MS);
    float temp = _readTemperature();

    _displaySetPointScreen(temp, thermoSetPoint);

    pinMode(PIN_ENC_CLK, INPUT);
    pinMode(PIN_ENC_DT,  INPUT);
    pinMode(PIN_ENC_SW,  INPUT_PULLUP);

    unsigned long lastActivity = millis();

    while (true) {
        if (millis() - lastActivity >= UI_TIMEOUT_MS) {
            _prefs.begin("thermo", false);
            _prefs.putFloat("setpt", thermoSetPoint);
            _prefs.end();
            Serial.println("[THERMO] Setpoint saved, closing UI");
            break;
        }

        float delta = _readEncoder();
        if (delta != 0.0f) {
            thermoSetPoint = constrain(thermoSetPoint + delta, SETPOINT_MIN, SETPOINT_MAX);
            _displaySetPointScreen(temp, thermoSetPoint);
            lastActivity = millis();
            Serial.printf("[THERMO] Setpoint -> %.1f°C\n", thermoSetPoint);
        }

        delay(5);
    }

    _lcd.clear();
    _lcdBacklight(false);
}

/**
 * Read all sensors and build the uplink payload.
 *
 * Payload format (6 bytes):
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
    _dht.begin();
    delay(DHT_STARTUP_MS);
    _dht.readTemperature();  // discard first read
    _dht.readHumidity();
    delay(2000);

    float temp     = _readTemperature();
    float humidity = _readHumidity();
    int   battPct  = _readBatteryPercent();

    int16_t temp_enc = encodeTemp10(temp);
    int16_t set_enc  = encodeTemp10(thermoSetPoint);

    Serial.printf("[THERMO] Temp: %.1f°C  Hum: %.0f%%  Setpt: %.1f°C  Batt: %d%%\n",
                  temp, humidity, thermoSetPoint, battPct);

    buf[0] = nodeId;
    buf[1] = 0x01;

    // temperature
    buf[2] = (temp_enc >> 8) & 0xFF;
    buf[3] = temp_enc & 0xFF;

    buf[4] = (uint8_t)humidity;

    // setpoint
    buf[5] = (set_enc >> 8) & 0xFF;
    buf[6] = set_enc & 0xFF;

    buf[7] = (uint8_t)battPct;

    *len = 8;
}

/**
 * Handle a downlink command from the gateway.
 *
 * CMD_SET_SETPOINT (0x01): data[0] = new setpoint in °C
 */
void nodeHandleDownlink(uint8_t cmd, uint8_t* data, uint8_t dataLen) {
    if (cmd == CMD_SET_SETPOINT && dataLen >= 1) {
        thermoSetPoint = constrain((float)data[0], SETPOINT_MIN, SETPOINT_MAX);
        _prefs.begin("thermo", false);
        _prefs.putFloat("setpt", thermoSetPoint);
        _prefs.end();
        Serial.printf("[THERMO] Setpoint updated to %.1f°C via downlink\n", thermoSetPoint);
    }
}
