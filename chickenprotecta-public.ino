#define BLYNK_PRINT Serial

#include <ESP8266WiFi.h>
#include <Servo.h>
#include <BlynkSimpleEsp8266.h>
#include <EEPROM.h>
#include <ArduinoOTA.h>
#include "DHT.h"

#include <NTPClient.h>
#include <WiFiUdp.h>
#include <TimeLib.h>
#include <Timezone.h>
#include <Dusk2Dawn.h>
#include <time.h>
#include <arduino-timer.h>

/* ------------------------------------------------------ 
 *  Defines and constants
 * ------------------------------------------------------ */

#define D_OPEN_A  0x0
#define D_CLOSE_A 0x2
#define S_OPEN_A  0x4
#define S_CLOSE_A 0x6
#define D_PREV_A  0x8
#define S_PREV_A  0xA

#define SUNRISE_OFF_A 0xC
#define SUNSET_OFF_A  0xE

#define DELAY_ATTACH_MILLI 2000
#define DELAY_DOOR_MILLI 2000
#define DELAY_SLIDER_MILLI 1000

#define SERVO_DOOR_MICRO_MIN 600
#define SERVO_DOOR_MICRO_MAX 2400
#define SERVO_MOVEMENT_MAX_MILLI 16000

#define NTP_SYNC_TIME_SEC 10
#define TIMER_INTERVAL_SEC 15

#define LOC_LONGITUDE 47.3769 
#define LOC_LATITUDE  8.5417
#define LOC_TIMEZONE_OFFSET 1

//#define DEBUG 1

char ntp_name[] = "europe.pool.ntp.org";

// Blynk token
char auth[] = "****";

// Your WiFi credentials.
char ssid[] = "****";
char pass[] = "****";


/* ------------------------------------------------------ 
 *  Global instances
 * ------------------------------------------------------ */
Servo s_door, s_slider;
//DHT dht(D5, DHT22);

WidgetTerminal terminal(V6);

auto timer = timer_create_default();
Dusk2Dawn location(LOC_LONGITUDE, LOC_LATITUDE, LOC_TIMEZONE_OFFSET);

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, ntp_name, 0, NTP_SYNC_TIME_SEC * 1000);
TimeChangeRule CEST = {"CEST", Last, Sun, Mar, 2, 120};     // Central European Summer Time
TimeChangeRule CET = {"CET ", Last, Sun, Oct, 3, 60};       // Central European Standard Time
Timezone CE(CEST, CET);

void move_door(int button);

/* ------------------------------------------------------ 
 *  Time/NTP and Sunset/Sunrise helper functions
 * ------------------------------------------------------ */

static tm getDateTimeByParams(long time){
    struct tm *newtime;
    const time_t tim = time;
    newtime = localtime(&tim);
    return *newtime;
}

int getMinutesSinceMidnighteLoc() {
  tm loc;
  loc = getDateTimeByParams(CE.toLocal(now()));
  Serial.printf("%d %d %d %d:%d\n", loc.tm_year + 1900, loc.tm_mon+1, loc.tm_mday, loc.tm_hour, loc.tm_min);
  return loc.tm_hour * 60 + loc.tm_min;
}

static bool isSunUp() {

  int minsSinceMidnight = getMinutesSinceMidnighteLoc();
  
  int sunrise = location.sunrise(year(), month(), day(), CE.utcIsDST(now()));
  int sunset = location.sunset(year(), month(), day(), CE.utcIsDST(now()));
#ifdef DEBUG
  Serial.printf("sunrise %d:%d\n", sunrise / 60, sunrise % 60);
  Serial.printf("sunset %d:%d\n", sunset / 60, sunset % 60);
  Serial.printf("now %d:%d\n", minsSinceMidnight / 60, minsSinceMidnight % 60);
#else
  terminal.printf("sunrise %d:%d\n", sunrise / 60, sunrise % 60);
  terminal.printf("sunset %d:%d\n", sunset / 60, sunset % 60);
  terminal.flush();
#endif
  bool sunUp = (minsSinceMidnight > sunrise) && (minsSinceMidnight < sunset);
  Serial.printf("door open: %d\n", sunUp);
  return sunUp;
}

