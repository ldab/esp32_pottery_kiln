/******************************************************************************
main.cpp
ESP8266 based Kiln controller, thermocouple type K, MAX31855
Leonardo Bispo
March, 2022
https://github.com/ldab/kiln
Distributed as-is; no warranty is given.
******************************************************************************/

#define BLYNK_PRINT Serial // Defines the object that is used for printing
// #define BLYNK_DEBUG        // Optional, this enables more detailed prints

#include <Arduino.h>

// Blynk and WiFi
#include <BlynkSimpleEsp8266.h>
#include <ESP8266WiFi.h>
#include <TimeLib.h>

// PapertrailLogger
#include "PapertrailLogger.h"
#include <WiFiUdp.h>

// OTA
#include <ArduinoOTA.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>

// MAX31855
#include <SPI.h>
#include <Wire.h>

#include "Adafruit_MAX31855.h"
#include "OTA.h"
#include "secrets.h"

#ifdef VERBOSE
#define DBG(msg, ...)                                     \
  {                                                       \
    Serial.printf("[%lu] " msg, millis(), ##__VA_ARGS__); \
  }
#else
#define DBG(...)
#endif

#define NOTIFY(msg, ...)               \
  {                                    \
    char _msg[64] = "";                \
    sprintf(_msg, msg, ##__VA_ARGS__); \
    Blynk.logEvent("alarm", _msg);     \
    DBG("%s\n", _msg);                 \
  }

// https://randomnerdtutorials.com/esp8266-pinout-reference-gpios/
#define BUZZER         D0 // GPIO12
#define RELAY          D1 // GPIO5
#define POWER          D2 // GPIO4

#define BUZZER_CHANNEL 1

// Heating Rate (°C/hr) during the last 100°C of Firing
#define SLOWFIRE       15
#define MEDIUMFIRE     60
#define FASTFIRE       150

#define COSTKWH        2.14

#define RATEUPDATE     60 // every 60 seconds

#define DIFFERENTIAL   5 // degC

// Update these with values suitable for your network.
const char *wifi_ssid     = s_wifi_ssid;
const char *wifi_password = s_wifi_password;
const char *mqtt_server   = s_mqtt_server;
const char *mqtt_user     = s_mqtt_user;
const char *mqtt_pass     = s_mqtt_pass;
uint16_t mqtt_port        = s_mqtt_port;
const char *blynk_auth    = s_blynk_auth;

float temp;
float tInt;
float currentSetpoint = -9999;
volatile float instPower;
volatile float energy = 0;
volatile float current;

// Control variables
volatile uint32_t energyMillis = 0;
uint32_t initMillis            = 0;
uint32_t holdMillis            = 0;
int step                       = 0;

// Timer instance numbers
int controlTimer;
int safetyTimer;
int rampTimer;
int slowCool;

Adafruit_MAX31855 thermocouple(PIN_SPI_SS);

BlynkTimer timer;

WidgetLED led(V6);

PapertrailLogger *errorLog;

void printSegments();
void rampRate();

BLYNK_CONNECTED()
{
  String resetReason   = ESP.getResetReason();
  uint32_t resetNumber = system_get_rst_info()->reason;

  if (resetNumber != REASON_DEFAULT_RST && resetNumber != REASON_SOFT_RESTART && resetNumber != REASON_EXT_SYS_RST) {
    // Restore data from the cloud
    for (size_t i = 11; i < 15; i++) {
      Blynk.syncVirtual(i);
      for (size_t j = 10; j < 25; j += 10) {
        Blynk.syncVirtual(i + j);
      }
    }

    Blynk.syncVirtual(V3, V9, V50);

    // Blynk.syncVirtual(V10);

    char resetInfo[32];
    sprintf(resetInfo, "%s epc1=0x%08x", resetReason.c_str(), system_get_rst_info()->epc1);
    Blynk.logEvent("info", resetInfo);
  }
  if (!timer.isEnabled(controlTimer)) {
    Blynk.virtualWrite(V5, 0);
    Blynk.virtualWrite(V10, 0);
    Blynk.virtualWrite(V7, "Idle 💤");
    led.off();
  }
}

// temperature, rate, hold/soak (min)
int segments[4][3] = {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}};

BLYNK_WRITE(V11) { segments[0][0] = param.asInt(); }
BLYNK_WRITE(V21) { segments[0][1] = param.asInt(); }
BLYNK_WRITE(V31) { segments[0][2] = param.asInt(); }

BLYNK_WRITE(V12) { segments[1][0] = param.asInt(); }
BLYNK_WRITE(V22) { segments[1][1] = param.asInt(); }
BLYNK_WRITE(V32) { segments[1][2] = param.asInt(); }

BLYNK_WRITE(V13) { segments[2][0] = param.asInt(); }
BLYNK_WRITE(V23) { segments[2][1] = param.asInt(); }
BLYNK_WRITE(V33) { segments[2][2] = param.asInt(); }

BLYNK_WRITE(V14) { segments[3][0] = param.asInt(); }
BLYNK_WRITE(V24) { segments[3][1] = param.asInt(); }
BLYNK_WRITE(V34) { segments[3][2] = param.asInt(); }

// BLYNK_WRITE(V5) { segments[3][2] = param.asInt() / 60; } TODO initMillis to
// UNIX
BLYNK_WRITE(V3) { energy = param.asFloat(); }
BLYNK_WRITE(V9) { currentSetpoint = param.asFloat(); }
BLYNK_WRITE(V50) { step = param.asInt(); }

BLYNK_WRITE(V10)
{
  for (size_t i = 0; i < sizeof(segments) / sizeof(segments[0]); i++) {
    Blynk.virtualWrite(11 + i * 1 + 20, segments[i][2]);
    for (size_t j = 0; j < sizeof(segments[0]) / (sizeof(int)) - 1; j++) {
      // sync pins
      Blynk.virtualWrite(11 + i * 1 + j * 10, segments[i][j]);
      if (segments[i][j] == 0) {
        Blynk.virtualWrite(V10, LOW);
        Blynk.logEvent("check", "Check the settings, i: " + String(i) + " j:" + String(j));
        return;
      }
    }
  }

  if (/*segments[3][0] < segments[2][0] ||*/ segments[2][0] < segments[1][0] ||
      segments[1][0] < segments[0][0]) {
    Blynk.virtualWrite(V10, LOW);
    Blynk.logEvent("check", "Invalid Target temperature");
    return;
  }

  // TODO confirm values as not pressing "enter" does funny things

  int pinValue = param.asInt();
  if (pinValue) {
    initMillis = millis();
    int tTotal = (segments[0][0] - temp) / segments[0][1] * 60 + segments[0][2];
    tTotal += (segments[1][0] - segments[0][0]) * 60 / segments[1][1] +
              segments[1][2];
    tTotal += (segments[2][0] - segments[1][0]) * 60 / segments[2][1] +
              segments[2][2];
    tTotal += (segments[3][0] - segments[2][0]) * 60 / segments[3][1] +
              segments[3][2];

    DBG("tTotal %dmin\n", tTotal);

    Blynk.setProperty(V5, "max", tTotal);
    Blynk.setProperty(V0, "max", segments[3][0]);
    Blynk.virtualWrite(V7, "Firing 🔥 @" + String(segments[step][0]) + "°C");

    printSegments();
    timer.enable(controlTimer);
    timer.enable(rampTimer);
    rampRate();
  } else {
    DBG("Button pressed, disable temp control\n");
    Blynk.virtualWrite(V7, "Idle 💤");
    // ESP.restart();
    digitalWrite(RELAY, LOW);
    led.off();
    step            = 0;
    holdMillis      = 0;
    currentSetpoint = -9999;
    timer.disable(controlTimer);
    timer.disable(rampTimer);
  }
}

void sendData()
{
  Blynk.virtualWrite(V0, temp);
  Blynk.virtualWrite(V1, current);
  Blynk.virtualWrite(V2, instPower);
  Blynk.virtualWrite(V3, energy);
  Blynk.virtualWrite(V4, energy * COSTKWH);
  Blynk.virtualWrite(V8, tInt);
  Blynk.virtualWrite(V9, currentSetpoint);
  Blynk.virtualWrite(V50, step);
  Blynk.virtualWrite(V51, WiFi.RSSI());
  current   = 0;
  instPower = 0;
  if (timer.isEnabled(controlTimer)) {
    Blynk.virtualWrite(V5, (int)((millis() - initMillis) / (60 * 1000)));
  }
}

void safetyCheck()
{
  static bool tIntError  = false;
  static bool noRlyError = false;

  if (timer.isEnabled(controlTimer)) {
    if (temp > (currentSetpoint + 10)) // TODO check differential
    {
      DBG("HIGH TEMPERATURE ALARM\n");
    } else if (temp < (currentSetpoint - 20)) {
      DBG("LOW TEMPERATURE ALARM\n");
    }
  }

  if (tInt > 60) {
    if (!tIntError) {
      char tIntChar[8];
      sprintf(tIntChar, "%.1f°C", tInt);
      DBG("High internal temp: %s", tIntChar);
      Blynk.logEvent("highIntTemp", tIntChar);
      tIntError = true;
    }
  } else {
    tIntError = false;
  }

  if (digitalRead(RELAY)) {
    if ((millis() - energyMillis) > 2000L) {
      if (!noRlyError) {
        DBG("PROBLEM 2300W expect 1 pulse every ~1565ms\n");
        Blynk.logEvent("noRlyError");
        noRlyError = true;
      }
    } else {
      noRlyError = true;
    }
  }
}

void printSegments()
{
  DBG("Firing ");
  for (size_t i = 0; i < sizeof(segments) / sizeof(segments[0]); i++) {
    Serial.printf("{");
    for (size_t j = 0; j < sizeof(segments[0]) / (sizeof(int)); j++) {
      Serial.printf("%d,", segments[i][j]);
    }
    Serial.printf("} ");
  }
  Serial.printf("\n");
}

IRAM_ATTR void readPower()
{
  if (energy == 0) {
    // We don't know the time difference between pulse, reset
    // TODO get from cloud in case MCU reset
    energy       = 1 / 1000.0f;
    current      = 0;
    instPower    = 0;

    energyMillis = millis();
  } else {
    volatile uint32_t pulseInterval = millis() - energyMillis;

    if (pulseInterval < 100) { // 10*sqrt(2) Amps =~ 1106.8ms
      // DBG("DEBOUNCE");
      return;
    }

    energy += 1 / 1000.0f;                             // each pulse is 1Wh
    instPower    = (3600) / (pulseInterval / 1000.0f); // 1Wh = 3600J
    current      = instPower / 230.0f;

    energyMillis = millis();
  }

  volatile static bool problem = false;
  if (!digitalRead(RELAY)) {
    if (problem == true) {
      char shortError[32];
      sprintf(shortError, "I = %.1fA P = %.1fW", current, instPower);
      DBG("PROBLEM, current but relay is Off, %s", shortError);
      Blynk.logEvent("shortErr", shortError);
    }

    problem = true;
  } else
    problem = false;

  DBG("Current: %.1fA, Power: %.1fW, Energy: %.1fKWh\n", current, instPower,
      energy);
}

void getTemp()
{
  static float _t   = 0;
  static uint8_t _s = 0;
  static bool tErr  = false;

  temp              = thermocouple.readCelsius();
  tInt              = thermocouple.readInternal();
  uint8_t error     = thermocouple.readError();

  // average 5x samples
  _t += temp;
  _s++;
  if (_s == 4) {
    temp = (float)(_t / _s);
    _t   = 0;
    _s   = 0;
  }

  // Ignore SCG fault
  // https://forums.adafruit.com/viewtopic.php?f=31&t=169135#p827564
  if (error & 0b001) {
    if (!tErr) {
      temp = NAN;
      tErr = true;

      DBG("Thermocouple error #%i", error);
      Blynk.logEvent("thermocouple_error", error);

      digitalWrite(RELAY, LOW);
    }
  } else {
    tErr = false;
    DBG("T: %.02fdegC\n", temp);
  }
}

void holdTimer(uint32_t _segment)
{
  if (holdMillis == 0) {
    DBG("Start hold for %dmin\n", _segment);
    holdMillis = millis();
    timer.disable(rampTimer);
  }
  uint32_t _elapsed = (millis() - holdMillis) / (60 * 1000);
  Blynk.virtualWrite(V7, "Hold: " + String(currentSetpoint, 0) + "°C-" +
                             String(_elapsed) + "/" + String(_segment) + "min");

  if (_elapsed >= _segment) {
    step++;
    holdMillis = 0;
    timer.enable(rampTimer);
    DBG("Done with hold, step: %d\n", step);
    Blynk.virtualWrite(V7, "Firing 🔥 @" + String(segments[step][0]) + "°C");
  }
}

void tControl()
{
  DBG("Control ST: %.01fdegC, step: %d\n", currentSetpoint, step);
  static uint8_t diff;

  if (!isnan(temp)) {
    // TODO Differential
    float delta_t = currentSetpoint - temp - diff;
    if (delta_t >= 0) {
      if (!digitalRead(RELAY)) {
        digitalWrite(RELAY, HIGH);
        led.on();
        timer.restartTimer(safetyTimer);
        diff = 0;
      }
    } else if (digitalRead(RELAY)) {
      digitalWrite(RELAY, LOW);
      led.off();
      diff = DIFFERENTIAL;
    }
    if (step == 5)
      return;
    if (temp > segments[step][0]) {
      currentSetpoint = segments[step][0];

      if (segments[step][2] == -1)
        segments[step][2] = 0;
      holdTimer(segments[step][2]);

      if (step == 4) {
        uint8_t h = (millis() - initMillis) / (1000 * 3600);
        uint8_t m = ((millis() - initMillis) - (h * 3600 * 1000)) / (60 * 1000);
        char endInfo[64];
        sprintf(endInfo, "Reached Temp, after: %d:%d", h, m);
        DBG("%s", endInfo);
        Blynk.logEvent("info", endInfo);
        Blynk.virtualWrite(V7, "Slow Cooling ❄️");
        timer.enable(slowCool);
        timer.disable(rampTimer);
        step++;
      }
    }
  }
}

void rampRate()
{
  // http://www.stoneware.net/stoneware/glasyrer/firing.htm

  if (currentSetpoint == -9999)
    currentSetpoint = temp;

  if (currentSetpoint >= segments[step][0]) {
    // TODO alarm if takes too long
    currentSetpoint = segments[step][0];
  } else {
    currentSetpoint += (float)(segments[step][1] / (3600.0f / RATEUPDATE));
  }

  DBG("Current Setpoint: %.02fdegC, step: %d\n", currentSetpoint, step);
}

void rampDown()
{
  // https://digitalfire.com/schedule/04dsdh
  if (currentSetpoint < 760 || step == 5) {
    timer.disable(controlTimer);
    timer.disable(slowCool);
    currentSetpoint = 0;
    tControl();
    Blynk.virtualWrite(V7, "Cooling ❄️");
  }

  currentSetpoint -= (float)(83.0 / (3600.0f / RATEUPDATE));
}

void pinInit()
{
  pinMode(POWER, INPUT);
  attachInterrupt(digitalPinToInterrupt(POWER), readPower, RISING);

  pinMode(RELAY, OUTPUT);
  digitalWrite(RELAY, LOW);

  pinMode(BUZZER, OUTPUT);
}

void otaInit()
{
  ArduinoOTA.setHostname("kiln");

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
  ArduinoOTA.onEnd([]() { Serial.println("\nEnd"); });
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

void setup()
{
#ifdef VERBOSE
  Serial.begin(115200);
  DBG("VERSION %s\n", BLYNK_FIRMWARE_VERSION);
#endif

#ifdef CALIBRATE
  // Measure GPIO in order to determine Vref to gpio 25 or 26 or 27
  adc2_vref_to_gpio(GPIO_NUM_25);
  delay(5000);
  abort();
#endif

  pinInit();

  if (!thermocouple.begin()) {
    DBG("ERROR.\n");
    // while (1) delay(10);
  } else
    DBG("MAX31855 Good\n");

  wifi_station_set_hostname("kiln");
  Blynk.begin(blynk_auth, wifi_ssid, wifi_password);

  while (WiFi.waitForConnectResult() != WL_CONNECTED) {
    delay(50);
  }

  otaInit();

  timer.setInterval(2000L, getTemp);
  timer.setInterval(10000L, sendData);

  safetyTimer  = timer.setInterval(2115L, safetyCheck); // 2100ms =~ 7.5A

  controlTimer = timer.setInterval(5530L, tControl);
  timer.disable(controlTimer); // enable it after button is pressed

  rampTimer = timer.setInterval(RATEUPDATE * 1000L, rampRate);
  timer.disable(rampTimer); // enable it after button is pressed

  slowCool = timer.setInterval(RATEUPDATE * 1000L, rampDown);
  timer.disable(slowCool);

  struct rst_info *rtc_info = system_get_rst_info();
  uint32_t resetNumber      = rtc_info->reason;

  if (resetNumber != REASON_DEFAULT_RST && resetNumber != REASON_SOFT_RESTART && resetNumber != REASON_EXT_SYS_RST) {
    errorLog           = new PapertrailLogger(PAPERTRAIL_HOST, PAPERTRAIL_PORT, LogLevel::Error, "\033[0;31m", "untrol.io", BLYNK_DEVICE_NAME);
    String resetReason = ESP.getResetReason();

    DBG("Reset Reason [%d] %s\n", resetNumber, resetReason.c_str());
    errorLog->printf("Reset Reason [%d] %s\n", resetNumber, resetReason.c_str());

    if (rtc_info->reason == REASON_EXCEPTION_RST) {
      DBG("Fatal exception (%d):\n", rtc_info->exccause);
      errorLog->printf("Fatal exception (%d):\n", rtc_info->exccause);
    }
    DBG("epc1=0x%08x, epc2=0x%08x, epc3=0x%08x, excvaddr=0x%08x, depc=0x%08x\n", rtc_info->epc1, rtc_info->epc2,
        rtc_info->epc3, rtc_info->excvaddr, rtc_info->depc); // The address of the last crash is printed, which is used to debug garbled output.
    errorLog->printf("epc1=0x%08x, epc2=0x%08x, epc3=0x%08x, excvaddr=0x%08x, depc=0x%08x\n", rtc_info->epc1, rtc_info->epc2,
                     rtc_info->epc3, rtc_info->excvaddr, rtc_info->depc);
  }
}

void loop()
{
  ArduinoOTA.handle();
  Blynk.run();
  timer.run();
}