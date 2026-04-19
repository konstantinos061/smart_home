#include <Arduino.h>
#include <HardwareSerial.h>

#define RECEPTION_TIME 10000
#define FRAME_SIZE 60000
#define READING_WINDOW 3000
#define ACK_WINDOW 2000
#define GUARD_TIME 500
#define BEACON_TIME 500
#define RST 21

String str;

HardwareSerial loraTDMA(2);

struct Node {
    String devAddr;     
    int slotTime;       
};

Node network[] = {
    {"DOOR_01", 1},    
    {"TEMP_01", 2},   
    {"LIGHT_01", 3},
    {"LIGHT_02", 4},
    {"LIGHT_03", 5}     
};

int totalNodes = sizeof(network) / sizeof(network[0]);

TaskHandle_t TDMATaskHandle = NULL;
TaskHandle_t LoraWANTaskHandle = NULL;

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
  //TODO::Add something for scanning window 
 

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
      vTaskDelay(100);
    }

    for (int i = 0; i < totalNodes; i++) {
        bool received_data = false; 
        int targetWake = network[i].slotTime * RECEPTION_TIME - GUARD_TIME;
        
        
        through_way_copy = start_time;
        xTaskDelayUntil(&through_way_copy, pdMS_TO_TICKS(targetWake));

        Serial.printf("Slot %d: Waiting for %s\n", i, network[i].devAddr.c_str());

        loraTDMA.println("radio rx 0"); 
        
        period = xTaskGetTickCount();
        while (xTaskGetTickCount() < (period + pdMS_TO_TICKS(READING_WINDOW))){

          if(loraTDMA.available() > 0){
            str = loraTDMA.readStringUntil('\n');
            Serial.println("str from lora: " + str);
            str.trim();

            if (str.indexOf("radio_rx") == 0) {
              Serial.println("Success! Data: " + str);
              received_data = true;
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
    
  
    through_way_copy = start_time;
    xTaskDelayUntil(&through_way_copy, pdMS_TO_TICKS(FRAME_SIZE));

  }
}
/*
void LoraWAN_TaskManager(void * pvParameters){
  //Handle LoraWAN communication
  //TODO
}
  */

//TODO::Add another task for downlink communication and instant actions
//TODO::Handle teh actual data


void setup() {
  // start the serial monitor at the speed we set in the ini file
  Serial.begin(19200);

  pinMode(RST, OUTPUT);

  loraTDMA.begin(57600, SERIAL_8N1, 16, 17);

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
  
  loraTDMA.println("mac pause");
  str = loraTDMA.readStringUntil('\n');
  Serial.println("MAC Pause Status:" + str);

  loraTDMA.println("radio set mod lora");
  str = loraTDMA.readStringUntil('\n');
  Serial.println(str);
  
  loraTDMA.println("radio set freq 869100000");
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
    1,                 // Priority
    &TDMATaskHandle,  // Task handle
    1                  
  );

  /*
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
  */


}

void loop() {
 
}