bool updateTimeAndDoTask(void *) {

  // manual update of clock since setsyncprovider
  // does not work correctly
  if (timeClient.update()){
     Serial.println ( "Update clock" );
     setTime(timeClient.getEpochTime());   
  } else{
     Serial.println ( "NTP Update not WORK!!" );
  }

  bool sunup = isSunUp();

  // calculate doorUp from sunup
  bool doorUp = sunup;

  // todo: do something here
  Blynk.virtualWrite(V2, (int) doorUp);
  move_door(doorUp);
  
  return true; // keep timer running
}

/* ------------------------------------------------------ 
 *  Servo member functions and Blynk macros
 * ------------------------------------------------------ */

int getApproximateServoDelay(int old_pos, int new_pos) {
  // todo remove
  return 1000;
  
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
  s_door.attach(D3); // d1
  s_slider.attach(D4); // d2
  int data = param.asInt();
 Serial.printf("V0 %d\n", data);
  // s_door.writeMicroseconds(data);
 writeEEPROM(D_CLOSE_A, data);
  // writeEEPROM(D_PREV_A, data);

}

BLYNK_WRITE(V1) // door max
{
  s_door.attach(D3);
  s_slider.attach(D4);
  int data = param.asInt();
  Serial.printf("V1 %d\n", data);
  //s_door.writeMicroseconds(data);
   writeEEPROM(D_OPEN_A, data);
  // writeEEPROM(D_PREV_A, data);

}

bool get_door_position()
{
  int door_milli_up, door_milli_down, door_milli_prev;
  
  door_milli_prev = readEEPROM(D_PREV_A);
  door_milli_up   = readEEPROM(D_OPEN_A);
  door_milli_down = readEEPROM(D_CLOSE_A);

  return abs(door_milli_prev-door_milli_up) < abs(door_milli_prev - door_milli_down);
}

void move_door(int button)
{
  int angle, angle_prev;
  int door_milli, door_milli_prev;

  if(get_door_position() == button) {
    Serial.print("skip move_door, already in position");
    return;
  }
  
  door_milli_prev = readEEPROM(D_PREV_A);
  s_door.attach(D3);
  s_slider.attach(D4);
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

BLYNK_WRITE(V6)
{

  // if you type "Marco" into Terminal Widget - it will respond: "Polo:"
  if (String("Marco") == param.asStr()) {
    terminal.println("You said: 'Marco'") ;
    terminal.println("I said: 'Polo'") ;
  } else {

    // Send it back
    terminal.print("You said:");
    terminal.write(param.getBuffer(), param.getLength());
    terminal.println();
  }

  // Ensure everything is sent
  terminal.flush();
}

void setupTerminal() 
{
    // Clear the terminal content
  terminal.clear();

  // This will print Blynk Software version to the Terminal Widget when
  // your hardware gets connected to Blynk Server
  terminal.println(F("Blynk v" BLYNK_VERSION ": Device started"));
  terminal.println(F("-------------"));
  terminal.println(F("Type 'Marco' and get a reply, or type"));
  terminal.println(F("anything else and get it printed back."));
  terminal.flush();
}

/* ------------------------------------------------------ 
 *  Main arduino setup and loop functions
 * ------------------------------------------------------ */
 
void setup()
{
  // Debug console
  Serial.begin(74880);
  pinMode(LED_BUILTIN, OUTPUT);

  // Setup wifi and blynk
  WiFi.begin(ssid, pass);
 
  while ( WiFi.status() != WL_CONNECTED ) {
    delay ( 500 );
    Serial.print ( "." );
  }
  Blynk.begin(auth, ssid, pass);

  // setup persistent memory
  EEPROM.begin(16);

  // initialize with known positions
  // don't attach as this is only needed during transitions
  int init_milli = readEEPROM(D_PREV_A);
  int init_angle = readEEPROM(S_PREV_A);
  s_door.writeMicroseconds(init_milli);
  s_slider.write(init_angle);

  // setup temp sensor
  //dht.begin();

  // set initial time and timer
  timeClient.begin(); 
  setTime(timeClient.getEpochTime()); 
  timer.every(TIMER_INTERVAL_SEC * 1000, updateTimeAndDoTask);

  setupTerminal();

  // setup OTA update
  // TODO: enable security
  
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
  timer.tick();
  Blynk.run();
  ArduinoOTA.handle();
}
