/******************************************************************************
main.cpp
ESP8266 based Kiln controller, thermocouple type K, MAX31855
Leonardo Bispo
Feb, 2021
https://github.com/ldab/kiln
Distributed as-is; no warranty is given.
******************************************************************************/

#include <Arduino.h>

// #define BLYNK_PRINT Serial

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
#include "secrets.h"

#ifndef DEVICE_NAME
#error Remember to define the Device Name
#elif not defined TO
#error Remember to set the email address
#endif

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
    Blynk.notify(_msg);                \
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

#define BURST_PERIOD   1000
#define BURST_RESOLUT  10

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
float currentSetpoint = 0;
volatile float instPower;
volatile float energy = 0;
volatile float current;

// Control variables
volatile uint32_t energyMillis = 0;
uint32_t initMillis            = 0;
uint32_t holdMillis            = 0;
int step                       = 0;
uint8_t pid_out                = 0;

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
  DBG("Reset Reason [%d] %s\n", resetNumber, resetReason.c_str());
  if (resetNumber != 0 && resetNumber != 4 && resetNumber != 6) {
    // Restore data from the cloud
    for (size_t i = 11; i < 15; i++) {
      Blynk.syncVirtual(i);
      for (size_t j = 10; j < 35; j += 10) {
        Blynk.syncVirtual(i + j);
      }
    }

    delay(250);
    Blynk.syncVirtual(V3, V9, V50);
    delay(250);

    while (currentSetpoint == 0)
      delay(25);

    Blynk.syncVirtual(V10);

    NOTIFY(resetReason.c_str());
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
BLYNK_WRITE(V31) { segments[0][2] = param.asInt() / 60; }

BLYNK_WRITE(V12) { segments[1][0] = param.asInt(); }
BLYNK_WRITE(V22) { segments[1][1] = param.asInt(); }
BLYNK_WRITE(V32) { segments[1][2] = param.asInt() / 60; }

BLYNK_WRITE(V13) { segments[2][0] = param.asInt(); }
BLYNK_WRITE(V23) { segments[2][1] = param.asInt(); }
BLYNK_WRITE(V33) { segments[2][2] = param.asInt() / 60; }

BLYNK_WRITE(V14) { segments[3][0] = param.asInt(); }
BLYNK_WRITE(V24) { segments[3][1] = param.asInt(); }
BLYNK_WRITE(V34) { segments[3][2] = param.asInt() / 60; }

// BLYNK_WRITE(V5) { segments[3][2] = param.asInt() / 60; } TODO initMillis to
// UNIX
BLYNK_WRITE(V3) { energy = param.asFloat(); }
BLYNK_WRITE(V9) { currentSetpoint = param.asFloat(); }
BLYNK_WRITE(V50) { step = param.asInt(); }

BLYNK_WRITE(V10)
{
  char tz[] = "Europe/Copenhagen";
  for (size_t i = 0; i < sizeof(segments) / sizeof(segments[0]); i++) {
    if (!segments[i][2])
      Blynk.virtualWrite(11 + i * 1 + 20, -1, -1, tz);
    else
      Blynk.virtualWrite(11 + i * 1 + 20, segments[i][2] * 60, -1, tz);
    for (size_t j = 0; j < sizeof(segments[0]) / (sizeof(int)) - 1; j++) {
      // sync pins
      Blynk.virtualWrite(11 + i * 1 + j * 10, segments[i][j]);
      if (segments[i][j] == 0) {
        Blynk.virtualWrite(V10, LOW);
        Blynk.notify("Check the settings, i: " + String(i) + " j:" + String(j));
        return;
      }
    }
  }

  if (/*segments[3][0] < segments[2][0] ||*/ segments[2][0] < segments[1][0] ||
      segments[1][0] < segments[0][0]) {
    Blynk.virtualWrite(V10, LOW);
    Blynk.notify("Invalid Target temperature");
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
    step            = 0;
    holdMillis      = 0;
    currentSetpoint = 0;
    timer.disable(controlTimer);
    timer.disable(rampTimer);
  }
}

void sendData()
{
  Blynk.virtualWrite(V0, String(temp, 2));
  Blynk.virtualWrite(V1, String(current, 1));
  Blynk.virtualWrite(V2, String(instPower, 0) + "W");
  Blynk.virtualWrite(V3, energy);
  Blynk.virtualWrite(V4, String(energy * COSTKWH, 1) + "dkk");
  Blynk.virtualWrite(V8, String(tInt, 2));
  Blynk.virtualWrite(V9, String(currentSetpoint, 2));
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
  if (timer.isEnabled(controlTimer)) {
    if (temp > (currentSetpoint + 10)) // TODO check differential
    {
      DBG("HIGH TEMPERATURE ALARM\n");
    } else if (temp < (currentSetpoint - 20)) {
      DBG("LOW TEMPERATURE ALARM\n");
    }
  }

  if (tInt > 60) {
    NOTIFY("High internal temp: %.1f°C", tInt);
  }

  if (pid_out && energyMillis) {
    float proportionalPower = 10.0 * 230 * pid_out / 10;
    uint32_t delta_t        = 3600 * 1000 / proportionalPower;
    if ((millis() - energyMillis) > delta_t * 1.1) {
      NOTIFY("PROBLEM %.1fW expect 1 pulse every ~%dms\n", proportionalPower, delta_t);
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
      NOTIFY("PROBLEM, current but relay is Off, I = %.1fA P = %.1fW", current,
             instPower);
    }

    problem = true;
  } else
    problem = false;

  DBG("Current: %.1fA, Power: %.1fW, Energy: %.1fKWh\n", current, instPower,
      energy);
}

uint16_t calculatePid(float input, float setpoint, float kp, float ki, float kd)
{
  static float _lastErr = 0;
  static float errSum   = 0;
  static float _last    = millis() - 1000; // avoid zero division
  uint32_t _now         = millis();
  float deltaT          = (_now - _last) / 1000.0;
  float _error          = setpoint - input;
  float deltaErr        = (_error - _lastErr);

  errSum += (_error * deltaT);

  // TODO limit integral part
  if (errSum > BURST_RESOLUT)
    errSum = BURST_RESOLUT;
  else if (errSum < 0)
    errSum = 0;

  /*Compute PID Output*/
  int32_t _out = kp * _error + ki * errSum + kd * deltaErr / deltaT;

  /*Remember some variables for next time*/
  _lastErr     = _error;
  _last        = _now;

  if (_out > BURST_RESOLUT)
    _out = BURST_RESOLUT;
  else if (_out < 0)
    _out = 0;

  DBG("PID output: %d%\n", _out);
  Blynk.virtualWrite(V52, _out);

  return _out;
}

void burstFire(void)
{
  digitalWrite(RELAY, pid_out);
  pid_out = pid_out <= 0 ? 0 : pid_out - 1;
}

void getTemp()
{
  static float _t   = 0;
  static uint8_t _s = 0;

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
    temp = NAN;
    NOTIFY("Thermocouple error #%i", error);

    digitalWrite(RELAY, LOW);
  } else {
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
  // static uint8_t diff;

  if (!isnan(temp)) {
    // float delta_t = currentSetpoint - temp - diff;
    // if (delta_t >= 0) {
    //   if (!digitalRead(RELAY)) {
    //     digitalWrite(RELAY, HIGH);
    //     led.on();
    //     timer.restartTimer(safetyTimer);
    //     diff = 0;
    //   }
    // } else if (digitalRead(RELAY)) {
    //   digitalWrite(RELAY, LOW);
    //   led.off();
    //   diff = DIFFERENTIAL;
    // }

    pid_out = calculatePid(temp, currentSetpoint, 0.4, 0.1, 0);
    burstFire();
    timer.setTimer(BURST_PERIOD * BURST_RESOLUT, burstFire, BURST_RESOLUT - 1);

    float proportionalPower = 10.0 * 230 * pid_out / 10;
    uint32_t delta_t        = 3600 * 1000 / proportionalPower + 1000;

    timer.changeInterval(safetyTimer, delta_t);
    led.setValue((uint16_t)(pid_out * 25.5));

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
        NOTIFY("Reached Temp, after: %d:%d", h, m);
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

  if (currentSetpoint == 0)
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
  if (currentSetpoint < 760) {
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

  timer.setInterval(1970L, getTemp);
  timer.setInterval(10000L, sendData);

  safetyTimer  = timer.setInterval(2115L, safetyCheck); // 2100ms =~ 7.5A

  controlTimer = timer.setInterval(BURST_PERIOD * BURST_RESOLUT, tControl);
  timer.disable(controlTimer); // enable it after button is pressed

  rampTimer = timer.setInterval(RATEUPDATE * 1000L, rampRate);
  timer.disable(rampTimer); // enable it after button is pressed

  slowCool = timer.setInterval(RATEUPDATE * 1000L, rampDown);
  timer.disable(slowCool);

  errorLog             = new PapertrailLogger(PAPERTRAIL_HOST, PAPERTRAIL_PORT, LogLevel::Error, "\033[0;31m", "papertrail-test", "testing");
  String resetReason   = ESP.getResetReason();
  uint32_t resetNumber = system_get_rst_info()->reason;
  errorLog->printf("Reset Reason [%d] %s\n", resetNumber, resetReason.c_str());
}

void loop()
{
  ArduinoOTA.handle();
  Blynk.run();
  timer.run();
}