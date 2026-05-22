#include <Arduino.h>
#include <WiFi.h>
#include <sys/time.h>
#include "esp_bt.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "driver/rtc_io.h"

// Author: Gabriel Crawford
// RTC backed monotonic microsecond clock survives deep sleep, unlike
// esp_timer_get_time() which resets every wake.
static int64_t rtcMicros() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (int64_t)tv.tv_sec * 1000000LL + tv.tv_usec;
}

// ---------------------------------------------------------------------------
// Node type selection set exactly one of these in build_flags:
//   -D NODE_THERMOSTAT
//   -D NODE_LIGHT
//   -D NODE_LOCK
// Also required in build_flags:
//   -D NODE_ID=0xXX       (protocol address sent in every packet)
//   -D TRANS_SLOT_MS=NNNN (ms after beacon when this node may transmit)
// ---------------------------------------------------------------------------
#if defined(NODE_THERMOSTAT)
  #include "thermostat.h"
#elif defined(NODE_LIGHT)
  #include "light_node.h"
#elif defined(NODE_LOCK)
  #include "lock_node.h"
#else
  #error "No node type defined. Add -D NODE_THERMOSTAT, NODE_LIGHT, or NODE_LOCK to build_flags."
#endif

// ---------------------------------------------------------------------------
// TDMA timing
// ---------------------------------------------------------------------------
#define BEACON_INTERVAL_MS  55000    // arbitrary retry/sleep-chunk only...NOT the gateway period
#define FRAME_SIZE_MS       60000    // actual gateway frame period 
#define BEACON_WINDOW_MS    5000     // ±2.5 s around expected beacon
#define FIRST_LISTEN_MS     300000    
#define TX_WINDOW_MS        2000
#define ACK_WINDOW_MS       2000
#define SENSOR_PERIOD_MS    30000    // how often the sensor task reads
#define COMMOM_SLOT_PERIOD  10000    // ms for slot index => TRANS_SLOT_MS
#define PIR_WARMUP_MS       60000    // ignore PIR wake during sensor stabilization (HC-SR501 needs ~30-60 s)

// Gateway assigned slot offset, initialised from build time TRANS_SLOT_MS, but
// may be updated when the gateway appends [NODE_ID][slot] to the SYNC_BEACON
// for a new joinee. RTC retained so the assignment survives deep sleep.
RTC_DATA_ATTR static uint32_t g_transSlotMs = TRANS_SLOT_MS;

// ---------------------------------------------------------------------------
// LoRa UART
// ---------------------------------------------------------------------------
#define LORA_TX    18
#define LORA_RX    19
#define LORA_RST   15
#define LORA_FREQ  "868100000"
#define LORA_FREQ_RX "868300000"
#define LORA_SF    "sf7"
#define LORA_BW    "125"
#define LORA_CR    "4/5"
#define LORA_AFCBW "41.7"
#define LORA_PWR   "14"
#define LORA_SYNC  "12"

HardwareSerial loraSerial(2);

#ifdef NODE_LIGHT
// Deep sleep model everything runs linearly in setup().
RTC_DATA_ATTR static int64_t g_nextBeaconUs   = 0;
RTC_DATA_ATTR static int64_t g_lastBeaconUs   = 0;   // last successful beacon detection 
RTC_DATA_ATTR static bool    g_loraInitedOnce = false;
RTC_DATA_ATTR static bool    g_radioSleeping  = false;
// After a successful beacon listen we deep sleep through the slot wait
// When this flag is true on the next timer wake we
// skip the listen and jump straight to the TX phase.
RTC_DATA_ATTR static bool    g_pendingTx      = false;
// PIR cold start: skip ext0 wake until the sensor has stabilised. esp_timer
// resets to 0 every boot, so we use a cycle based gate and armed once we've
// completed at least one full sleep cycle (≥ BEACON_INTERVAL_MS ≈ 55 s,
// close enough to PIR_WARMUP_MS).
RTC_DATA_ATTR static bool    g_pirArmed       = false;
// Motion level at sleep entry ext0 was armed for the OPPOSITE level, so on
// PIR wake the new state is !g_motionAtSleep 
RTC_DATA_ATTR static bool    g_motionAtSleep  = false;
#else
TaskHandle_t Comms_TaskHandle  = NULL;
TaskHandle_t Sensor_TaskHandle = NULL;
TaskHandle_t Rx_TaskHandle     = NULL;

