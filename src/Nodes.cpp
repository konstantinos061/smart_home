#include <Arduino.h>

#define  NODE_ID 0
#define NEW_BEACON 595000
uint8_t trans_slot = (NODE_ID * 10000) + 10000;

// TDMA configuration
#define BEACON_INTERVAL_MS   60000
#define WINDOW_1_OFFSET_MS   5000
#define WINDOW_2_OFFSET_MS   15000
#define WINDOW_3_OFFSET_MS   35000
#define WINDOW_DURATION_MS   5000
#define BEACON_WINDOW_MS     3000
#define FIRST_LISTEN_MS      300000  // 5 minutes — guaranteed to catch first beacon

// Simulated node IDs
#define NODE_THERMOSTAT  0x02
#define NODE_LIGHTING    0x03
#define NODE_SECURITY    0x04

// LoRa UART
#define LORA_TX   17
#define LORA_RX   16
#define LORA_RST  21
#define LORA_FREQ "869100000"
#define LORA_SF   "sf7"
#define LORA_BW   "125"
#define LORA_CR   "4/5"
#define LORA_AFCBW "41.7"
#define LORA_PWR  "14"
#define LORA_SYNC "12"

#define TX_WINDOW 2000
#define ACK_WINDOW 1000

//TODO::Add the logic just for the specific sensor and also the constant reading on other frequency - maybe a lower priority task!!

HardwareSerial loraSerial(2);

TaskHandle_t Comms_TaskHandle = NULL;
TaskHandle_t SensorTaskHandle = NULL;

// -----------------------------------------------------------------------------
// LoRa helpers
// -----------------------------------------------------------------------------
static String loraCmd(const String& cmd, uint32_t timeoutMs = 1000) {
    loraSerial.println(cmd);
    loraSerial.flush();
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
    digitalWrite(LORA_RST, LOW); delay(100);
    digitalWrite(LORA_RST, HIGH); delay(500);

    loraSerial.begin(57600, SERIAL_8N1, LORA_RX, LORA_TX);
    loraSerial.setTimeout(2000);
    delay(300);
    if (loraSerial.available()) loraSerial.readStringUntil('\n');

    loraCmd("mac pause", 1000);

    String r;
    r = loraCmd("radio set mod lora");        if (r != "ok") { Serial.println("[LORA] mod lora -> " + r); return false; }
    r = loraCmd("radio set freq " LORA_FREQ); if (r != "ok") { Serial.println("[LORA] freq -> "     + r); return false; }
    r = loraCmd("radio set pwr "  LORA_PWR);  if (r != "ok") { Serial.println("[LORA] pwr -> "      + r); return false; }
    r = loraCmd("radio set sf "   LORA_SF);   if (r != "ok") { Serial.println("[LORA] sf -> "       + r); return false; }
    r = loraCmd("radio set afcbw "   LORA_AFCBW);   if (r != "ok") { Serial.println("[LORA] afcbw -> "       + r); return false; }
    r = loraCmd("radio set rxbw "   LORA_BW);   if (r != "ok") { Serial.println("[LORA] rxbw -> "       + r); return false; }
    r = loraCmd("radio set prlen 8");          if (r != "ok") { Serial.println("[LORA] prlen -> "     + r); return false; }
    r = loraCmd("radio set crc on");           if (r != "ok") { Serial.println("[LORA] crc -> "      + r); return false; }
    r = loraCmd("radio set iqi off");          if (r != "ok") { Serial.println("[LORA] iqi -> "      + r); return false; }
    r = loraCmd("radio set cr "   LORA_CR);   if (r != "ok") { Serial.println("[LORA] cr -> "       + r); return false; }
    r = loraCmd("radio set sync " LORA_SYNC); if (r != "ok") { Serial.println("[LORA] sync -> "     + r); return false; }
    r = loraCmd("radio set bw "   LORA_BW);   if (r != "ok") { Serial.println("[LORA] bw -> "       + r); return false; }
    return true;
}

// -----------------------------------------------------------------------------
// Listen for gateway beacon (first byte 0xFF)
// -----------------------------------------------------------------------------
static bool listenForBeacon(uint32_t windowMs) {
    Serial.printf("[BEACON] Listening for %u ms...\n", windowMs);
    loraSerial.println("radio rx 0");
    loraSerial.flush();

    unsigned long start = millis();
    while (millis() - start < windowMs) {
        if (loraSerial.available()) {
            String resp = loraSerial.readStringUntil('\n');
            resp.trim();
            if (resp.startsWith("radio_rx")) {
                int spaceIdx = resp.lastIndexOf(' ');
                if (spaceIdx >= 0) {
                    String hex = resp.substring(spaceIdx + 1);
                    if (hex.startsWith("53")) {
                        loraSerial.println("radio rxstop");
                        delay(50);
                        Serial.println("[BEACON] Received!");
                        return true;
                    }
                }
            }
        }
        delay(5);
    }

    loraSerial.println("radio rxstop");
    delay(50);
    Serial.println("[BEACON] Not received");
    return false;
}

