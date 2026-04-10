#include <Arduino.h>
#include <HardwareSerial.h>

#define RST 21

String str;

HardwareSerial loraSerial(2);


void setup() {
  // start the serial monitor at the speed we set in the ini file
  Serial.begin(9600);

  pinMode(RST, OUTPUT);

  loraSerial.begin(57600, SERIAL_8N1, 16, 17);

  digitalWrite(RST, LOW);
  delay(200);
  digitalWrite(RST, HIGH);


  loraSerial.setTimeout(1000);

  Serial.println("Initing LoRa");

  //used to read naything on the line garbage
  str = loraSerial.readStringUntil('\n');
  Serial.println(str);

  loraSerial.println("sys get ver");
  str = loraSerial.readStringUntil('\n');
  Serial.println("Module Version" + str);
  
  loraSerial.println("mac pause");
  str = loraSerial.readStringUntil('\n');
  Serial.println("MAC Pause Status:" + str);

  loraSerial.println("radio set mod lora");
  str = loraSerial.readStringUntil('\n');
  Serial.println(str);
  
  loraSerial.println("radio set freq 869100000");
  str = loraSerial.readStringUntil('\n');
  Serial.println(str);
  
  loraSerial.println("radio set pwr 14");
  str = loraSerial.readStringUntil('\n');
  Serial.println(str);
  
  loraSerial.println("radio set sf sf7");
  str = loraSerial.readStringUntil('\n');
  Serial.println(str);
  
  loraSerial.println("radio set afcbw 41.7");
  str = loraSerial.readStringUntil('\n');
  Serial.println(str);
  
  loraSerial.println("radio set rxbw 125");
  str = loraSerial.readStringUntil('\n');
  Serial.println(str);
  
  
  loraSerial.println("radio set prlen 8");
  str = loraSerial.readStringUntil('\n');
  Serial.println(str);
  
  loraSerial.println("radio set crc on");
  str = loraSerial.readStringUntil('\n');
  Serial.println(str);
  
  loraSerial.println("radio set iqi off");
  str = loraSerial.readStringUntil('\n');
  Serial.println(str);
  
  loraSerial.println("radio set cr 4/5");
  str = loraSerial.readStringUntil('\n');
  Serial.println(str);
  
  loraSerial.println("radio set wdt 60000"); //disable for continuous reception
  str = loraSerial.readStringUntil('\n');
  Serial.println(str);
  
  loraSerial.println("radio set sync 12");
  str = loraSerial.readStringUntil('\n');
  Serial.println(str);
  
  loraSerial.println("radio set bw 125");
  str = loraSerial.readStringUntil('\n');
  Serial.println(str);

  Serial.println("starting loop");
  
}

void loop() {

    loraSerial.println("radio rx 0");
    str = String("");
    while(str == ""){
        str = loraSerial.readStringUntil('\n');
    }
    if ( str.indexOf("radio_rx") == 0 ) //checks if the data received from the serial line has the LoRa communication packet format, if it starts with the "radio_rx" string
    {
      Serial.print("Beacon received: "); //print the data to the serial monitor to check it content
      Serial.println(str);

      delay(30000);

    }
    else
    {
      Serial.println("Received nothing");
    }
 
}