// Shared payload: Sensor task writes, Comms task reads
static uint8_t           g_payload[16];
static uint8_t           g_payloadLen = 0;
static SemaphoreHandle_t g_payloadMutex = NULL;
#endif

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

    // Drain the reset banner (RN2483 prints "RN2483 X.Y.Z ...") for up to 1.5 s.
    // If nothing arrives, the module isn't powered or TX/RX are swapped.
    String banner;
    unsigned long deadline = millis() + 1500;
    while (millis() < deadline) {
        if (loraSerial.available()) {
            banner = loraSerial.readStringUntil('\n');
            banner.trim();
            if (banner.length()) break;
        }
        delay(10);
    }
    if (banner.length()) {
        Serial.println("[LORA] Module alive, banner: " + banner);
    } else {
        Serial.println("[LORA] No reset banner:module may be unpowered or TX/RX swapped");
    }

    // Independent liveness probe: ask for firmware version.
    String ver = loraCmd("sys get ver", 1000);
    if (ver == "timeout" || ver.length() == 0) {
        Serial.println("[LORA] sys get ver -> no response (module not responding)");
        return false;
    }
    Serial.println("[LORA] sys get ver -> " + ver);

    String pause = loraCmd("mac pause", 1000);
    Serial.println("[LORA] mac pause -> " + pause);

    String r;
    r = loraCmd("radio set mod lora");         if (r != "ok") { Serial.println("[LORA] mod lora -> "  + r); return false; }
    r = loraCmd("radio set freq " LORA_FREQ);  if (r != "ok") { Serial.println("[LORA] freq -> "      + r); return false; }
    r = loraCmd("radio set wdt 0");            if (r != "ok") { Serial.println("[LORA] wdt -> "       + r); return false; }
    r = loraCmd("radio set pwr "  LORA_PWR);   if (r != "ok") { Serial.println("[LORA] pwr -> "       + r); return false; }
    r = loraCmd("radio set sf "   LORA_SF);    if (r != "ok") { Serial.println("[LORA] sf -> "        + r); return false; }
    r = loraCmd("radio set afcbw " LORA_AFCBW);if (r != "ok") { Serial.println("[LORA] afcbw -> "     + r); return false; }
    r = loraCmd("radio set rxbw " LORA_BW);    if (r != "ok") { Serial.println("[LORA] rxbw -> "      + r); return false; }
    r = loraCmd("radio set prlen 8");          if (r != "ok") { Serial.println("[LORA] prlen -> "     + r); return false; }
    r = loraCmd("radio set crc on");           if (r != "ok") { Serial.println("[LORA] crc -> "       + r); return false; }
    r = loraCmd("radio set iqi off");          if (r != "ok") { Serial.println("[LORA] iqi -> "       + r); return false; }
    r = loraCmd("radio set cr "   LORA_CR);    if (r != "ok") { Serial.println("[LORA] cr -> "        + r); return false; }
    r = loraCmd("radio set sync " LORA_SYNC);  if (r != "ok") { Serial.println("[LORA] sync -> "      + r); return false; }
    r = loraCmd("radio set bw "   LORA_BW);    if (r != "ok") { Serial.println("[LORA] bw -> "        + r); return false; }
    return true;
}

