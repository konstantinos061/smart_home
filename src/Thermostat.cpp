#include <Arduino.h>
#include "thermostat.h"

// ---------------------------------------------------------------------------
// TDMA timing
// ---------------------------------------------------------------------------
#define BEACON_INTERVAL_MS  55000
#define BEACON_WINDOW_MS    3000
#define FIRST_LISTEN_MS     300000
#define TX_WINDOW_MS        2000
#define ACK_WINDOW_MS       2000
#define NEW_BEACON_MS       59500
#define COMMOM_SLOT_PERIOD  10000

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
volatile bool    g_txActive   = false;  // true during TX+ACK — suppresses LCD I2C traffic

// ---------------------------------------------------------------------------
// LoRa helpers
// ---------------------------------------------------------------------------
static String loraCmd(const String& cmd, uint32_t timeoutMs = 1000) {
    loraSerial.println(cmd);
    unsigned long start = xTaskGetTickCount();
    while (xTaskGetTickCount() - start < timeoutMs) {
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
    pinMode(PIN_ENC_SW, INPUT_PULLUP);
    while (true) {
        if (digitalRead(PIN_ENC_SW) == LOW) {
            nodeRunUI();
            delay(200);  // debounce after UI exits
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

// ===========================================================================
// SETUP — deep-sleep driven: one TDMA cycle per boot
// ===========================================================================
void setup() {
    Serial.begin(115200);
    delay(200);
    tlog("\n===== Thermostat 0x%02X =====\n", NODE_ID);

    // Encoder press wakeup during deep sleep: run UI, sleep for remaining time, then return
    if (nodeHandleWakeup()) {
        return;
    }

    nodeSetup();

    // Start UI task now so button works throughout the entire active window
    xTaskCreate(uiTask, "UI", 4096, NULL, 1, NULL);

    if (!loraInit()) {
        Serial.println("[ERROR] LoRa init failed");
        nodeGoSleep(10000);
    }
    Serial.println("[LORA] Init OK");

    nodeBuildPayload(NODE_ID, g_payload, &g_payloadLen);

    // Short window once synced, 5-min window on first boot
    uint32_t beaconWindow = g_tdmaSynced ? (BEACON_WINDOW_MS + 2000) : FIRST_LISTEN_MS;
    
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
    uint32_t beaconMs = millis();

    // Wait for our assigned TDMA slot
    TickType_t tc = xTaskGetTickCount();
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
                Serial.println("[NODE] ACK: " + s);
            }
        }
        
        vTaskDelay(5);
    }

    // Sleep until just before the next beacon
    uint32_t elapsed = millis() - beaconMs;
    uint32_t sleepMs = (NEW_BEACON_MS > elapsed + 1500)
                       ? NEW_BEACON_MS - elapsed - 1500
                       : NEW_BEACON_MS - 1500;

    Serial.printf("[NODE] elapsed=%u ms  sleep=%u ms\n", elapsed, sleepMs);
    loraCmd("radio rxstop", 500);
    loraSerial.println("sys sleep " + String(sleepMs + 3000));  // LoRa sleeps a bit longer
    delay(100);
    nodeGoSleep(sleepMs);
}

void loop() {}
