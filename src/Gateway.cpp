#include <Arduino.h>
#include <rn2xx3.h>
#include <HardwareSerial.h>

#define RECEPTION_TIME 10000
#define FRAME_SIZE 60000
#define READING_WINDOW 2000
#define ACK_WINDOW 1000
#define GUARD_TIME 500
#define BEACON_TIME 1000
#define RST 21

#define DOWNLINK_RETRIES  3

#define MAX_PAYLOAD_SIZE 64

uint8_t sharedPayload[MAX_PAYLOAD_SIZE];

uint8_t DownlinkPayload[MAX_PAYLOAD_SIZE];

uint8_t payloadLength = 0;




SemaphoreHandle_t payloadMutex;

//LoraWAn config
#define RESET 23
HardwareSerial loraWAN(1);
rn2xx3 myLoraWAN(loraWAN);


String str;
HardwareSerial loraTDMA(2);

struct Node {
    String devAddr;     
    uint8_t slotTime;
    int ID; 
    byte Device_Type;      
};

uint8_t current_min_freeSlot = 4;

Node network[] = {
    {"DOOR_01", 1, 0},    
    {"TEMP_01", 2, 32},   
    {"LIGHT_01", 3,0}    
};

int totalNodes = sizeof(network) / sizeof(network[0]);

TaskHandle_t TDMATaskHandle = NULL;
TaskHandle_t LoraWANTaskHandle = NULL;
TaskHandle_t DownLinkTaskHandle = NULL;

void Add_New_Node(uint8_t NodeID){

  byte DeviceT = (NodeID >> 5) & 0x07;
  int currentLength = sizeof(network) / sizeof(network[0]);

  network[++currentLength] = {"OLA",current_min_freeSlot,NodeID, DeviceT};

}

void Send_ACK(TickType_t Starting_time_window){

  Serial.println("Sending ACK!");

  while(loraTDMA.available()) { loraTDMA.read(); } //clear the Lora

  while(xTaskGetTickCount() < (Starting_time_window + pdMS_TO_TICKS(ACK_WINDOW))){
    loraTDMA.println("radio tx 41434B"); //ACK

    str = loraTDMA.readStringUntil('\n');
    str = loraTDMA.readStringUntil('\n');

    vTaskDelay(100);
  }

}


