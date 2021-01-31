/******************************************************************************
main.cpp
ESP8266 based Kiln controller, thermocouple type K, MAX31855
Leonardo Bispo
Feb, 2021
https://github.com/ldab/kiln
Distributed as-is; no warranty is given.
******************************************************************************/

#include <Arduino.h>

#define BLYNK_PRINT Serial

// Blynk and WiFi
#include <BlynkSimpleEsp8266.h>
#include <ESP8266WiFi.h>
#include <TimeLib.h>

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
#define DBG(msg, ...) \
  { Serial.printf("[%lu] " msg, millis(), ##__VA_ARGS__); }
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
#define BUZZER D0  // GPIO12
#define RELAY D1   // GPIO5
#define POWER D2   // GPIO4

#define BUZZER_CHANNEL 1

// Heating Rate (°C/hr) during the last 100°C of Firing
#define SLOWFIRE 15
#define MEDIUMFIRE 60
#define FASTFIRE 150

#define COSTKWH 2.14

#define RATEUPDATE 60  // every 60 seconds

// Update these with values suitable for your network.
const char *wifi_ssid = s_wifi_ssid;
const char *wifi_password = s_wifi_password;
const char *mqtt_server = s_mqtt_server;
const char *mqtt_user = s_mqtt_user;
const char *mqtt_pass = s_mqtt_pass;
uint16_t mqtt_port = s_mqtt_port;
const char *blynk_auth = s_blynk_auth;

/*
{
    "to": "email",
    "state": "off",
    "attributes": {
        "friendly_name": "ESP_BANANA_moisture",
        "device_class": "moisture"
    }
}
*/

float temp;
float tInt;
float currentSetpoint;
volatile float instPower;
volatile float energy;
volatile float current;

// Control variables
volatile uint32_t energyMillis = 0;
uint32_t initMillis = 0;
uint32_t holdMillis = 0;
int step = 0;

// Timer instance numbers
int controlTimer;
int safetyTimer;
int rampTimer;

// initialize the MQTT Client
WiFiClient espClient;
bool published = false;

Adafruit_MAX31855 thermocouple(PIN_SPI_SS);

BlynkTimer timer;

WidgetLED led(V6);

void printSegments();
void rampRate();

BLYNK_CONNECTED() {
  String resetReason = ESP.getResetReason();
  uint32_t resetNumber = system_get_rst_info()->reason;
  DBG("Reset Reason [%d] %s\n", resetNumber, resetReason.c_str());
  if (resetNumber != 0 && resetNumber != 4 && resetNumber != 6)
    NOTIFY(resetReason.c_str());
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

BLYNK_WRITE(V10) {
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

  if (segments[3][0] < segments[2][0] || segments[2][0] < segments[1][0] ||
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
    step = 0;
    holdMillis = 0;
    currentSetpoint = 0;
    timer.disable(controlTimer);
    timer.disable(rampTimer);
  }
}

void sendData() {
  Blynk.virtualWrite(V0, String(temp, 2));
  Blynk.virtualWrite(V1, String(current, 1));
  Blynk.virtualWrite(V2, String(instPower, 0) + "W");
  Blynk.virtualWrite(V3, energy);
  Blynk.virtualWrite(V4, String(energy * COSTKWH, 1) + "dkk");
  Blynk.virtualWrite(V8, String(tInt, 2));
  Blynk.virtualWrite(V9, String(currentSetpoint, 2));
  current = 0;
  instPower = 0;
  if (timer.isEnabled(controlTimer)) {
    Blynk.virtualWrite(V5, (int)((millis() - initMillis) / (60 * 1000)));
  }
}
/*
float tInternal() {
  // 10K NTC -> B 3977 with 10k R1 to 3.3V
  uint16_t _b = 3977;
  uint16_t _r = 10000;
  float vNtc = (float)(analogRead(A0) * 3.3 / 1024);
  float rNtc = vNtc / ((3.3 - vNtc) / 10000);

  return (float)(1 / (1 / 298.15 + 1 / _b * log(rNtc / _r))) - 273.15;
}
*/
void safetyCheck() {
  if (timer.isEnabled(controlTimer)) {
    if (temp > (currentSetpoint + 10))  // TODO check differential
    {
      DBG("HIGH TEMPERATURE ALARM\n");
    } else if (temp < (currentSetpoint - 20)) {
      DBG("LOW TEMPERATURE ALARM\n");
    }
  }

  if (tInt > 60) {
    NOTIFY("High internal temp: %.1f°C", tInt);
  }

  if (digitalRead(RELAY)) {
    if ((millis() - energyMillis) > 2000L) {
      NOTIFY("PROBLEM 2300W expect 1 pulse every ~1565ms\n");
    }
  }
}

void printSegments() {
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

/*
void setup_wifi() {
  delay(10);

  char id[32];
  sprintf(id, "%s-%s", DEVICE_NAME, WiFi.macAddress().c_str());
  DBG("Device name is %s\n", id);

  WiFi.onEvent(WiFiEvent);

  if (WiFi.SSID() != wifi_ssid) {
    DBG("Connecting to %s\n", wifi_ssid);

    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);  // TODO maybe false as WiFI should start off

    // int32_t wifi_channel = WiFi.channel();
    // uint8_t wifi_bssid[6]; //{0xF8, 0xD1, 0x11, 0x24, 0xB3, 0x84};

    // https://github.com/espressif/arduino-esp32/issues/2537
    WiFi.config(
        INADDR_NONE, INADDR_NONE,
        INADDR_NONE);  // call is only a workaround for bug in WiFi class

    // WiFi.begin(wifi_ssid, wifi_password, wifi_channel, wifi_bssid);
    WiFi.begin(wifi_ssid, wifi_password);

    WiFi.setHostname(id);

    WiFi.persistent(true);  // TODO maybe false as WiFI should start off
  }

  while (millis() < 10000)  // TODO
  {
    int8_t wifi_result = WiFi.waitForConnectResult();
    if (wifi_result == WL_CONNECTED) break;
    DBG(".");
  }
  if (WiFi.status() == WL_CONNECTED) {
    randomSeed(micros());

    mqttClient.setServer(mqtt_server, mqtt_port);
    mqttClient.setCredentials(mqtt_user, mqtt_pass);
    mqttClient.setClientId(id);
    DBG("Connecting to MQTT Server %s\n", mqtt_server);
    mqttClient.connect();
  } else {
    DBG(" WiFi connect failed: %d\n", WiFi.status());
    play(BUZZER_CHANNEL, Urgent, sizeof(Urgent) / sizeof(char *));
    esp_deep_sleep(360000000L);
  }

  while (!mqttClient.connected() && millis() < 10000) {
    delay(50);
  }
  if (!mqttClient.connected()) {
    DBG(" MQTT connect failed: %d\n", WiFi.status());
    play(BUZZER_CHANNEL, Urgent, sizeof(Urgent) / sizeof(char *));
    esp_deep_sleep(360000000L);
  }
}
*/

void inline static beep() {
  analogWrite(BUZZER, 123);

  delay(42);

  analogWrite(BUZZER, 0);
}

void static chirp(uint8_t times) {
  while (times-- > 0) {
    beep();
    delay(40);
  }
}

ICACHE_RAM_ATTR void readPower() {
  if (energyMillis == 0) {
    // We don't know the time difference between pulse, reset
    // TODO get from cloud in case MCU reset
    energy = 1 / 1000.0f;
    current = 0;
    instPower = 0;

    energyMillis = millis();
  } else {
    volatile uint32_t pulseInterval = millis() - energyMillis;

    if (pulseInterval < 100) {  // 10*sqrt(2) Amps =~ 1106.8ms
      // DBG("DEBOUNCE");
      return;
    }

    energy += 1 / 1000.0f;                           // each pulse is 1Wh
    instPower = (3600) / (pulseInterval / 1000.0f);  // 1Wh = 3600J
    current = instPower / 230.0f;

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

void getTemp() {
  static float _t = 0;
  static uint8_t _s = 0;

  temp = thermocouple.readCelsius();
  tInt = thermocouple.readInternal();
  uint8_t error = thermocouple.readError();

  // average 5x samples
  _t += temp;
  _s++;
  if (_s == 4) {
    temp = (float)(_t / _s);
    _t = 0;
    _s = 0;
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

void holdTimer(uint32_t _segment) {
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

void tControl() {
  DBG("Control ST: %.01fdegC, step: %d\n", currentSetpoint, step);
  if (!isnan(temp)) {
    // TODO Differential
    float delta_t = currentSetpoint - temp;
    if (delta_t >= 0) {
      if (!digitalRead(RELAY)) {
        digitalWrite(RELAY, HIGH);
        led.on();
        timer.restartTimer(safetyTimer);
      }
    } else if (digitalRead(RELAY)) {
      digitalWrite(RELAY, LOW);
      led.off();
    }
    if (temp > segments[step][0]) {
      currentSetpoint = segments[step][0];

      if (segments[step][2] == -1) segments[step][2] = 0;
      holdTimer(segments[step][2]);

      if (step == 4) {
        timer.disable(controlTimer);
        uint8_t h = (millis() - initMillis) / (1000 * 3600);
        uint8_t m = ((millis() - initMillis) - (h * 3600 * 1000)) / (60 * 1000);
        NOTIFY("Reached Temp, after: %d:%d", h, m);
        Blynk.virtualWrite(V7, "Cooling ❄️");
      }
    }
  }
}

void rampRate() {
  // http://www.stoneware.net/stoneware/glasyrer/firing.htm

  if (currentSetpoint == 0) currentSetpoint = temp;

  if (currentSetpoint >= segments[step][0]) {
    // TODO alarm if takes too long
    currentSetpoint = segments[step][0];
  } else {
    currentSetpoint += (float)(segments[step][1] / (3600.0f / RATEUPDATE));
  }

  DBG("Current Setpoint: %.02fdegC, step: %d\n", currentSetpoint, step);
}

void pinInit() {
  pinMode(POWER, INPUT);
  attachInterrupt(digitalPinToInterrupt(POWER), readPower, RISING);

  pinMode(RELAY, OUTPUT);
  digitalWrite(RELAY, LOW);

  pinMode(BUZZER, OUTPUT);
}

void otaInit() {
  ArduinoOTA.setHostname("kiln");

  ArduinoOTA.onStart([]() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH) {
      type = "sketch";
    } else {  // U_FS
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

void setup() {
#ifdef VERBOSE
  Serial.begin(115200);
  DBG("VERSION %s\n", VERSION);
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

  safetyTimer = timer.setInterval(2115L, safetyCheck);  // 2100ms =~ 7.5A

  controlTimer = timer.setInterval(5530L, tControl);
  timer.disable(controlTimer);  // enable it after button is pressed

  rampTimer = timer.setInterval(RATEUPDATE * 1000L, rampRate);
  timer.disable(rampTimer);  // enable it after button is pressed
}

void loop() {
  ArduinoOTA.handle();
  Blynk.run();
  timer.run();
}