// ---------------------------------------------------------------------------
// Listen for gateway beacon payload starts with 0x53 = 'S'
// ---------------------------------------------------------------------------
static bool listenForBeacon(uint32_t windowMs) {
    Serial.printf("[BEACON] Listening for %u ms...\n", windowMs);

    // Drain stale UART bytes left over from before sleep (stray radio_err,
    // truncated radio_rx, etc.) otherwise the next loraCmd reads them as
    // its response and every other next command is misaligned by one line
    while (loraSerial.available()) loraSerial.read();

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

                    // SYNC_BEACON = "53594E435F424541434F4E" (11 bytes = 22 hex chars).
                    // If the gateway appended a new joinee slot assignment, the
                    // bytes after offset 22 are [NODE_ID][slot] in hex.
                    if (payload.length() >= 26) {
                        const char* assign = payload.c_str() + 22;
                        char idHex[3]   = { assign[0], assign[1], '\0' };
                        char slotHex[3] = { assign[2], assign[3], '\0' };
                        uint8_t actualId = (uint8_t)strtol(idHex,   nullptr, 16);
                        uint8_t slot     = (uint8_t)strtol(slotHex, nullptr, 16);

                        if (actualId == NODE_ID) {
                            g_transSlotMs = (uint32_t)slot * COMMOM_SLOT_PERIOD;
                            Serial.printf("[BEACON] Slot assignment: slot=%u -> %u ms\n",
                                          slot, g_transSlotMs);
                        }
                    }

                    loraCmd("radio rxstop");
                    vTaskDelay(pdMS_TO_TICKS(50));
                    Serial.println("[BEACON] Received!");
                    return true;
                }
            }
            else if (resp.startsWith("radio_err")) {
                loraCmd("radio rxstop");
                loraCmd("radio rx 0");
            }
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }

    loraCmd("radio rxstop");
    vTaskDelay(pdMS_TO_TICKS(50));
    Serial.println("[BEACON] Not received");
    return false;
}

// ---------------------------------------------------------------------------
// Transmit a raw byte buffer over LoRa
// ---------------------------------------------------------------------------
static void sendPacket(const uint8_t* buf, uint8_t len) {
    String cmd = "radio tx ";
    for (uint8_t i = 0; i < len; i++) {
        char hex[3];
        sprintf(hex, "%02X", buf[i]);
        cmd += hex;
    }

    String resp = loraCmd(cmd, 500);
    if (resp != "ok") {
        Serial.printf("[NODE 0x%02X] TX start failed: %s\n", NODE_ID, resp.c_str());
        return;
    }

    loraSerial.setTimeout(3000);
    resp = loraSerial.readStringUntil('\n');
    resp.trim();
    if (resp == "radio_tx_ok") {
        Serial.printf("[NODE 0x%02X] TX OK (%d bytes)\n", NODE_ID, len);
    } else {
        Serial.printf("[NODE 0x%02X] TX failed: %s\n", NODE_ID, resp.c_str());
    }
    loraSerial.setTimeout(2000);
}

#ifndef NODE_LIGHT
// ---------------------------------------------------------------------------
// Parse and dispatch a downlink hex payload from the gateway.
// Expected format: [dest 1B] [cmd 1B] [data 0..N B]
// ---------------------------------------------------------------------------
static void handleDownlinkHex(const String& hex) {
    if (hex.length() < 4) return;

    uint8_t destId = (uint8_t)strtoul(hex.substring(0, 2).c_str(), nullptr, 16);
    if (destId != NODE_ID) return;

    uint8_t cmd     = (uint8_t)strtoul(hex.substring(2, 4).c_str(), nullptr, 16);
    uint8_t dataLen = (hex.length() - 4) / 2;
    uint8_t data[8] = {};
    for (uint8_t i = 0; i < dataLen && i < 8; i++) {
        data[i] = (uint8_t)strtoul(hex.substring(4 + i * 2, 6 + i * 2).c_str(), nullptr, 16);
    }

    //nodeHandleDownlink(cmd, data, dataLen);
}

// ---------------------------------------------------------------------------
// Sensor task reads sensors every SENSOR_PERIOD_MS and updates shared buf
// ---------------------------------------------------------------------------
void Sensor_TaskManager(void* pv) {
    while (1) {
        uint8_t buf[16];
        uint8_t len = 0;
        nodeBuildPayload(NODE_ID, buf, &len);

        if (len > 0) {
            xSemaphoreTake(g_payloadMutex, portMAX_DELAY);
            memcpy(g_payload, buf, len);
            g_payloadLen = len;
            xSemaphoreGive(g_payloadMutex);
        }

        vTaskDelay(pdMS_TO_TICKS(SENSOR_PERIOD_MS));
    }
}
#endif

