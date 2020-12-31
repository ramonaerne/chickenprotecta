#define BLYNK_PRINT Serial

#include <ESP8266WiFi.h>
#include <Servo.h>
#include <BlynkSimpleEsp8266.h>
#include <EEPROM.h>
#include <ArduinoOTA.h>
#include "DHT.h"

#define D_OPEN_A  0x0
#define D_CLOSE_A 0x2
#define S_OPEN_A  0x4
#define S_CLOSE_A 0x6
#define D_PREV_A  0x8
#define S_PREV_A  0xA

#define DELAY_ATTACH_MILLI 2000
#define DELAY_DOOR_MILLI 2000
#define DELAY_SLIDER_MILLI 1000

#define SERVO_DOOR_MICRO_MIN 600
#define SERVO_DOOR_MICRO_MAX 2400
#define SERVO_MOVEMENT_MAX_MILLI 16000

// You should get Auth Token in the Blynk App.
// Go to the Project Settings (nut icon).
char auth[] = "****";

// Your WiFi credentials.
// Set password to "" for open networks.
char ssid[] = "****";
char pass[] = "****";
Servo s_door, s_slider;
//DHT dht(D5, DHT22);

int getApproximateServoDelay(int old_pos, int new_pos) {
  int diff = abs(new_pos-old_pos);
  int del = diff * SERVO_MOVEMENT_MAX_MILLI / (SERVO_DOOR_MICRO_MAX-SERVO_DOOR_MICRO_MIN);
  del += DELAY_DOOR_MILLI;
  printf("delay: %d\n", del);
  if(del < 0) return 0;
  if(del > SERVO_MOVEMENT_MAX_MILLI) return SERVO_MOVEMENT_MAX_MILLI;
  
  return del;
}

int readEEPROM(int address) {
  // little endian
  return (EEPROM.read(address+1) << 8) + EEPROM.read(address); // read a byte
}

void writeEEPROM(int address, int val) {
  // little endian
  EEPROM.write(address+1, (val >> 8) & 0xFF);
  EEPROM.write(address, val & 0xFF);
  EEPROM.commit();
}


BLYNK_WRITE(V0) // door min
{
  s_door.attach(D1);
  s_slider.attach(D2);
  int data = param.asInt();
 Serial.printf("V0 %d\n", data);
  // s_door.writeMicroseconds(data);
 writeEEPROM(D_CLOSE_A, data);
  // writeEEPROM(D_PREV_A, data);

}

BLYNK_WRITE(V1) // door max
{
  s_door.attach(D1);
  s_slider.attach(D2);
  int data = param.asInt();
  Serial.printf("V1 %d\n", data);
  //s_door.writeMicroseconds(data);
   writeEEPROM(D_OPEN_A, data);
  // writeEEPROM(D_PREV_A, data);

}

void move_door(int button)
{
  int angle, angle_prev;
  int door_milli, door_milli_prev;
  
  door_milli_prev = readEEPROM(D_PREV_A);
  s_door.attach(D1);
  s_slider.attach(D2);
  delay(DELAY_ATTACH_MILLI);

  if(button == 1) {
    // button on = door up
    door_milli = readEEPROM(D_OPEN_A);
    angle =  readEEPROM(S_OPEN_A);
  
    // door up: 
    // 0. power up servos (init pos should be the last)
    // 1. (wait for last position to be taken)
    // 2. lift door
    // 3. wait for door to finish
    // 4. move slider
    // 5. wait for slider to finish
    // 6. detach servos (door should come down to slider)
  
    s_door.writeMicroseconds(door_milli);
  
    // wait
    Serial.print("lifting door...");
    delay(getApproximateServoDelay(door_milli_prev, door_milli));
    Serial.print("done\n");  
  
    s_slider.write(angle);
    Serial.print("locking door...");
    delay(DELAY_SLIDER_MILLI);
    Serial.print("done\n");  
  
  } else {
    // button off = door down
    door_milli = readEEPROM(D_CLOSE_A);
    angle =  readEEPROM(S_CLOSE_A);
  
    // door down
    // 1. power up servos (init pos should be the last)
    // 2. wait for last position to be taken -> this will free the slider
    // 3. move slider
    // 4. wait for slider to finish
    // 5. lower door
    // 6. wait for door to finish
    // 7. detach servos (door should come down to bottom)
  
    s_slider.write(angle);
    Serial.print("unlocking door...");
    delay(DELAY_SLIDER_MILLI);
    Serial.print("done\n");  
  
    s_door.writeMicroseconds(door_milli);
    Serial.print("lowering door...");
    delay(getApproximateServoDelay(door_milli_prev, door_milli));
    Serial.print("done\n");  
  }
  
  s_door.detach();
  s_slider.detach();   
  writeEEPROM(D_PREV_A, door_milli);
  writeEEPROM(S_PREV_A, angle);
}

BLYNK_WRITE(V2)
{
  int button = param.asInt();
  move_door(button);
}

BLYNK_WRITE(V3) // slider close
{
  int angle = param.asInt();
  Serial.printf("V2 %d\n", angle);
  s_slider.write(angle);
  writeEEPROM(S_CLOSE_A, angle);
  //writeEEPROM(S_PREV_A, angle);
}
BLYNK_WRITE(V5) // slider open
{
  int angle = param.asInt();
  Serial.printf("V5 %d\n", angle);
  s_slider.write(angle);
  writeEEPROM(S_OPEN_A, angle);
  //writeEEPROM(S_PREV_A, angle);
  }
  BLYNK_READ(V4)
  {
  //int t = dht.readTemperature();
  //int h = dht.readHumidity();
  //Blynk.virtualWrite(V4, t);
}

void setup()
{
  // Debug console
  Serial.begin(9600);
  
  pinMode(LED_BUILTIN, OUTPUT);
  Blynk.begin(auth, ssid, pass);
  
  EEPROM.begin(16);

  // initialize with known positions
  // don't attach as this is only needed during transitions
  int init_milli = readEEPROM(D_PREV_A);
  int init_angle = readEEPROM(S_PREV_A);
  s_door.writeMicroseconds(init_milli);
  s_slider.write(init_angle);
  
  //dht.begin();


  // Port defaults to 8266
  // ArduinoOTA.setPort(8266);
  
  // Hostname defaults to esp8266-[ChipID]
  // ArduinoOTA.setHostname("myesp8266");
  
  // No authentication by default
  // ArduinoOTA.setPassword("admin");
  
  // Password can be set with it's md5 value as well
  // MD5(admin) = 21232f297a57a5a743894a0e4a801fc3
  // ArduinoOTA.setPasswordHash("21232f297a57a5a743894a0e4a801fc3");

  ArduinoOTA.onStart([]() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH) {
      type = "sketch";
    } else { // U_FS
      type = "filesystem";
    }
  
    // NOTE: if updating FS this would be the place to unmount FS using FS.end()
    Serial.println("Start updating " + type);
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) {
      Serial.println("Auth Failed");
    } else if (error == OTA_BEGIN_ERROR) {
      Serial.println("Begin Failed");
    } else if (error == OTA_CONNECT_ERROR) {
      Serial.println("Connect Failed");
    } else if (error == OTA_RECEIVE_ERROR) {
      Serial.println("Receive Failed");
    } else if (error == OTA_END_ERROR) {
      Serial.println("End Failed");
    }
  });
  ArduinoOTA.begin();
}

void loop()
{
  Blynk.run();
  ArduinoOTA.handle();
}
