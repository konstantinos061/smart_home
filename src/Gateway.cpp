#include <Arduino.h>
#include <HardwareSerial.h>

#define RST 21

String str;

HardwareSerial loraTDMA(2);


TaskHandle_t TDMATaskHandle = NULL;



void TDMA_TaskManager(void * pvParameters){

bool waiting_for_data;

  while(1){
    //First have to send the syncrhonization beacon every 60 seconds and then listen for the health informations

    

    loraTDMA.println("radio tx 53594E435F424541434F4E"); //First send the beacon to everyone beacon = SYNC_BEACON
    str = loraTDMA.readStringUntil('\n');
    Serial.println("Lora module confirmed the recption of the instruction to send" + str); //Message stating ok I will start sending 

    str = loraTDMA.readStringUntil('\n');
    Serial.println("Becon as been sent: " + str); //Message sating I started the actual sending

    //put the radio in rx

    TickType_t start_time = xTaskGetTickCount();

    xTaskDelayUntil( &start_time, pdMS_TO_TICKS(4800));
    //wake up and be ready to ahdnle door data

    loraTDMA.println("radio rx 4000");
    waiting_for_data = true;

    while (waiting_for_data){

      if(loraTDMA.available() > 0){
        str = loraTDMA.readStringUntil('\n');

        if(str.indexOf("radio_rx") == 0){
          Serial.println("Received Data from Door: " + str);
          waiting_for_data = false;
        }
        else {
          Serial.println("Lora Timeout - Nothing received: " + str);
          waiting_for_data = false;
          
        }

      }

      //Do another check in here if it decides to break or something
    }

    
    //then also add here later sednign of an ack and within taht ack ask for the node to wait and transmit to hime some info or not!


    xTaskDelayUntil( &start_time, pdMS_TO_TICKS(14800));

    loraTDMA.println("radio rx 4000");
    waiting_for_data = true;

    while (waiting_for_data){

      if(loraTDMA.available() > 0){
        str = loraTDMA.readStringUntil('\n');

        if(str.indexOf("radio_rx") == 0){
          Serial.println("Received Data from Thermostate: " + str);
          waiting_for_data = false;
        }
        else {
          Serial.println("Lora Timeout - Nothing received: " + str);
          waiting_for_data = false;
          
        }

      }

      //Do another check in here if it decides to break or something
    }
    

    xTaskDelayUntil( &start_time, pdMS_TO_TICKS(24800));

    loraTDMA.println("radio rx 4000");
    waiting_for_data = true;

    //transform this into a single fucntion maybe yheah
    while (waiting_for_data){

      if(loraTDMA.available() > 0){
        str = loraTDMA.readStringUntil('\n');

        if(str.indexOf("radio_rx") == 0){
          Serial.println("Received Data from Garden Light: " + str);
          waiting_for_data = false;
        }
        else {
          Serial.println("Lora Timeout - Nothing received: " + str);
          waiting_for_data = false;
          
        }

      }

      //Do another check in here if it decides to break or something
    }
    
    xTaskDelayUntil( &start_time, pdMS_TO_TICKS(60000));

  }
}


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
  
  loraTDMA.println("radio set wdt 60000"); //disable for continuous reception
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
    0                  
  );
}

void loop() {
 
}