String hex_to_string(const uint8_t* buf, uint8_t len) {
    String hex;
    for (uint8_t i = 0; i < len; i++) {
        char h[3];
        sprintf(h, "%02X", buf[i]);
        hex += h;
    }
    return hex;
}

#ifndef NODE_LIGHT
// ---------------------------------------------------------------------------
// Comms task TDMA: beacon → wait for slot → TX → listen ACK → repeat
// This is for other nodes, the LIGHT node uses a deep sleep flow instead
// ---------------------------------------------------------------------------
void Comms_TaskManager(void* pv) {
    static bool firstListen = true;
    TickType_t through_way_copy;
    TickType_t period;

    while (1) {
        Serial.println("--- Waiting for beacon ---");

        bool beaconReceived = false;
        while (!beaconReceived) {
            uint32_t listenWindow = firstListen ? FIRST_LISTEN_MS : BEACON_WINDOW_MS;
            beaconReceived = listenForBeacon(listenWindow);
            firstListen = false;

            if (!beaconReceived) {
                Serial.println("[BEACON] Retrying in 60 s...");
                vTaskDelay(pdMS_TO_TICKS(BEACON_INTERVAL_MS));
                if (!loraInit()) Serial.println("[ERROR] LoRa re-init failed");
            }
        }

        // Record beacon tick, then delay until our assigned slot
        xTaskNotifyGive(Rx_TaskHandle);  // Tell RX task to start listening for downlink in this beacon interval
        TickType_t start_time = xTaskGetTickCount();
        through_way_copy = start_time;
        xTaskDelayUntil(&through_way_copy, pdMS_TO_TICKS(TRANS_SLOT_MS));

        // --- TX window ---
        Serial.printf("[NODE 0x%02X] TX window open\n", NODE_ID);

        uint8_t buf[16];
        uint8_t len = 0;
        nodeBuildPayload(NODE_ID, buf, &len);

        Serial.println("\n--- Handling windows ---");

        for (int i = 0; i < len; i++) {
            Serial.printf("%02X ", buf[i]);
        }

        char sending[64];
        sprintf(sending , "radio tx %02X%02X%02X%02X%02X%02X%02X%02X", buf[0], buf[1], buf[2], buf[3], buf[4], buf[5], buf[6], buf[7]);
        //Serial.println(hex_to_string(buf, len));

        Serial.println("\n--- My Sending window ---");
        period = xTaskGetTickCount();
        while(xTaskGetTickCount() < (period + pdMS_TO_TICKS(TX_WINDOW_MS))) {
            
            loraSerial.println(sending);
            loraSerial.readStringUntil('\n');
            loraSerial.readStringUntil('\n');

            vTaskDelay(10);
        }

        loraCmd("radio rx 0");

        //Read ACK
        Serial.println("\n--- Listening for ACK ---");
        period = xTaskGetTickCount();
        while(xTaskGetTickCount() < (period + pdMS_TO_TICKS(ACK_WINDOW_MS))) {

            
            if(loraSerial.available() > 0){
                String str = loraSerial.readStringUntil('\n');
                Serial.println("str from lora: " + str);
                str.trim();

                if (str.indexOf("radio_rx") == 0) {
                Serial.println("Success! ACK receives: " + str);
                } 
                else if (str == "ok") {
                Serial.println("Module is now listening...");
                } 
                else if (str == "radio_err") {
                Serial.println("Slot Timeout: No signal heard.");
                }
                else {
                // Catch-all for weird garbage
                Serial.println("Unexpected: " + str);
                }

            }
            vTaskDelay(5);
        }

        xTaskNotifyGive(Rx_TaskHandle);
        through_way_copy = start_time;
        xTaskDelayUntil(&through_way_copy, pdMS_TO_TICKS(NEW_BEACON_MS));
    }
}
#endif  // !NODE_LIGHT
#ifndef NODE_LIGHT
// ===========================================================================
// Rx task listens for downlinks and dispatches to node handler
// ===========================================================================
 void Rx_TaskManager(void* pv) {
    while (1) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);  // Wait for notification from Comms task that RX window is open
        Serial.println("\n--- RX window open ---");
        loraCmd("radio rxstop");
        loraCmd("radio rx 0");
        while (1) {

            if (ulTaskNotifyTake(pdTRUE, 0) == 1) {
                Serial.println("Restarting RX window");
                loraCmd("radio rxstop");
                loraCmd("radio rx 0");
            }

            if (loraSerial.available() > 0) {
                String resp = loraSerial.readStringUntil('\n');
                resp.trim();

                if (resp.indexOf("radio_rx") == 0) {
                    Serial.println("Downlink in Rx Window: " + resp);
                }
                else if (resp == "ok") {
                    Serial.println("Module is now listening...");
                }
                else if (resp == "radio_err") {
                    Serial.println("Slot Timeout: No signal heard.");
                    loraCmd("radio rxstop");
                    loraCmd("radio rx 0");
                }
                else {
                    Serial.println("Unexpected: " + resp);
                }
            }
            vTaskDelay(3);
        }
        loraCmd("radio rxstop");
        Serial.println("RX window closed");
    }
}
#endif


