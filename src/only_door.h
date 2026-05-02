/**
 * lock_node.h — Smart Lock Node
 * Sensors: RFID reader (SPI), keypad (GPIO)
 * Actuator: relay for door latch
 */

#pragma once
#include <Arduino.h>
#include <SPI.h>
#include <MFRC522.h>
#include <Keypad.h>
#include "mbedtls/gcm.h"

// -----------------------------------------------------------------------------
// Pin definitions 
// -----------------------------------------------------------------------------
#define PIN_RELAY      15  // Αποφεύγουμε το 14 λόγω Keypad
#define PIN_RFID_SS    5   
#define PIN_RFID_RST   22  

// --- Keypad Configuration ---
const byte ROWS = 4; const byte COLS = 4;
char keys[ROWS][COLS] = {
  {'1','2','3','A'}, {'4','5','6','B'}, {'7','8','9','C'}, {'*','0','#','D'}
};
byte rowPins[ROWS] = {13, 12, 14, 27}; 
byte colPins[COLS] = {26, 25, 33, 32};
Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);

// --- RFID Configuration ---
MFRC522 mfrc522(PIN_RFID_SS, PIN_RFID_RST);

struct User { String uid; String pin; };
User authorizedUsers[] = { {"A36C11DA", "1234"} }; 

// -----------------------------------------------------------------------------
// Internal state & Counters
// -----------------------------------------------------------------------------
static uint8_t success_openings = 0;
static uint8_t unsuccessful_attempts = 0;

enum SystemState { SYS_IDLE, SYS_WAITING_FOR_PIN }; 
SystemState currentState = SYS_IDLE; 
String enteredPin = ""; 
int failedPinAttempts = 0; 
int currentUserIndex = -1; 
unsigned long lastKeyTime = 0; 
const unsigned long KEYPAD_TIMEOUT = 10000; 

// --- AES-GCM Crypto Variables ---
const uint8_t secret_key[16] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10};
uint16_t lastAcceptedSequence = 0;

// -----------------------------------------------------------------------------
// Helpers
// -----------------------------------------------------------------------------
void resetSystem() { 
    currentState = SYS_IDLE; enteredPin = ""; failedPinAttempts = 0; currentUserIndex = -1; 
}

void triggerUnlock() {
    Serial.println("\n[DOOR] >>> UNLOCKING DOOR...");
    success_openings++; // Σωρευτική αύξηση
    
    digitalWrite(PIN_RELAY, HIGH);   // Άνοιγμα ρελέ
    vTaskDelay(pdMS_TO_TICKS(2000)); // Παραμένει ανοιχτό για 2 δευτερόλεπτα
    digitalWrite(PIN_RELAY, LOW);    // Κλείδωμα ξανά
}

// -----------------------------------------------------------------------------
// Public API (Called by main.cpp)
// -----------------------------------------------------------------------------
void nodeSetup() {
    pinMode(PIN_RELAY, OUTPUT);
    digitalWrite(PIN_RELAY, LOW);   // start locked

    SPI.begin();
    mfrc522.PCD_Init();
    resetSystem();

    Serial.println("[LOCK] Hardware init OK (locked)");
}

// Χτίζει το Uplink Payload (3 Bytes) και ελέγχει τους σένσορες
void nodeBuildPayload(uint8_t nodeId, uint8_t* buf, uint8_t* len) {
    
    // --- 1. ΕΚΤΕΛΕΣΗ ΛΟΓΙΚΗΣ ΠΟΡΤΑΣ (POLLING) ---
    if (currentState == SYS_WAITING_FOR_PIN && (millis() - lastKeyTime > KEYPAD_TIMEOUT)) {
        Serial.println("[SECURITY] PIN Timeout."); resetSystem();
    }

    char key = keypad.getKey();
    if (key) {
        lastKeyTime = millis();
        if (key == '#') {
            if (currentState == SYS_WAITING_FOR_PIN) {
                if (enteredPin == authorizedUsers[currentUserIndex].pin) {
                    triggerUnlock();
                    resetSystem();
                } else {
                    failedPinAttempts++; enteredPin = "";
                    Serial.printf("[DOOR] Wrong PIN (%d/3)\n", failedPinAttempts);
                    vTaskDelay(pdMS_TO_TICKS(2000)); 
                    if (failedPinAttempts >= 3) { unsuccessful_attempts++; resetSystem(); }
                }
            }
        } else if (key == '*') { resetSystem(); }
        else { enteredPin += key; }
    }

    if (currentState == SYS_IDLE && mfrc522.PICC_IsNewCardPresent() && mfrc522.PICC_ReadCardSerial()) {
        String scannedUid = "";
        for (byte i = 0; i < mfrc522.uid.size; i++) {
            scannedUid += String(mfrc522.uid.uidByte[i] < 0x10 ? "0" : "");
            scannedUid += String(mfrc522.uid.uidByte[i], HEX);
        }
        scannedUid.toUpperCase(); mfrc522.PICC_HaltA();
        
        if (scannedUid == authorizedUsers[0].uid) { 
            currentUserIndex = 0; currentState = SYS_WAITING_FOR_PIN; lastKeyTime = millis(); 
            Serial.println("[DOOR] Card OK. Enter PIN.");
        } else { 
            unsuccessful_attempts++; Serial.println("[SECURITY] Invalid Card."); 
        }
    }

    // --- 2. ΔΗΜΙΟΥΡΓΙΑ PAYLOAD (Ακριβώς 3 Bytes) ---
    buf[0] = nodeId;
    buf[1] = success_openings;
    buf[2] = unsuccessful_attempts;
    
    *len = 3; 
}

// Λαμβάνει και αποκρυπτογραφεί τα 11 bytes του Downlink
void nodeHandleDownlink(const uint8_t* packet, uint8_t len) {
    if (len < 11) {
        Serial.println("[SECURITY] Error: Downlink Packet too short");
        return;
    }

    uint8_t nonceShort[2];
    uint8_t ciphertext[4];
    uint8_t authTag[4];

    memcpy(nonceShort, &packet[1], 2);
    memcpy(ciphertext, &packet[3], 4);
    memcpy(authTag,    &packet[7], 4);

    uint8_t fullIv[12] = {0};
    memcpy(fullIv, nonceShort, 2);

    mbedtls_gcm_context gcm; 
    mbedtls_gcm_init(&gcm); 
    mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, secret_key, 128);  

    uint8_t decryptedOutput[4]; 
    int ret = mbedtls_gcm_auth_decrypt(&gcm, 4, fullIv, 12, NULL, 0, authTag, 4, ciphertext, decryptedOutput);

    if (ret != 0) {
        Serial.println("[SECURITY] CRITICAL: AUTHENTICATION FAILED! Message tampered or wrong key.");
        mbedtls_gcm_free(&gcm);
        return;
    }

    uint16_t receivedSeq = (decryptedOutput[0] << 8) | decryptedOutput[1];
    uint16_t receivedCmd = (decryptedOutput[2] << 8) | decryptedOutput[3];

    if (receivedSeq <= lastAcceptedSequence) {
        Serial.println("[SECURITY] REPLAY ATTACK: Old sequence number received.");
        mbedtls_gcm_free(&gcm);
        return;
    }

    lastAcceptedSequence = receivedSeq;
    Serial.printf("[SECURITY] Valid Encrypted Command! Seq: %u, Cmd: 0x%04X\n", receivedSeq, receivedCmd);

    if (receivedCmd == 0x00FF) { 
        triggerUnlock(); 
    }

    mbedtls_gcm_free(&gcm);
}