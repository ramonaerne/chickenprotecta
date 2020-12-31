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

#define LOC_LONGITUDE 40.0
#define LOC_LATITUDE 40.0
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

// used as mutex, but not thread safe on arduino, should be ok IMO
bool moving_door = false;
void move_door(int button);

/* ------------------------------------------------------ 
 *  EEPROM helper functions
 * ------------------------------------------------------ */

template <class T> int EEPROM_writeAnything(int ee, const T& value)
{
    const byte* p = (const byte*)(const void*)&value;
    unsigned int i;
    for (i = 0; i < sizeof(value); i++)
          EEPROM.write(ee++, *p++);
    return i;
}

template <class T> int EEPROM_readAnything(int ee, T& value)
{
    byte* p = (byte*)(void*)&value;
    unsigned int i;
    for (i = 0; i < sizeof(value); i++)
          *p++ = EEPROM.read(ee++);
    return i;
}

int16_t readEEPROM(int address) {
  int16_t val;
  int bytes = EEPROM_readAnything(address, val);

  if(bytes != sizeof(int16_t)) {
    Serial.println("failed to read bytes for integer");
    return -1;
  }

  return val;
}

void writeEEPROM(int address, int16_t val) {
  int bytes = EEPROM_writeAnything(address, val);
  if(bytes != sizeof(int16_t)) {
    Serial.println("failed to write bytes for integer");
    return;
  }
  EEPROM.commit();
}

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

void printDoorUp() 
{
  int sunrise = location.sunrise(year(), month(), day(), CE.utcIsDST(now()));
  int sunset = location.sunset(year(), month(), day(), CE.utcIsDST(now()));

  int sunrise_offset = readEEPROM(SUNRISE_OFF_A);
  int sunset_offset = readEEPROM(SUNSET_OFF_A);
  int open = sunrise + sunrise_offset;
  int close = sunset + sunset_offset;
  terminal.printf("open  door at  %d:%d (%d %d)\n", open / 60, open % 60, sunrise, sunrise_offset);
  terminal.printf("close door at %d:%d (%d %d)\n", close / 60, close % 60, sunset, sunset_offset);
  terminal.flush();

#ifdef DEBUG
  Serial.printf("open door at %d:%d\n", open / 60, open % 60);
  Serial.printf("close door at %d:%d\n", close / 60, close % 60);
  Serial.printf("now %d:%d\n", minsSinceMidnight / 60, minsSinceMidnight % 60);
#endif
}

static bool isDoorUp() {

  int minsSinceMidnight = getMinutesSinceMidnighteLoc();
  
  int sunrise = location.sunrise(year(), month(), day(), CE.utcIsDST(now()));
  int sunset = location.sunset(year(), month(), day(), CE.utcIsDST(now()));

  int open = sunrise + readEEPROM(SUNRISE_OFF_A);
  int close = sunset + readEEPROM(SUNSET_OFF_A);

  bool doorUp = (minsSinceMidnight > open) && (minsSinceMidnight < close);
  Serial.printf("door open: %d\n", doorUp);
  return doorUp;
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

  bool doorUp = isDoorUp();
  if (!moving_door)
    move_door(doorUp);
  
  return true; // keep timer running
}

/* ------------------------------------------------------ 
 *  Servo member functions and Blynk macros
 * ------------------------------------------------------ */