#ifdef NODE_LIGHT
// ===========================================================================
// RN2483 sleep helpers
// ---------------------------------------------------------------------------
// `sys sleep <ms>` drops the module to ~1.6 µA. The module wakes itself when
// the timer expires, but we always force wake it via break and 0x55 so the
// timing is deterministic and decoupled from the ESP32 wake.
// ===========================================================================
static void radioSleep(uint32_t ms) {
    if (ms < 100) ms = 100;
    String cmd = "sys sleep " + String(ms);
    loraSerial.println(cmd);
    delay(20);  // let the "ok" come back
    while (loraSerial.available()) loraSerial.read();
}

static void radioWake() {
    // Break condition: TX held LOW longer than one character frame
    // (174 µs at 57600). We use 5 ms to be safe and account for any scheduling jitter.
    loraSerial.end();
    pinMode(LORA_TX, OUTPUT);
    digitalWrite(LORA_TX, LOW);
    delay(5);
    digitalWrite(LORA_TX, HIGH);
    delayMicroseconds(100);
    loraSerial.begin(57600, SERIAL_8N1, LORA_RX, LORA_TX);
    loraSerial.setTimeout(2000);

    // Autobaud trigger.
    loraSerial.write(0x55);

    // Drain wake banner / "ok" / leftover bytes.
    delay(50);
    while (loraSerial.available()) loraSerial.read();
}

// ===========================================================================
// Deep-sleep helper => never returns. Wake sources:
//   * Timer        => fires BEACON_WINDOW_MS/2 before the next beacon.
//   * ext0 on PIR  => fires on the level opposite to the current PIR state,
//                    so each PIR transition (rise OR fall) wakes us.
// LED state is held across deep sleep so it tracks PIR even while CPU is off.
// ===========================================================================
static void deepSleepUntilNextBeacon() __attribute__((noreturn));
static void deepSleepUntilNextBeacon() {
    int64_t now = rtcMicros();
    int64_t remaining_us = g_nextBeaconUs - now;
    if (remaining_us < 100000LL) remaining_us = 100000LL;  // floor at 100 ms

    bool motionNow = digitalRead(PIN_MOTION) == HIGH;
    int  wakeLevel = motionNow ? 0 : 1;  // wake on the OPPOSITE of current state
    g_motionAtSleep = motionNow;

    // During the PIR's cold start window the output is glitchy; ext0 would
    // wake light out of deep sleep over and over, and I'd never reach the next beacon. Arm timer only wake
    // until at least one full cycle has elapsed since POR.
    if (g_pirArmed) {
        esp_sleep_enable_ext0_wakeup((gpio_num_t)PIN_MOTION, wakeLevel);
    }
    esp_sleep_enable_timer_wakeup((uint64_t)remaining_us);

    // Latch LED and LoRa RST so their levels survive deep sleep. RST must stay
    // HIGH or the RN2483 resets and our `sys sleep` state is lost.
    rtc_gpio_hold_en((gpio_num_t)PIN_LED);
    rtc_gpio_hold_en((gpio_num_t)LORA_RST);

    // First entry after a beacon round put the radio to sleep too.
    // PIR wakes loop through here repeatedly; don't reissue (radio is asleep).
    if (!g_radioSleeping) {
        loraCmd("radio rxstop");
        // Sleep far longer than any cycle we always force wake explicitly.
        radioSleep((uint32_t)BEACON_INTERVAL_MS * 4);
        g_radioSleeping = true;
        Serial.println("[SLEEP] radio => sys sleep");
    }

    Serial.printf("[SLEEP] %.1f s (PIR=%d, wake on %d, radio=%s)\n",
                  remaining_us / 1.0e6, motionNow, wakeLevel,
                  g_radioSleeping ? "sleeping" : "idle");
    Serial.flush();

    esp_deep_sleep_start();
    while (true) {}  // unreachable
}
#endif  // NODE_LIGHT

