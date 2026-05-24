#include <Arduino.h>

// ---------------------------------------------------------------------------
// TDMA timing
// ---------------------------------------------------------------------------
#define BEACON_INTERVAL_MS  55000
#define BEACON_WINDOW_MS    3500
#define FIRST_LISTEN_MS     300000
#define TX_WINDOW_MS        2000
#define ACK_WINDOW_MS       2000
#define NEW_BEACON_MS       59500
#define COMMOM_SLOT_PERIOD  10000

#include "thermostat.h"

// ---------------------------------------------------------------------------
// LoRa UART
// ---------------------------------------------------------------------------
#define LORA_TX    17
#define LORA_RX    16
#define LORA_RST   25
#define LORA_FREQ  "868100000"
#define LORA_SF    "sf7"
#define LORA_BW    "125"
#define LORA_CR    "4/5"
#define LORA_AFCBW "41.7"
#define LORA_PWR   "14"
#define LORA_SYNC  "12"

HardwareSerial loraSerial(2);

static uint8_t   g_payload[16];
static uint8_t   g_payloadLen = 0;
static uint32_t  g_beaconMs   = 0;
volatile bool    g_txActive        = false;
volatile bool    g_uiActive        = false;
volatile bool    g_sleepPending    = false;
static bool      g_startUIImmediately = false;
SemaphoreHandle_t g_dhtMutex      = nullptr;

// ---------------------------------------------------------------------------
// LoRa helpers
// ---------------------------------------------------------------------------
static String loraCmd(const String& cmd, uint32_t timeoutMs = 1000) {
    loraSerial.println(cmd);
    unsigned long start = millis();
    while (millis() - start < timeoutMs) {
        if (loraSerial.available()) {
            String resp = loraSerial.readStringUntil('\n');
            resp.trim();
            return resp;
        }
    }
    return "timeout";
}

static bool loraInit() {
    pinMode(LORA_RST, OUTPUT);
    digitalWrite(LORA_RST, LOW);  delay(100);
    digitalWrite(LORA_RST, HIGH); delay(500);

    loraSerial.begin(57600, SERIAL_8N1, LORA_RX, LORA_TX);
    loraSerial.setTimeout(2000);
    delay(300);
    while (loraSerial.available()) loraSerial.readStringUntil('\n');

    loraCmd("mac pause", 1000);

    String r;
    r = loraCmd("radio set mod lora");          if (r != "ok") { Serial.println("[LORA] mod lora -> "  + r); return false; }
    r = loraCmd("radio set freq " LORA_FREQ);   if (r != "ok") { Serial.println("[LORA] freq -> "      + r); return false; }
    r = loraCmd("radio set wdt 0");             if (r != "ok") { Serial.println("[LORA] wdt -> "       + r); return false; }
    r = loraCmd("radio set pwr "  LORA_PWR);    if (r != "ok") { Serial.println("[LORA] pwr -> "       + r); return false; }
    r = loraCmd("radio set sf "   LORA_SF);     if (r != "ok") { Serial.println("[LORA] sf -> "        + r); return false; }
    r = loraCmd("radio set afcbw " LORA_AFCBW); if (r != "ok") { Serial.println("[LORA] afcbw -> "     + r); return false; }
    r = loraCmd("radio set rxbw " LORA_BW);     if (r != "ok") { Serial.println("[LORA] rxbw -> "      + r); return false; }
    r = loraCmd("radio set prlen 8");           if (r != "ok") { Serial.println("[LORA] prlen -> "     + r); return false; }
    r = loraCmd("radio set crc on");            if (r != "ok") { Serial.println("[LORA] crc -> "       + r); return false; }
    r = loraCmd("radio set iqi off");           if (r != "ok") { Serial.println("[LORA] iqi -> "       + r); return false; }
    r = loraCmd("radio set cr "   LORA_CR);     if (r != "ok") { Serial.println("[LORA] cr -> "        + r); return false; }
    r = loraCmd("radio set sync " LORA_SYNC);   if (r != "ok") { Serial.println("[LORA] sync -> "      + r); return false; }
    r = loraCmd("radio set bw "   LORA_BW);     if (r != "ok") { Serial.println("[LORA] bw -> "        + r); return false; }
    return true;
}

static void radioSleep(uint32_t ms) {
    if (ms < 100) ms = 100;
    loraSerial.println("sys sleep " + String(ms));
    delay(20);
    while (loraSerial.available()) loraSerial.read();
}