int getApproximateServoDelay(int old_pos, int new_pos) {
  
  int diff = abs(new_pos-old_pos);
  int del = diff * SERVO_MOVEMENT_MAX_MILLI / (SERVO_DOOR_MICRO_MAX-SERVO_DOOR_MICRO_MIN);
  del += DELAY_DOOR_MILLI;
  printf("delay: %d\n", del);
  if(del < 0) del = 0;
  if(del > SERVO_MOVEMENT_MAX_MILLI) del = SERVO_MOVEMENT_MAX_MILLI;

  return del;
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
    Serial.println("skip move_door, already in position");
    return;
  }

  moving_door = true;
  Blynk.setProperty(V2, "color", "#000000");
  
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
    terminal.print("lifting door..."); terminal.flush();
    int del = getApproximateServoDelay(door_milli_prev, door_milli);
    delay(del);
    Serial.printf("done in %dms\n", del);
    terminal.printf("done in %dms\n", del); terminal.flush();
  
    s_slider.write(angle);
    Serial.print("locking door...");
    terminal.print("locking door..."); terminal.flush();
    delay(DELAY_SLIDER_MILLI);
    Serial.print("done\n");
    terminal.print("done\n"); terminal.flush();
  
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
    terminal.print("unlocking door..."); terminal.flush(); 
    delay(DELAY_SLIDER_MILLI);
    Serial.print("done\n");  
    terminal.print("done\n"); terminal.flush(); 
  
    s_door.writeMicroseconds(door_milli);
    Serial.print("lowering door...");
    terminal.print("lowering door..."); terminal.flush(); 
    int del = getApproximateServoDelay(door_milli_prev, door_milli);
    delay(del);
    Serial.printf("done in %dms\n", del);
    terminal.printf("done in %dms\n", del); terminal.flush();
  }
  
  s_door.detach();
  s_slider.detach();   
  writeEEPROM(D_PREV_A, door_milli);
  writeEEPROM(S_PREV_A, angle);

  Blynk.setProperty(V2, "color", "#23C890");
  Blynk.virtualWrite(V2, (int) button);
  moving_door = false;
}

BLYNK_WRITE(V2)
{
  int button = param.asInt();
  if (!moving_door)
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


void helpTerminal()
{
  terminal.println(F("Blynk v" BLYNK_VERSION ": Device started"));
  terminal.println(F("-------------"));
  terminal.println(F("type 'help' or 'clear' to show this"));
  printDoorUp();
  terminal.flush();
}

void setupTerminal() 
{
  terminal.clear();
  helpTerminal();
}


BLYNK_WRITE(V6)
{
  if (String("clear") == param.asStr()) {
    setupTerminal();
  }
  else if (String("help") == param.asStr()) {
    helpTerminal();
  } else {
    // Send it back
    terminal.print("You said:");
    terminal.write(param.getBuffer(), param.getLength());
    terminal.println();
  }

  // Ensure everything is sent
  terminal.flush();
}
BLYNK_WRITE(V7) // sunrise offset
{
  int offset = param.asInt();
  Serial.printf("sunrise offset %d\n", offset);
  writeEEPROM(SUNRISE_OFF_A, offset);
  printDoorUp();
}
BLYNK_WRITE(V8) // sunset offset
{
  int offset = param.asInt();
  Serial.printf("sunset offset %d\n", offset);
  writeEEPROM(SUNSET_OFF_A, offset);
  printDoorUp();
}

void updateAllVirtualPinsFromEEPROM()
{
  Blynk.virtualWrite(V0, readEEPROM(D_CLOSE_A));
  Blynk.virtualWrite(V1, readEEPROM(D_OPEN_A));
  
  Blynk.virtualWrite(V2, get_door_position());
  Blynk.virtualWrite(V3, readEEPROM(S_CLOSE_A));
  Blynk.virtualWrite(V5, readEEPROM(S_OPEN_A));

  Blynk.virtualWrite(V7, readEEPROM(SUNRISE_OFF_A));
  Blynk.virtualWrite(V8, readEEPROM(SUNSET_OFF_A));
}

/* ------------------------------------------------------ 
 *  Main arduino setup and loop functions
 * ------------------------------------------------------ */
 
void setup()
{
  // Debug console
  Serial.begin(74880);

  Serial.println(sizeof(int));
  pinMode(LED_BUILTIN, OUTPUT);

  // Setup wifi and blynk
  WiFi.begin(ssid, pass);
 
  while ( WiFi.status() != WL_CONNECTED ) {
    delay ( 500 );
    Serial.print ( "." );
  }
  Blynk.begin(auth, ssid, pass);

  // setup persistent memory
  EEPROM.begin(32);

  updateAllVirtualPinsFromEEPROM();
  setupTerminal();

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