// -----------------------------------------------------------------------------
// Send a packet for a given node
// -----------------------------------------------------------------------------
static void sendPacket(uint8_t nodeId, float temp, float humidity,
                       int setpt, int battPct) {
    uint8_t flags = (battPct <= 20) ? 0x01 : 0x00;
    uint8_t payload[7] = {
        nodeId, 0x01,
        (uint8_t)(temp * 2),
        (uint8_t)humidity,
        (uint8_t)setpt,
        (uint8_t)battPct,
        flags
    };

    String cmd = "radio tx ";
    for (uint8_t b : payload) {
        char hex[3];
        sprintf(hex, "%02X", b);
        cmd += hex;
    }

    String resp = loraCmd(cmd, 500);
    if (resp != "ok") {
        Serial.printf("[NODE 0x%02X] TX start failed: %s\n", nodeId, resp.c_str());
        return;
    }

    loraSerial.setTimeout(3000);
    resp = loraSerial.readStringUntil('\n');
    resp.trim();
    if (resp == "radio_tx_ok") {
        Serial.printf("[NODE 0x%02X] Sent: temp=%.1f hum=%.0f setpt=%d batt=%d flags=0x%02X\n",
                      nodeId, temp, humidity, setpt, battPct, flags);
    } else {
        Serial.printf("[NODE 0x%02X] TX failed: %s\n", nodeId, resp.c_str());
    }
}

void Comms_TaskManager(void * pvParameters){
    //Chnage this to use maybe a FreeRTOS - one Core for communication another for tempreature stuff
    static bool firstListen = true;
    TickType_t through_way_copy;
    TickType_t period;
    while(1){
        Serial.println("--- Waiting for beacon ---");

        // On first call listen for up to 5 minutes so we always catch the beacon
        // regardless of where we are in the 60 s cycle.
        // After that use the normal 600 ms window timed to the cycle.
        bool beaconReceived = false;

        while (!beaconReceived) {
            uint32_t listenWindow = firstListen ? FIRST_LISTEN_MS : BEACON_WINDOW_MS;
            beaconReceived = listenForBeacon(listenWindow);
            firstListen = false;

            if (!beaconReceived) {
                Serial.println("[BEACON] Retrying in 60 s...");
                delay(BEACON_INTERVAL_MS);
                if (!loraInit()) {
                    Serial.println("[ERROR] LoRa re-init failed");
                }
            }
        }
        TickType_t start_time = xTaskGetTickCount();
        through_way_copy = start_time;
        xTaskDelayUntil(&through_way_copy, pdMS_TO_TICKS(trans_slot));

        Serial.println("\n--- Handling windows ---");

        
        Serial.println("\n--- First Sending window ---");
        period = xTaskGetTickCount();
        while(millis() < period + TX_WINDOW) {
            
            loraSerial.println("radio tx AAAAAAAAAA");
            loraSerial.readStringUntil('\n');
            loraSerial.readStringUntil('\n');

            vTaskDelay(10);
        }

        loraSerial.println("radio rx 0");

        //Read ACK
        Serial.println("\n--- Listening for ACK ---");
        period = xTaskGetTickCount();
        while(millis() < period + ACK_WINDOW) {

            
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
        through_way_copy = start_time;
        xTaskDelayUntil(&through_way_copy, pdMS_TO_TICKS(NEW_BEACON));

    }
}
/*
void Sensor_TaskManager(void * pvParameters){
    //ADD here the code to deal with actual sensor reading and other stuff
    
}
    */

// =============================================================================
// SETUP
// =============================================================================
void setup() {
    Serial.begin(115200);
    delay(500);
    

    Serial.println("\n===== TDMA Multi-Node Simulator =====");
    Serial.println("Windows: +5s (thermostat), +15s (lighting), +35s (security)");
    Serial.println("=====================================\n");

    if (!loraInit()) {
        Serial.println("[ERROR] LoRa init failed — halting");
        while (true) delay(1000);
    }
    Serial.println("[LORA] Init OK\n");

    //Create the task!
  xTaskCreatePinnedToCore(
    Comms_TaskManager,         // Task function
    "Comms_TaskManager",       // Task name
    10000,             // Stack size (bytes)
    NULL,              // Parameters
    1,                 // Priority
    &Comms_TaskHandle,  // Task handle
    1                  
  );

  /*
   //Create the task!
  xTaskCreatePinnedToCore(
    Sensor_TaskManager,         // Task function
    "Sensor_TaskManager",       // Task name
    10000,             // Stack size (bytes)
    NULL,              // Parameters
    1,                 // Priority
    &SensorTaskHandle,  // Task handle
    0                
  );
  */
}

// =============================================================================
// LOOP — one full 60 s cycle per iteration
// =============================================================================
void loop() {
    
}