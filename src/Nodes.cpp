#include <Arduino.h>

// ---------------------------------------------------------------------------
// Node-type selection — set exactly one of these in build_flags:
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
#define BEACON_INTERVAL_MS  60000
#define BEACON_WINDOW_MS    3000
#define FIRST_LISTEN_MS     300000   // 5 min — always catches first beacon
#define TX_WINDOW_MS        2000
#define ACK_WINDOW_MS       2000
#define NEW_BEACON_MS       59500   // wait before re-entering beacon search
#define SENSOR_PERIOD_MS    30000    // how often the sensor task reads

// ---------------------------------------------------------------------------
// LoRa UART
// ---------------------------------------------------------------------------
#define LORA_TX    17
#define LORA_RX    16
#define LORA_RST   21
#define LORA_FREQ  "868100000"
#define LORA_FREQ_RX "868300000"
#define LORA_SF    "sf7"
#define LORA_BW    "125"
#define LORA_CR    "4/5"
#define LORA_AFCBW "41.7"
#define LORA_PWR   "14"
#define LORA_SYNC  "12"

HardwareSerial loraSerial(2);

TaskHandle_t Comms_TaskHandle  = NULL;
TaskHandle_t Sensor_TaskHandle = NULL;
TaskHandle_t Rx_TaskHandle     = NULL;

// Shared payload: Sensor task writes, Comms task reads
static uint8_t           g_payload[16];
static uint8_t           g_payloadLen = 0;
static SemaphoreHandle_t g_payloadMutex = NULL;

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
    if (loraSerial.available()) loraSerial.readStringUntil('\n');

    loraCmd("mac pause", 1000);

    String r;
    r = loraCmd("radio set mod lora");         if (r != "ok") { Serial.println("[LORA] mod lora -> "  + r); return false; }
    r = loraCmd("radio set freq " LORA_FREQ);  if (r != "ok") { Serial.println("[LORA] freq -> "      + r); return false; }
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
// Listen for gateway beacon (payload starts with 0x53 = 'S')
// ---------------------------------------------------------------------------
static bool listenForBeacon(uint32_t windowMs) {
    Serial.printf("[BEACON] Listening for %u ms...\n", windowMs);
    loraSerial.println("radio rxstop");
    loraSerial.readStringUntil('\n');

    loraSerial.println("radio rx 0");

    TickType_t start = xTaskGetTickCount();
    while (xTaskGetTickCount() < start + pdMS_TO_TICKS(windowMs)) {
        if (loraSerial.available() > 0) {
            String resp = loraSerial.readStringUntil('\n');
            resp.trim();
            if (resp.startsWith("radio_rx")) {
                int spaceIdx = resp.lastIndexOf(' ');
                if (spaceIdx >= 0 && resp.substring(spaceIdx + 1).startsWith("53")) {
                    loraSerial.println("radio rxstop");
                    vTaskDelay(pdMS_TO_TICKS(50));
                    Serial.println("[BEACON] Received!");
                    return true;
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }

    loraSerial.println("radio rxstop");
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
// Sensor task — reads sensors every SENSOR_PERIOD_MS and updates shared buf
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

String hex_to_string(const uint8_t* buf, uint8_t len) {
    String hex;
    for (uint8_t i = 0; i < len; i++) {
        char h[3];
        sprintf(h, "%02X", buf[i]);
        hex += h;
    }
    return hex;
}

// ---------------------------------------------------------------------------
// Comms task — TDMA: beacon → wait for slot → TX → listen ACK → repeat
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
        memcpy(buf, g_payload, g_payloadLen);
        len = g_payloadLen;

        //Serial.printf("[NODE %d] Payload ready (%d bytes)\n", NODE_ID, len);

        Serial.println("\n--- Handling windows ---");

        for (int i = 0; i < g_payloadLen; i++) {
        Serial.printf("%X ", g_payload[i]);
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
// ===========================================================================
// Rx task — listens for downlinks and dispatches to node handler
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
                loraCmd("radio rx 0");  // ← just restart, don't break
            }

            if (loraSerial.available() > 0) {
                Serial.println("waiting for any downlink");
                String resp = loraSerial.readStringUntil('\n');
                resp.trim();

                if (resp.indexOf("radio_rx") == 0) {
                    Serial.println("Shits in Rx Window: " + resp);
                    
                }
                else if (resp == "ok") {
                    Serial.println("Module is now listening...");
                } 
                else if (resp == "radio_err") {
                    Serial.println("Slot Timeout: No signal heard.");
                }
                else {
                    // Catch-all for weird garbage
                    Serial.println("Unexpected: " + resp);
                }
            }
            vTaskDelay(3);
        }
        loraCmd("radio rxstop");
         Serial.println("RX window closed");
        }
        vTaskDelay(5);
}


// ===========================================================================
// SETUP
// ===========================================================================
void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.printf("\n===== Node 0x%02X Starting =====\n", NODE_ID);

    // Node-specific hardware init
    //nodeSetup();

    if (!loraInit()) {
        Serial.println("[ERROR] LoRa init failed — halting");
        while (true) delay(1000);
    }
    Serial.println("[LORA] Init OK");

    g_payloadMutex = xSemaphoreCreateMutex();

    // Prime the payload buffer before the comms task starts
    nodeBuildPayload(NODE_ID, g_payload, &g_payloadLen);
    for (int i = 0; i < g_payloadLen; i++) {
        Serial.printf("%02X ", g_payload[i]);
    }

    xTaskCreatePinnedToCore(Comms_TaskManager,  "Comms",  10000, NULL, 2, &Comms_TaskHandle,  1);
    xTaskCreatePinnedToCore(Rx_TaskManager,     "Rx",   10000, NULL, 1, &Rx_TaskHandle,     1);
    //xTaskCreatePinnedToCore(Sensor_TaskManager, "Sensor",  8000, NULL, 1, &Sensor_TaskHandle, 0);
} 

// ===========================================================================
// LOOP — all work is done in the two FreeRTOS tasks above
// ===========================================================================
void loop() {}