// ===========================================================================
// SETUP
// ===========================================================================
void setup() {
    Serial.begin(115200);
    delay(50);

    // Power: kill unused radios and drop the CPU clock (LoRa UART is 57600).
    WiFi.mode(WIFI_OFF);
    btStop();
    setCpuFrequencyMhz(80);

#ifdef NODE_LIGHT
    // -------------------------------------------------------------------
    // LIGHT node deep sleep state machine.
    //   POR / undefined wake => first-listen window, full LoRa init.
    //   Timer wake           => short-listen window, radio stays configured.
    //   ext0 (PIR) wake      => service motion, sleep again until next beacon.
    // -------------------------------------------------------------------
    esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
    Serial.printf("\n===== Node 0x%02X (cause=%d) =====\n", NODE_ID, (int)cause);

    nodeSetup();  // pinModes, ADC, releases LED hold from previous sleep
    rtc_gpio_hold_dis((gpio_num_t)LORA_RST);  // release RST hold from previous sleep

    if (cause == ESP_SLEEP_WAKEUP_EXT0) {
        Serial.println("[WAKE] PIR");
        // ext0 was armed for the level opposite to g_motionAtSleep, so the
        // new state is its negation. Avoids rereading a bouncing pin.
        lightHandlePIRWake(!g_motionAtSleep);
        deepSleepUntilNextBeacon();  // never returns
    }

    // POR or timer wake => run a beacon round.
    bool firstBoot = !g_loraInitedOnce;
    if (firstBoot) {
        Serial.println("[WAKE] POR initialising LoRa");
        if (!loraInit()) {
            Serial.println("[ERROR] LoRa init failed sleeping 10 s and retrying");
            g_nextBeaconUs = rtcMicros() + 10LL * 1000000LL;
            deepSleepUntilNextBeacon();
        }
        g_loraInitedOnce = true;
        g_radioSleeping = false;
    } else {
        Serial.println("[WAKE] Timer waking radio from sys sleep");
        // UART driver was torn down by deep sleep; radioWake begins again
        radioWake();
        g_radioSleeping = false;
        loraCmd("radio rxstop");  // drain any leftover output
        // One full sleep cycle (~BEACON_INTERVAL_MS) has elapsed since POR
        // the HCSR501 has had time to settle, so we can now wake on PIR edges without getting trapped in a wake loop.
        g_pirArmed = true;
    }

    // Sync LED to current PIR and let the awake window ISR track edges.
    digitalWrite(PIN_LED, digitalRead(PIN_MOTION) == HIGH ? HIGH : LOW);
    attachInterrupt(digitalPinToInterrupt(PIN_MOTION), pirEdgeISR, CHANGE);

    if (g_pendingTx) {
        // ---------------------------------------------------------------
        // TX phase  we deep sleep through the slot wait. The radio still
        // holds the listen config which sys sleep preserves, so just TX.
        // ---------------------------------------------------------------
        Serial.println("[WAKE] TX phase");
        g_pendingTx = false;

        uint8_t buf[16];
        uint8_t len = 0;
        nodeBuildPayload(NODE_ID, buf, &len);

        char sending[64];
        sprintf(sending, "radio tx %02X%02X%02X%02X%02X%02X%02X%02X",
                buf[0], buf[1], buf[2], buf[3],
                buf[4], buf[5], buf[6], buf[7]);

        Serial.println("--- TX window ---");
        TickType_t period = xTaskGetTickCount();
        while (xTaskGetTickCount() < period + pdMS_TO_TICKS(TX_WINDOW_MS)) {
            loraSerial.println(sending);
            loraSerial.readStringUntil('\n');
            loraSerial.readStringUntil('\n');
            delay(10);
        }

        loraCmd("radio rx 0");

        Serial.println("--- ACK window ---");
        period = xTaskGetTickCount();
        while (xTaskGetTickCount() < period + pdMS_TO_TICKS(ACK_WINDOW_MS)) {
            if (loraSerial.available() > 0) {
                String s = loraSerial.readStringUntil('\n');
                s.trim();
                if (s.indexOf("radio_rx") == 0)      Serial.println("ACK: " + s);
                else if (s == "radio_err")          Serial.println("Slot timeout");
                else if (s.length() > 0)            Serial.println("RX: " + s);
            }
            delay(5);
        }
    } else {
        // ---------------------------------------------------------------
        // Listen phase: POR or scheduled timer wake for next beacon.
        // ---------------------------------------------------------------
        uint32_t window = firstBoot ? FIRST_LISTEN_MS : BEACON_WINDOW_MS;
        bool got = listenForBeacon(window);
        int64_t beacon_ref_us = rtcMicros();

        if (got) {
            g_lastBeaconUs = beacon_ref_us;  // anchor cadence to last real beacon
            g_pendingTx = true;
            // Schedule a deep sleep wake at the start of our TX slot. The
            // gateway measures the slot from the beacon transmission, which
            // is a few tens of ms before beacon_ref_us... so close enough.
            g_nextBeaconUs = beacon_ref_us + (int64_t)g_transSlotMs * 1000LL;
            Serial.printf("[NODE 0x%02X] beacon OK, sleeping %u ms until TX slot\n",
                          NODE_ID, g_transSlotMs);
        } else {
            Serial.println("[BEACON] Missed: retrying at next frame");
        }
    }

    detachInterrupt(digitalPinToInterrupt(PIN_MOTION));

    // After TX phase or a missed listen, schedule the next BEACON listen by
    // projecting from the last KNOWN beacon in FRAME_SIZE_MS steps and waking
    // BEACON_WINDOW_MS/2 early to give the radio time to wake and settle before the beacon arrives. If
    // A successful listen already set g_nextBeaconUs above; skip rescheduling.
    if (!g_pendingTx) {
        int64_t now_us         = rtcMicros();
        int64_t period_us      = (int64_t)FRAME_SIZE_MS     * 1000LL;
        int64_t half_window_us = (int64_t)BEACON_WINDOW_MS  * 1000LL / 2;

        if (g_lastBeaconUs > 0) {
            int64_t next = g_lastBeaconUs + period_us;
            while (next - half_window_us < now_us) next += period_us;
            g_nextBeaconUs = next - half_window_us;
        } else {
            // No beacon ever heard and we need to retry sooner than a full period.
            g_nextBeaconUs = now_us + period_us / 2;
        }
    }

    deepSleepUntilNextBeacon();  // never returns
#else
    // -------------------------------------------------------------------
    // Non-LIGHT nodes: task based forever loop.
    // -------------------------------------------------------------------
    delay(450);
    Serial.printf("\n===== Node 0x%02X Starting =====\n", NODE_ID);
    Serial.printf("[PWR] WiFi/BT off, CPU @ %u MHz\n", getCpuFrequencyMhz());

    nodeSetup();

    if (!loraInit()) {
        Serial.println("[ERROR] LoRa init failed — halting");
        while (true) delay(1000);
    }
    Serial.println("[LORA] Init OK");

    g_payloadMutex = xSemaphoreCreateMutex();
    nodeBuildPayload(NODE_ID, g_payload, &g_payloadLen);
    for (int i = 0; i < g_payloadLen; i++) {
        Serial.printf("%02X ", g_payload[i]);
    }

    xTaskCreatePinnedToCore(Comms_TaskManager, "Comms", 10000, NULL, 2, &Comms_TaskHandle, 1);
    xTaskCreatePinnedToCore(Rx_TaskManager,    "Rx",    10000, NULL, 1, &Rx_TaskHandle,    1);
#endif
}

void loop() {}