void TDMA_TaskManager(void * pvParameters){
 
  while(1){
    //First have to send the syncrhonization beacon every 60 seconds and then listen for the health informations

    Serial.println("Start Timer");
    TickType_t start_time = xTaskGetTickCount();

    TickType_t through_way_copy;
    TickType_t period;
    while (xTaskGetTickCount() < (start_time + pdMS_TO_TICKS(BEACON_TIME)))
    {

      loraTDMA.println("radio tx 53594E435F424541434F4E"); //First send the beacon to everyone beacon = SYNC_BEACON
      str = loraTDMA.readStringUntil('\n');
      Serial.println("Lora module confirmed the recption of the instruction to send" + str); //Message stating ok I will start sending 

      str = loraTDMA.readStringUntil('\n');
      Serial.println("Becon as been sent: " + str); //Message sating I started the actual sending
      vTaskDelay(200);
    }

    for (int i = 0; i < totalNodes; i++) {
        bool received_data = false; 
        int targetWake = network[i].slotTime * RECEPTION_TIME - GUARD_TIME;
        
        
        through_way_copy = start_time;
        xTaskDelayUntil(&through_way_copy, pdMS_TO_TICKS(targetWake));

        Serial.printf("Slot %d: Waiting for %s\n", i, network[i].devAddr.c_str());

        loraTDMA.println("radio rxstop");
        loraTDMA.readStringUntil('\n'); //clear the ok form this command

        loraTDMA.println("radio rx 0"); 
        
        

        period = xTaskGetTickCount();
        while (xTaskGetTickCount() < (period + pdMS_TO_TICKS(READING_WINDOW))){

          if(loraTDMA.available() > 0){
            str = loraTDMA.readStringUntil('\n');
            Serial.println("str from lora: " + str);
            str.trim();

            if (str.indexOf("radio_rx") == 0) {

              const char* hexStart = str.c_str() + 10;
              int hexStringLen = strlen(hexStart);
              
              Serial.println("Success! Data: " + str);
              received_data = true;

              if (xSemaphoreTake(payloadMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
                  
                  payloadLength = 0;
                  for (int i = 0; i + 1 < hexStringLen; i += 2) {
                      char byteStr[3] = { hexStart[i], hexStart[i+1], '\0' };
                      sharedPayload[payloadLength++] = (uint8_t) strtol(byteStr, nullptr, 16);

                      if (payloadLength >= MAX_PAYLOAD_SIZE) break;
                  }
                  
                  xSemaphoreGive(payloadMutex);
              }
              if (LoraWANTaskHandle != NULL) {
                xTaskNotifyGive(LoraWANTaskHandle);
              }

              break;
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
          vTaskDelay(pdMS_TO_TICKS(5));

        }
        xTaskDelayUntil(&period, pdMS_TO_TICKS(READING_WINDOW));
        period = xTaskGetTickCount();
        if(received_data){Send_ACK(period);}
        
    }
    
    loraTDMA.println("radio rxstop");
    through_way_copy = start_time;
    xTaskDelayUntil(&through_way_copy, pdMS_TO_TICKS(FRAME_SIZE));

  }
}

/*

void Downlink_TaskManager(void * pvParameters){
 
  while(1){

    ulTaskNotifyTake(pdTRUE,portMAX_DELAY);

      Serial.println("Sending downlink");
      loraTDMA.println("radio rxstop");
      loraTDMA.readStringUntil('\n');
      
      for(int tries = 0; tries < DOWNLINK_RETRIES; tries++){
        
        loraTDMA.println("radio tx AAAAAA");

        loraTDMA.readStringUntil('\n');
        loraTDMA.readStringUntil('\n');

        vTaskDelay(1000);
      }

    vTaskDelay(50);
}
    
}
*/

void LoraWAN_TaskManager(void * pvParameters){

  loraWAN.println("mac set class c");
  String class_resp = loraWAN.readStringUntil('\n');

  loraWAN.println("mac get class");
  String LoraWanClass = loraWAN.readStringUntil('\n');
  LoraWanClass.trim();
  Serial.println("LoRaWAN set to Class C: " + LoraWanClass);

  loraWAN.println("mac tx uncnf 1 000");

  while(1){


    if(loraWAN.available() > 0){
      String incoming = loraWAN.readStringUntil('\n');
      incoming.trim();
      Serial.println("Data from LoraWAN " + incoming);
 
      const char* a = incoming.c_str() + 9;

      char ID[3] = {a[0], a[1], '\0'};

      uint8_t actual_ID = (uint8_t)strtol(ID, nullptr, 16);

      Serial.print("Node ID of LoraWANMsg: ");
      Serial.println(actual_ID);

      bool new_node = true;

      //Loop to check if the node is new
      for(int i = 0; i < sizeof(network) / sizeof(network[0]); i++){
        if(network[i].ID == actual_ID) new_node = false;
      }
      if(new_node) Add_New_Node(actual_ID);

      //check if the message if for thermostat or door different behavior 
    
      a+=2;

      int hexStringLen = strlen(a);

      payloadLength = 0;
      for (int i = 0; i + 1 < hexStringLen; i += 2) {
          char byteStr[3] = { a[i], a[i+1], '\0' };
          DownlinkPayload[payloadLength++] = (uint8_t) strtol(byteStr, nullptr, 16);

          if (payloadLength >= MAX_PAYLOAD_SIZE) break;
      }
      /*
      if (DownLinkTaskHandle != NULL) {
        xTaskNotifyGive(DownLinkTaskHandle);
      }
        
      */
    }
      

    if(ulTaskNotifyTake(pdTRUE,0) > 0){
      Serial.println("TDMA Task signaled me! Time to send Uplink.");

      if(payloadLength > 0){
          
        if (xSemaphoreTake(payloadMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            
            myLoraWAN.txBytes(sharedPayload, payloadLength);

            payloadLength = 0; 

            xSemaphoreGive(payloadMutex);
          }
      
        
      }
    }

    vTaskDelay(10);
  
  }
  
}


void initialize_LoraWAN_Radio()
{
  //reset RN2xx3
  pinMode(RESET, OUTPUT);
  digitalWrite(RESET, LOW);
  delay(100);
  digitalWrite(RESET, HIGH);

  delay(100); //wait for the RN2xx3's startup message
  loraWAN.flush();

  //check communication with radio
  String hweui = myLoraWAN.hweui();
  while(hweui.length() != 16)
  {
    Serial.println("Communication with RN2xx3 unsuccessful. Power cycle the board.");
    Serial.println(hweui);
    delay(10000);
    hweui = myLoraWAN.hweui();
  }

  //print out the HWEUI so that we can register it via ttnctl
  Serial.println("When using OTAA, register this DevEUI: ");
  Serial.println(hweui);
  Serial.println("RN2xx3 firmware version:");
  Serial.println(myLoraWAN.sysver());

  //configure your keys and join the network
  Serial.println("Trying to join ChirpStack");
  bool join_result = false;

  join_result = myLoraWAN.initOTAA("0000000000000000", "50e181018fd18b956a549e647699b288");

  while(!join_result)
  {
    Serial.println("Unable to join. Are your keys correct, and do you have TTN coverage?");
    delay(10000); //delay a minute before retry
    join_result = myLoraWAN.initOTAA("0000000000000000", "50e181018fd18b956a549e647699b288");
  }
  Serial.println("Successfully joined LoraWAN");

}


void setup() {
  // start the serial monitor at the speed we set in the ini file
  Serial.begin(19200);

  pinMode(RST, OUTPUT);

  loraTDMA.begin(57600, SERIAL_8N1, 16, 17);

  payloadMutex = xSemaphoreCreateMutex();

  loraWAN.begin(57600, SERIAL_8N1, 18, 19);

  initialize_LoraWAN_Radio();

  digitalWrite(RST, LOW);
  delay(200);
  digitalWrite(RST, HIGH);


  loraTDMA.setTimeout(1000);

  Serial.println("Initing LoRa");

  //used to read naything on the line garbage
  str = loraTDMA.readStringUntil('\n');
  Serial.println(str);

  loraTDMA.println("sys get ver");
  str = loraTDMA.readStringUntil('\n');
  Serial.println("Module Version" + str);

  loraTDMA.println("sys get hweui");
  str = loraTDMA.readStringUntil('\n');
  Serial.println("EUI Version: " + str);

  
  loraTDMA.println("mac pause");
  str = loraTDMA.readStringUntil('\n');
  Serial.println("MAC Pause Status:" + str);

  loraTDMA.println("radio set mod lora");
  str = loraTDMA.readStringUntil('\n');
  Serial.println(str);
  
  loraTDMA.println("radio set freq 868100000");
  str = loraTDMA.readStringUntil('\n');
  Serial.println(str);
  
  loraTDMA.println("radio set pwr 14");
  str = loraTDMA.readStringUntil('\n');
  Serial.println(str);
  
  loraTDMA.println("radio set sf sf7");
  str = loraTDMA.readStringUntil('\n');
  Serial.println(str);
  
  loraTDMA.println("radio set afcbw 41.7");
  str = loraTDMA.readStringUntil('\n');
  Serial.println(str);
  
  loraTDMA.println("radio set rxbw 125");
  str = loraTDMA.readStringUntil('\n');
  Serial.println(str);
  
  
  loraTDMA.println("radio set prlen 8");
  str = loraTDMA.readStringUntil('\n');
  Serial.println(str);
  
  loraTDMA.println("radio set crc on");
  str = loraTDMA.readStringUntil('\n');
  Serial.println(str);
  
  loraTDMA.println("radio set iqi off");
  str = loraTDMA.readStringUntil('\n');
  Serial.println(str);
  
  loraTDMA.println("radio set cr 4/5");
  str = loraTDMA.readStringUntil('\n');
  Serial.println(str);
  
  loraTDMA.println("radio set sync 12");
  str = loraTDMA.readStringUntil('\n');
  Serial.println(str);
  
  loraTDMA.println("radio set bw 125");
  str = loraTDMA.readStringUntil('\n');
  Serial.println(str);

  Serial.println("starting loop");
  
  
  //Create the task!
  xTaskCreatePinnedToCore(
    TDMA_TaskManager,         // Task function
    "TDMA_TaskManager",       // Task name
    10000,             // Stack size (bytes)
    NULL,              // Parameters
    2,                 // Priority
    &TDMATaskHandle,  // Task handle
    1                  
  );
/*
  //Create the task!
  xTaskCreatePinnedToCore(
    Downlink_TaskManager,         // Task function
    "Downlink_TaskManager",       // Task name
    10000,             // Stack size (bytes)
    NULL,              // Parameters
    1,                 // Priority
    &DownLinkTaskHandle,  // Task handle
    1                  
  );
  
  */
   //LoraWANTask
   
  xTaskCreatePinnedToCore(
    LoraWAN_TaskManager,         // Task function
    "LoraWAN_TaskManager",       // Task name
    10000,             // Stack size (bytes)
    NULL,              // Parameters
    1,                 // Priority
    &LoraWANTaskHandle,  // Task handle
    0                
  );
  
}

void loop() {
 
}