static void radioWake() {
    loraSerial.end();
    pinMode(LORA_TX, OUTPUT);
    digitalWrite(LORA_TX, LOW);  delay(5);
    digitalWrite(LORA_TX, HIGH); delayMicroseconds(100);
    loraSerial.begin(57600, SERIAL_8N1, LORA_RX, LORA_TX);
    loraSerial.setTimeout(2000);
    loraSerial.write(0x55);
    delay(50);
    while (loraSerial.available()) loraSerial.read();
}

static bool listenForBeacon(uint32_t windowMs) {
    Serial.printf("[BEACON] Listening for %u ms...\n", windowMs);

    loraCmd("radio rxstop");
    loraCmd("radio rx 0");

    TickType_t start = xTaskGetTickCount();
    while (xTaskGetTickCount() < start + pdMS_TO_TICKS(windowMs)) {
        if (loraSerial.available() > 0) {
            String resp = loraSerial.readStringUntil('\n');
            resp.trim();
            if (resp.startsWith("radio_rx")) {
                int spaceIdx = resp.lastIndexOf(' ');
                if (spaceIdx >= 0 && resp.substring(spaceIdx + 1).startsWith("53")) {
                    String payload = resp.substring(spaceIdx + 1);
                    if (payload.length() > 22) {
                        const char* p = payload.c_str() + 22;
                        char ID[3] = {p[0], p[1], '\0'};
                        if ((uint8_t)strtol(ID, nullptr, 16) == NODE_ID) {
                            // dynamic slot assignment placeholder
                        }
                    }
                    loraCmd("radio rxstop");
                    vTaskDelay(pdMS_TO_TICKS(50));
                    Serial.println("[BEACON] Received!");
                    return true;
                }
            } else if (resp.startsWith("radio_err")) {
                loraCmd("radio rxstop");
                loraCmd("radio rx 0");
            }
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }

    loraCmd("radio rxstop");
    Serial.println("[BEACON] Not received");
    return false;
}

// ---------------------------------------------------------------------------
// UI task — runs in parallel with the TDMA cycle while the ESP is awake.
// Polls the encoder button and calls nodeRunUI() on press.
// Killed automatically when esp_deep_sleep_start() fires.
// ---------------------------------------------------------------------------
static void uiTask(void*) {
     pinMode(PIN_EXT0, INPUT_PULLUP);
    
    // If woken by button, run UI immediately without waiting for a press
    if (g_startUIImmediately) {
        nodeRunUI();
        delay(200);
    }
    
    while (true) {
        if (digitalRead(PIN_EXT0) == LOW) {
            nodeRunUI();
            delay(200);
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

static void tdmaTask(void*) {
    TickType_t tc = xTaskGetTickCount();

    // Build payload during the wait for our TDMA slot (DHT needs ~4500ms anyway)
    nodeBuildPayload(NODE_ID, g_payload, &g_payloadLen);

    // Wait for our assigned TDMA slot, counted from task start (beacon time)
    xTaskDelayUntil(&tc, pdMS_TO_TICKS(TRANS_SLOT_MS));

    // TX window
    Serial.printf("[NODE 0x%02X] TX\n", NODE_ID);
    char sending[64];
    sprintf(sending, "radio tx %02X%02X%02X%02X%02X%02X%02X%02X",
            g_payload[0], g_payload[1], g_payload[2], g_payload[3],
            g_payload[4], g_payload[5], g_payload[6], g_payload[7]);

    TickType_t period = xTaskGetTickCount();
    while (xTaskGetTickCount() < period + pdMS_TO_TICKS(TX_WINDOW_MS)) {
        loraSerial.println(sending);
        loraSerial.readStringUntil('\n');
        loraSerial.readStringUntil('\n');
        vTaskDelay(10);
    }

    // ACK + downlink window
    loraCmd("radio rx 0");
    period = xTaskGetTickCount();
    while (xTaskGetTickCount() < period + pdMS_TO_TICKS(ACK_WINDOW_MS)) {
        if (loraSerial.available()) {
            String s = loraSerial.readStringUntil('\n');
            s.trim();
            if (s.indexOf("radio_rx") == 0) {
                s = s.substring(9);
                Serial.println("[NODE] ACK: " + s);
                if(s.length() > 6) {
                    s = s.substring(7);
                    char ID[3] = {s[0], s[1], '\0'};
                    char cmd[3] = {s[2], s[3], '\0'};
                    const char* dataStr = s.c_str() + 4;
                    uint8_t node_id = (uint8_t)strtol(ID, nullptr, 16);
 
                    if (node_id == 32) {
                            nodeHandleDownlink((uint8_t)strtol(cmd, nullptr, 16), (uint8_t*)dataStr, s.length() - 4);
                    }
                    
                }
                
            }
        }
        
        vTaskDelay(5);
    }

    // Sleep until just before the next beacon
    uint32_t elapsed = millis() - g_beaconMs;
    uint32_t sleepMs = (NEW_BEACON_MS > elapsed + 2500)
                       ? NEW_BEACON_MS - elapsed - 2500
                       : NEW_BEACON_MS - 2500;

    Serial.printf("[NODE] elapsed=%u ms  sleep=%u ms\n", elapsed, sleepMs);
    g_uiWasActive = g_uiActive;  // persist across deep sleep so next boot re-opens UI
    g_sleepPending = true;       // signal UI task to shut down cleanly
    loraCmd("radio rxstop", 500);
    delay(300);  // give UI task time to save setpoint and turn off LCD
    rtc_gpio_hold_en((gpio_num_t)LORA_RST);
    radioSleep(sleepMs + 3000);
    nodeGoSleep(sleepMs);
}

// ===========================================================================
// SETUP — deep-sleep driven: one TDMA cycle per boot
// ===========================================================================
void setup() {
    Serial.begin(115200);
    delay(200);
    Serial.printf("\n===== Thermostat 0x%02X =====\n", NODE_ID);

    bool buttonWakeup    = nodeHandleWakeup();
    g_startUIImmediately = buttonWakeup || g_uiWasActive;
    g_uiWasActive        = false;  // consumed — clear before next cycle

    rtc_gpio_hold_dis((gpio_num_t)LORA_RST);
    if (!buttonWakeup && g_tdmaSynced) {
        radioWake();
    }

    nodeSetup();

    g_dhtMutex = xSemaphoreCreateMutex();

    // Start UI task now so button works throughout the entire active window
    xTaskCreatePinnedToCore(uiTask, "UI", 4096, NULL, 1, NULL,0);

    if (!loraInit()) {
        Serial.println("[ERROR] LoRa init failed");
        nodeGoSleep(10000);
    }
    Serial.println("[LORA] Init OK");

    // Beacon window depends on wakeup cause:
    //   Timer wakeup (incl. UI resume): short window (already synced) or full 5-min (first boot)
    //   Button wakeup: calculate from remaining planned sleep, or force full resync if overrun
    uint32_t beaconWindow;
    if (buttonWakeup) {
        uint64_t elapsedTicks = rtc_time_get() - g_sleepStartTick;
        uint32_t hz           = rtc_clk_slow_freq_get_hz();
        uint32_t elapsedMs    = (uint32_t)(elapsedTicks * 1000ULL / hz);
        Serial.printf("[WAKEUP] slept+button = %u ms of planned %u ms\n", elapsedMs, g_sleepMs);
        if (g_sleepMs > elapsedMs + 3000) {
            // Beacon arrives at (g_sleepMs + 2500) from sleepStartTick.
            // +2500 corrects for the early-wake offset; +5000 is the true margin.
            beaconWindow = g_sleepMs - elapsedMs + 2500 + 5000;
        } else {
            // Overrun: lost sync, force 5-min hunt
            g_tdmaSynced = false;
            beaconWindow = FIRST_LISTEN_MS;
        }
    } else {
        beaconWindow = g_tdmaSynced ? (BEACON_WINDOW_MS + 2000) : FIRST_LISTEN_MS;
    }
    
    bool gotBeacon = listenForBeacon(beaconWindow);
    // Simulate beacon
    //bool gotBeacon = true;

    if (!gotBeacon) {
        loraCmd("radio rxstop", 500);
        loraSerial.println("sys sleep " + String(BEACON_INTERVAL_MS));
        delay(100);
        nodeGoSleep(BEACON_INTERVAL_MS - 1500);
    }

    g_tdmaSynced = true;
    g_beaconMs = millis();

    xTaskCreatePinnedToCore(tdmaTask, "TDMA", 4096, NULL, 1, NULL,1);
}


void loop() {}
