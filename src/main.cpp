/******************************************************************************
main.cpp
ESP8266 based Kiln controller, thermocouple type K, MAX31855
Leonardo Bispo
March, 2022
https://github.com/ldab/kiln
Distributed as-is; no warranty is given.
******************************************************************************/

#include "ArduinoJson.h"

extern "C" {
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"
}

#include "ArduinoOTA.h"
#include <Arduino.h>
#include <WiFi.h>

#include <AsyncMqttClient.h>

#include "esp_system.h"
#include <Ticker.h>
#include <pthread.h>

#include <DNSServer.h>
#include <ESPmDNS.h>

#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <Update.h>

#include <HTTPClient.h>
#include <WiFiClientSecure.h>

#include "SPIFFS.h"

// PapertrailLogger
#include "PapertrailLogger.h"
#include <WiFiUdp.h>

// MAX31855
#include <SPI.h>
#include <Wire.h>

#include "Adafruit_MAX31855.h"

#include "time.h"

#include "html_strings.h"

#define PAPERTRAIL_HOST "logs2.papertrailapp.com"
#define PAPERTRAIL_PORT 53139

#ifdef VERBOSE
#define DBG(msg, ...)                                                          \
  {                                                                            \
    Serial.printf("[%lu] " msg, millis(), ##__VA_ARGS__);                      \
    Serial.flush();                                                            \
  }
#else
#define DBG(...)
#endif

#define NOTIFY(msg, ...)                                                       \
  {                                                                            \
    char _msg[64] = "";                                                        \
    sprintf(_msg, msg, ##__VA_ARGS__);                                         \
    Blynk.logEvent("alarm", _msg);                                             \
    DBG("%s\n", _msg);                                                         \
  }

// https://randomnerdtutorials.com/esp8266-pinout-reference-gpios/
#define RELAY        16
#define POWER        17
#define LED_R        25
#define LED_G        26
#define LED_B        27
#define SPI_CS       22
#define SPI_MISO     21
#define SPI_CLK      23
#define I2C_SDA      32
#define I2C_SCL      33

// Heating Rate (°C/hr) during the last 100°C of Firing
#define SLOWFIRE     15
#define MEDIUMFIRE   60
#define FASTFIRE     150

#define COSTKWH      2.14

#define RATEUPDATE   60 // every 60 seconds

#define DIFFERENTIAL 5 // degC

const char *p_mqtt   = "/mqtt.txt";
char mqtt_user[64]   = {'\0'};
char mqtt_pass[64]   = {'\0'};
char mqtt_server[64] = {'\0'};
uint16_t mqtt_port   = 1883;

String ssid, pass;

float temp;
float tInt;
float currentSetpoint = -9999;
std::vector<float> readings;
std::vector<long> epocTime;
volatile float instPower;
volatile float energy = 0;
volatile float current;

// Control variables
volatile uint32_t energyMillis = 0;
uint32_t initMillis            = 0;
uint32_t holdMillis            = 0;
int step                       = 0;

// Timer instance numbers
Ticker controlTimer;
Ticker safetyTimer;
Ticker rampTimer;
Ticker slowCool;
Ticker tempTimer;
Ticker sendTimer;
Ticker restart;
pthread_t graphThread;

DNSServer dnsServer;

AsyncWebServer server(80);
AsyncEventSource events("/events"); // event source (Server-Sent events)
AsyncWebSocket ws("/ws");           // access at ws://[esp ip]/ws

Adafruit_MAX31855 thermocouple(SPI_CLK, SPI_CS, SPI_MISO);

PapertrailLogger *errorLog;

AsyncMqttClient mqttClient;
TimerHandle_t mqttReconnectTimer;
TimerHandle_t wifiReconnectTimer;

void printSegments();
void rampRate();
void tControl();
String processor(const String &var);
String readFile(fs::FS &fs, const char *path);
void writeFile(fs::FS &fs, const char *path, const char *message);

class CaptiveRequestHandler : public AsyncWebHandler
{
  public:
  CaptiveRequestHandler() {}
  virtual ~CaptiveRequestHandler() {}

  bool canHandle(AsyncWebServerRequest *request)
  {
    // request->addInterestingHeader("ANY");
    return true;
  }

  void handleRequest(AsyncWebServerRequest *request)
  {
    request->send_P(200, "text/html", HTTP_CONFIG, processor);
  }
};

void espRestart() { ESP.restart(); }

void *sendGraph(void *)
{
  char msg[8];
  for (uint16_t i = 0; i < readings.size(); i++) {
    sprintf(msg, "%.01f", readings[i]);
    events.send(msg, "temperature");
    vTaskDelay(pdMS_TO_TICKS(10));
  }
  // pthread_exit(NULL);
}

// Send notification to HA, max 32 bytes
void notify(char *msg, size_t length)
{
  char topic[64] = {'\0'};
  sprintf(topic, "%s/f/kiln/notify", mqtt_user);
  mqttClient.publish(topic, 0, false, msg, length);
}

void onUpload(AsyncWebServerRequest *request, String filename, size_t index,
              uint8_t *data, size_t len, bool final)
{
  if (!index) {
    Serial.printf("Update Start: %s\n", filename.c_str());
    if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
      Update.printError(Serial);
    }
  }
  Serial.printf("Progress: %u of %u\r", Update.progress(), Update.size());
  if (!Update.hasError()) {
    if (Update.write(data, len) != len) {
      Update.printError(Serial);
    }
  }
  if (final) {
    if (Update.end(true)) {
      Serial.printf("Update Success: %uB\n", index + len);
      request->redirect("/");
      restart.once_ms(1000, espRestart);
    } else {
      Update.printError(Serial);
    }
  }
}

void onRequest(AsyncWebServerRequest *request)
{
  // Handle Unknown Request
  request->send(404, "text/plain", "OUCH");
}

void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client,
             AwsEventType type, void *arg, uint8_t *data, size_t len)
{
  // Handle WebSocket event
}

String processor(const String &var)
{
  if (var == "CSS_TEMPLATE")
    return FPSTR(HTTP_STYLE);
  if (var == "INDEX_JS")
    return FPSTR(HTTP_JS);
  if (var == "HTML_HEAD_TITLE")
    return FPSTR(HTML_HEAD_TITLE);
  if (var == "HTML_INFO_BOX") {
    String ret = "";
    if (WiFi.isConnected()) {
      ret = "<strong> Connected</ strong> to ";
      ret += WiFi.SSID();
      ret += "<br><em><small> with IP ";
      ret += WiFi.localIP().toString();
      ret += "</small>";
    } else
      ret = "<strong> Not Connected</ strong>";
    return ret;
  }
  if (var == "UPTIME") {
    String ret = String(millis() / 1000 / 60);
    ret += " min ";
    ret += String((millis() / 1000) % 60);
    ret += " sec";
    return ret;
  }
  if (var == "CHIP_ID") {
    String ret = String((uint32_t)ESP.getEfuseMac());
    return ret;
  }
  if (var == "FREE_HEAP") {
    String ret = String(ESP.getFreeHeap());
    ret += " bytes";
    return ret;
  }
  if (var == "SKETCH_INFO") {
    //%USED_BYTES% / &FLASH_SIZE&<br><progress value="%USED_BYTES%"
    // max="&FLASH_SIZE&">
    String ret = String(ESP.getSketchSize());
    ret += " / ";
    ret += String(ESP.getFlashChipSize());
    ret += "<br><progress value=\"";
    ret += String(ESP.getSketchSize());
    ret += "\" max=\"";
    ret += String(ESP.getFlashChipSize());
    ret += "\">";
    return ret;
  }
  if (var == "HOSTNAME")
    return String(WiFi.getHostname());
  if (var == "MY_MAC")
    return WiFi.macAddress();
  if (var == "FW_VER")
    return String(FIRMWARE_VERSION);
  if (var == "SDK_VER")
    return String(esp_get_idf_version());
  if (var == "ABOUT_DATE") {
    String ret = String(__DATE__) + " " + String(__TIME__);
    return ret;
  }
  if (var == "GRAPH_DATA") {
    String graphString;
    graphString.reserve(readings.size() * 2);
    graphString = "[";
    for (size_t i = 0; i < readings.size() - 1; i++) {
      graphString += "[";
      graphString += String(epocTime[i]);
      graphString += ",";
      graphString += String(readings[i], 0);
      graphString += "]";
      graphString += ",";
    }
    graphString += "[";
    graphString += String(epocTime[readings.size() - 1]);
    graphString += ",";
    graphString += String(readings[readings.size() - 1], 0);
    graphString += "]";
    graphString += "]";
    Serial.println(graphString);
    return graphString;
  }

  return String();
}

void captiveServer()
{
  server.on("/", HTTP_POST, [](AsyncWebServerRequest *request) {
    int params = request->params();
    StaticJsonDocument<192> doc;
    char output[192] = {'\0'};
    for (int i = 0; i < params; i++) {
      AsyncWebParameter *p = request->getParam(i);
      if (p->isPost()) {
        // HTTP POST ssid value
        if (p->name() == "ssid") {
          ssid = p->value().c_str();
          DBG("SSID set to: %s\n", ssid.c_str());
        }
        if (p->name() == "pass") {
          pass = p->value().c_str();
          DBG("Password set to: %s\n", pass.c_str());
        }
        if (p->name() == "server") {
          doc["s"] = p->value().c_str();
        }
        if (p->name() == "mqtt_pass") {
          doc["pass"] = p->value().c_str();
        }
        if (p->name() == "user") {
          doc["u"] = p->value().c_str();
        }
        if (p->name() == "port") {
          doc["port"] = p->value().c_str();
        }
      }
    }
    serializeJson(doc, output);
    DBG("%s\n", output);
    writeFile(SPIFFS, p_mqtt, output);

    WiFi.persistent(true);
    WiFi.begin(ssid.c_str(), pass.c_str());
    DBG("Connecting to WiFi ..");
    while (WiFi.status() != WL_CONNECTED) {
      Serial.print('.');
      delay(100);
    }
    DBG("Connected\n");
    restart.once_ms(1000, espRestart);
    request->redirect("http://" + WiFi.localIP().toString());
  });
}

// Read File from SPIFFS
String readFile(fs::FS &fs, const char *path)
{
  DBG("Reading file: %s\r\n", path);

  File file = fs.open(path);
  if (!file || file.isDirectory()) {
    DBG("- failed to open file for reading\n");
    return String();
  }

  String fileContent;
  while (file.available()) {
    fileContent = file.readStringUntil('\n');
    DBG("Read: %s\n", fileContent.c_str());
    break;
  }
  return fileContent;
}

// Write file to SPIFFS
void writeFile(fs::FS &fs, const char *path, const char *message)
{
  Serial.printf("Writing file: %s\r\n", path);

  File file = fs.open(path, FILE_WRITE);
  if (!file) {
    Serial.println("- failed to open file for writing");
    return;
  }
  if (file.print(message)) {
    Serial.println("- file written");
  } else {
    Serial.println("- frite failed");
  }
}

// BLYNK_CONNECTED()
// {
//   String resetReason   = ESP.getResetReason();
//   uint32_t resetNumber = system_get_rst_info()->reason;

//   if (resetNumber != REASON_DEFAULT_RST && resetNumber != REASON_SOFT_RESTART
//   &&
//       resetNumber != REASON_EXT_SYS_RST) {
//     // Restore data from the cloud
//     for (size_t i = 11; i < 15; i++) {
//       Blynk.syncVirtual(i);
//       for (size_t j = 10; j < 25; j += 10) {
//         Blynk.syncVirtual(i + j);
//       }
//     }

//     Blynk.syncVirtual(V3, V9, V50);

//     // Blynk.syncVirtual(V10);

//     char resetInfo[32];
//     sprintf(resetInfo, "%s epc1=0x%08x", resetReason.c_str(),
//             system_get_rst_info()->epc1);
//     Blynk.logEvent("info", resetInfo);
//   } else if (!timer.isEnabled(controlTimer)) {
//     Blynk.virtualWrite(V5, 0);
//     Blynk.virtualWrite(V10, 0);
//     Blynk.virtualWrite(V7, "Idle 💤");
//     led.off();
//   }
// }

// temperature, rate, hold/soak (min)
int segments[4][3]     = {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}};
const char *p_segments = "/segments.txt";

// BLYNK_WRITE(V11) { segments[0][0] = param.asInt(); }
// BLYNK_WRITE(V21) { segments[0][1] = param.asInt(); }
// BLYNK_WRITE(V31) { segments[0][2] = param.asInt(); }

// BLYNK_WRITE(V12) { segments[1][0] = param.asInt(); }
// BLYNK_WRITE(V22) { segments[1][1] = param.asInt(); }
// BLYNK_WRITE(V32) { segments[1][2] = param.asInt(); }

// BLYNK_WRITE(V13) { segments[2][0] = param.asInt(); }
// BLYNK_WRITE(V23) { segments[2][1] = param.asInt(); }
// BLYNK_WRITE(V33) { segments[2][2] = param.asInt(); }

// BLYNK_WRITE(V14) { segments[3][0] = param.asInt(); }
// BLYNK_WRITE(V24) { segments[3][1] = param.asInt(); }
// BLYNK_WRITE(V34) { segments[3][2] = param.asInt(); }

// BLYNK_WRITE(V5) { segments[3][2] = param.asInt() / 60; } TODO initMillis to
// UNIX
// BLYNK_WRITE(V3) { energy = param.asFloat(); }
// BLYNK_WRITE(V9) { currentSetpoint = param.asFloat(); }
// BLYNK_WRITE(V50) { step = param.asInt(); }

void onFire(AsyncWebServerRequest *request)
{
  int params = request->params();

  for (int i = 0; i < params; i++) {
    AsyncWebParameter *p = request->getParam(i);
    if (p->isPost()) {

      DBG("POST: %s\n", p->name().c_str());

      if (p->name() == "s00")
        segments[0][0] = p->value().toInt();
      if (p->name() == "s01")
        segments[0][1] = p->value().toInt();
      if (p->name() == "s02")
        segments[0][2] = p->value().toInt();
      if (p->name() == "s10")
        segments[1][0] = p->value().toInt();
      if (p->name() == "s11")
        segments[1][1] = p->value().toInt();
      if (p->name() == "s12")
        segments[1][2] = p->value().toInt();
      if (p->name() == "s20")
        segments[2][0] = p->value().toInt();
      if (p->name() == "s21")
        segments[2][1] = p->value().toInt();
      if (p->name() == "s22")
        segments[2][2] = p->value().toInt();
      if (p->name() == "s30")
        segments[3][0] = p->value().toInt();
      if (p->name() == "s31")
        segments[3][1] = p->value().toInt();
      if (p->name() == "s32")
        segments[3][2] = p->value().toInt();
    }
  }

  for (size_t i = 0; i < sizeof(segments) / sizeof(segments[0]); i++) {
    for (size_t j = 0; j < sizeof(segments[0]) / (sizeof(int)) - 1; j++) {
      if (segments[i][j] == 0) {
        DBG("Check the settings, i: %u j: %u\n", i, j);
        return;
      }
    }
  }

  if (/*segments[3][0] < segments[2][0] ||*/ segments[2][0] < segments[1][0] ||
      segments[1][0] < segments[0][0]) {
    DBG("Invalid Target temperature");
    return;
  }

  StaticJsonDocument<384> doc;
  char output[384]   = {'\0'};

  JsonObject preheat = doc.createNestedObject("preheat");
  preheat["st"]      = segments[0][0];
  preheat["r"]       = segments[0][1];
  preheat["h"]       = segments[0][2];

  JsonObject step1   = doc.createNestedObject("step1");
  step1["st"]        = segments[1][0];
  step1["r"]         = segments[1][1];
  step1["h"]         = segments[1][2];

  JsonObject step2   = doc.createNestedObject("step2");
  step2["st"]        = segments[2][0];
  step2["r"]         = segments[2][1];
  step2["h"]         = segments[2][2];

  JsonObject final   = doc.createNestedObject("final");
  final["st"]        = segments[3][0];
  final["r"]         = segments[3][1];
  final["h"]         = segments[3][2];

  serializeJson(doc, output);
  writeFile(SPIFFS, p_segments, output);

  // int pinValue = param.asInt();

  if (true) {
    initMillis = millis();
    int tTotal = (segments[0][0] - temp) / segments[0][1] * 60 + segments[0][2];
    tTotal += (segments[1][0] - segments[0][0]) * 60 / segments[1][1] +
              segments[1][2];
    tTotal += (segments[2][0] - segments[1][0]) * 60 / segments[2][1] +
              segments[2][2];
    tTotal += (segments[3][0] - segments[2][0]) * 60 / segments[3][1] +
              segments[3][2];

    DBG("tTotal %dmin\n", tTotal);

    // Blynk.setProperty(V5, "max", tTotal);
    // Blynk.setProperty(V0, "max", segments[3][0]);
    // Blynk.virtualWrite(V7, "Firing 🔥 @" + String(segments[step][0]) + "°C");

    printSegments();
    rampRate();
    controlTimer.attach_ms(5530L, tControl);
    rampTimer.attach_ms(RATEUPDATE * 1000L, rampRate);
  } else {
    DBG("Button pressed, disable temp control\n");
    // Blynk.virtualWrite(V7, "Idle 💤");
    // ESP.restart();
    digitalWrite(RELAY, LOW);
    step            = 0;
    holdMillis      = 0;
    currentSetpoint = -9999;
    controlTimer.detach();
    rampTimer.detach();
  }
}

void sendData()
{
  StaticJsonDocument<192> doc;
  char payload[192];

  JsonObject feeds = doc.createNestedObject("feeds");
  feeds["T"]       = temp;
  feeds["I"]       = current;
  feeds["P"]       = instPower;
  feeds["E"]       = energy;
  feeds["$"]       = energy * COSTKWH;
  feeds["Tint"]    = tInt;
  feeds["St"]      = currentSetpoint;
  feeds["Step"]    = step;
  feeds["RSSI"]    = WiFi.RSSI();

  serializeJson(doc, payload);

  char topic[64] = {'\0'};
  sprintf(topic, "%s/g/kiln/json", mqtt_user);

  mqttClient.publish(topic, 0, false, payload, strlen(payload));

  DBG("Publish: %s\n", payload);

  current   = 0;
  instPower = 0;
  if (true /*controlTimer.active()*/) {
    // Blynk.virtualWrite(V5, (int)((millis() - initMillis) / (60 * 1000)));
  }
}

void safetyCheck()
{
  static bool tIntError  = false;
  static bool noRlyError = false;

  // if (controlTimer.active()) {
  //   if (temp > (currentSetpoint + 10)) // TODO check differential
  //   {
  //     DBG("HIGH TEMPERATURE ALARM\n");
  //   } else if (temp < (currentSetpoint - 20)) {
  //     DBG("LOW TEMPERATURE ALARM\n");
  //   }
  // }

  if (tInt > 60) {
    if (!tIntError) {
      char tIntChar[8];
      sprintf(tIntChar, "%.1f°C", tInt);
      DBG("High internal temp: %s", tIntChar);
      // Blynk.logEvent("highIntTemp", tIntChar);
      tIntError = true;
    }
  } else {
    tIntError = false;
  }

  if (digitalRead(RELAY)) {
    if ((millis() - energyMillis) > 2000L) {
      if (!noRlyError) {
        DBG("PROBLEM 2300W expect 1 pulse every ~1565ms\n");
        // Blynk.logEvent("noRlyError");
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

void IRAM_ATTR readPower()
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
      // Blynk.logEvent("shortErr", shortError);
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
      // Blynk.logEvent("thermocouple_error", error);

      digitalWrite(RELAY, LOW);
    }
  } else {
    static uint32_t log = millis();
    tErr                = false;
    DBG("T: %.02fdegC\n", temp);

    char msg[8];
    sprintf(msg, "%d", WiFi.RSSI());

    struct tm timeinfo;
    if ((millis() - log) > (1000) && getLocalTime(&timeinfo)) {
      time_t epoc = mktime(&timeinfo);
      epocTime.push_back((long)epoc);
      readings.push_back(WiFi.RSSI() / 1.0);
      DBG("strlen: %u\n", readings.size());
      log = millis();
    }

    events.send(msg, "temperature");
  }
}

void holdTimer(uint32_t _segment)
{
  if (holdMillis == 0) {
    DBG("Start hold for %dmin\n", _segment);
    holdMillis = millis();
    rampTimer.detach();
  }
  uint32_t _elapsed = (millis() - holdMillis) / (60 * 1000);
  // Blynk.virtualWrite(V7, "Hold: " + String(currentSetpoint, 0) + "°C-" +
  //                            String(_elapsed) + "/" + String(_segment) +
  //                            "min");

  if (_elapsed >= _segment) {
    step++;
    holdMillis = 0;
    rampTimer.attach_ms(RATEUPDATE * 1000L, rampRate);
    DBG("Done with hold, step: %d\n", step);
    // Blynk.virtualWrite(V7, "Firing 🔥 @" + String(segments[step][0]) + "°C");
  }
}

void rampDown()
{
  // https://digitalfire.com/schedule/04dsdh
  if (currentSetpoint < 760 || step == 5) {
    controlTimer.detach();
    slowCool.detach();
    currentSetpoint = 0;
    tControl();
    // Blynk.virtualWrite(V7, "Cooling ❄️");
  }

  currentSetpoint -= (float)(83.0 / (3600.0f / RATEUPDATE));
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
        // timer.restartTimer(safetyTimer);
        diff = 0;
      }
    } else if (digitalRead(RELAY)) {
      digitalWrite(RELAY, LOW);
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
        // Blynk.logEvent("info", endInfo);
        // Blynk.virtualWrite(V7, "Slow Cooling ❄️");
        slowCool.attach_ms(RATEUPDATE * 1000L, rampDown);
        rampTimer.detach();
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

void pinInit()
{
  pinMode(POWER, INPUT);
  // attachInterrupt(POWER, readPower, FALLING);

  pinMode(RELAY, OUTPUT);
  digitalWrite(RELAY, LOW);

  pinMode(LED_R, OUTPUT);
  pinMode(LED_G, OUTPUT);
  pinMode(LED_B, OUTPUT);
  digitalWrite(LED_R, HIGH);
  digitalWrite(LED_G, HIGH);
  digitalWrite(LED_B, HIGH);
}

void onMqttConnect(bool sessionPresent) { DBG("Connected to MQTT.\n"); }

void onMqttDisconnect(AsyncMqttClientDisconnectReason reason)
{
  DBG("Disconnected from MQTT, reason: %u\n", reason);

  if (WiFi.isConnected()) {
    xTimerStart(mqttReconnectTimer, 0);
  }
}

void connectToMqtt()
{
  DBG("Connecting to MQTT...\n");
  mqttClient.connect();
}

void WiFiEvent(WiFiEvent_t event)
{
  Serial.printf("[WiFi-event] event: %d\n", event);
  switch (event) {
  case SYSTEM_EVENT_STA_GOT_IP:
    // connectToMqtt();
    break;
  case SYSTEM_EVENT_STA_DISCONNECTED:
    xTimerStop(mqttReconnectTimer,
               0); // don't reconnect to MQTT while reconnecting WiFi
    break;
  }
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
  DBG("VERSION %s\n", FIRMWARE_VERSION);
#endif

  readings.reserve(2000);
  epocTime.reserve(2000);

#ifdef CALIBRATE
  // Measure GPIO in order to determine Vref to gpio 25 or 26 or 27
  adc2_vref_to_gpio(GPIO_NUM_25);
  delay(5000);
  abort();
#endif

  pinInit();

  WiFi.mode(WIFI_STA);
  WiFi.onEvent(WiFiEvent);
  WiFi.begin();

  // Initialize SPIFFS
  if (!SPIFFS.begin(true)) {
    Serial.println("An Error has occurred while mounting SPIFFS");
    return;
  }

  if (!thermocouple.begin()) {
    DBG("ERROR.\n");
    // while (1) delay(10);
  } else
    DBG("MAX31855 Good\n");

  if (WiFi.waitForConnectResult() == WL_DISCONNECTED ||
      WiFi.waitForConnectResult() == WL_NO_SSID_AVAIL) { //~ 100 * 100ms
    Serial.printf("WiFi Failed!: %u\n", WiFi.status());

    captiveServer();

    WiFi.softAP("esp-captive");

    server.onNotFound(
        [](AsyncWebServerRequest *request) { request->redirect("/"); });
    dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
    dnsServer.start(53, "*", WiFi.softAPIP());

    DBG("Start Captive Portal at: %s\n", WiFi.softAPIP().toString().c_str());

    server.addHandler(new CaptiveRequestHandler())
        .setFilter(ON_AP_FILTER); // only when requested from AP
  } else {
    DBG("WiFi Connected, IP: %s\n", WiFi.localIP().toString().c_str());

    configTzTime("CET-1CEST,M3.5.0,M10.5.0/3", "0.pool.ntp.org",
                 "1.pool.ntp.org");

    char input[192] = {'\0'};
    sprintf(input, "%s", readFile(SPIFFS, p_mqtt).c_str());

    StaticJsonDocument<64> doc;
    DeserializationError error = deserializeJson(doc, input);

    if (error) {
      DBG("deserializeJson() failed: %s\n", error.c_str());
      return;
    }

    const char *u = doc["u"];
    const char *s = doc["s"];          // "adafruitojdfoisdjfosdijfoi.com"
    const char *p = doc["pass"];       // "12345678123456781234567812345678"
    mqtt_port     = atoi(doc["port"]); // 9999

    strcpy(mqtt_user, u);
    strcpy(mqtt_pass, p);
    strcpy(mqtt_server, s);

    mqttClient.setServer(mqtt_server, 1883);
    mqttClient.setCredentials(mqtt_user, mqtt_pass);

    mqttClient.onConnect(onMqttConnect);
    mqttClient.onDisconnect(onMqttDisconnect);
    connectToMqtt();

    // https://github.com/espressif/arduino-esp32/blob/master/libraries/ESP32/examples/ResetReason/ResetReason.ino
    esp_reset_reason_t reset_reason = esp_reset_reason();
    if (reset_reason == ESP_RST_PANIC || reset_reason == ESP_RST_INT_WDT ||
        reset_reason == ESP_RST_TASK_WDT || reset_reason == ESP_RST_WDT ||
        reset_reason == ESP_RST_BROWNOUT) {
      char rstMsg[12];
      sprintf(rstMsg, "WDT= %u", reset_reason);
      // notify(rstMsg, strlen(rstMsg));
    }

    MDNS.begin("kiln");

    server.addHandler(&events);

    events.onConnect([](AsyncEventSourceClient *client) {
      DBG("Client connected!\n");

      // pthread_attr_t attr;
      // pthread_attr_init(&attr);
      // pthread_attr_setstacksize(&attr, 16384);
      // pthread_create(&graphThread, &attr, sendGraph, NULL);
      // pthread_detach(graphThread);

      // client->send("hello!", NULL, millis(), 10000);
    });

    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
      request->send_P(200, "text/html", HTTP_INDEX, processor);
    });

    server.on("/", HTTP_POST, [](AsyncWebServerRequest *request) {
      onFire(request);
      request->send_P(200, "text/html", HTTP_INDEX, processor);
    });

    server.on("/setup", HTTP_GET, [](AsyncWebServerRequest *request) {
      request->send_P(200, "text/html", HTTP_SETUP, processor);
    });

    server.on("/config", HTTP_GET, [](AsyncWebServerRequest *request) {
      request->send_P(200, "text/html", HTTP_CONFIG, processor);
    });

    server.on("/info", HTTP_GET, [](AsyncWebServerRequest *request) {
      request->send_P(200, "text/html", HTTP_INFO, processor);
    });

    server.on("/update", HTTP_GET, [](AsyncWebServerRequest *request) {
      request->send_P(200, "text/html", HTTP_UPDATE, processor);
    });

    server.on("/notify", HTTP_GET, [](AsyncWebServerRequest *request) {
      request->send_P(200, "text/plain", "OK");
    });

    server.on(
        "/u", HTTP_POST,
        [](AsyncWebServerRequest *request) {
          AsyncWebServerResponse *response = request->beginResponse(
              200, "text/plain", Update.hasError() ? "OK" : "FAIL");
          response->addHeader("Connection", "close");
          request->send(response);
        },
        onUpload);

    server.on("/gpio", HTTP_GET, [](AsyncWebServerRequest *request) {
      String inputMessage1;
      String inputMessage2;
      // GET input1 value on
      // <ESP_IP>/gpio?output=<inputMessage1>&state=<inputMessage2>
      if (request->hasParam("output") && request->hasParam("state")) {
        inputMessage1 = request->getParam("output")->value();
        inputMessage2 = request->getParam("state")->value();
        digitalWrite(inputMessage1.toInt(), inputMessage2.toInt());
      } else {
        inputMessage1 = "No message sent";
        inputMessage2 = "No message sent";
      }
      Serial.print("GPIO: ");
      Serial.print(inputMessage1);
      Serial.print(" - Set to: ");
      Serial.println(inputMessage2);
      request->send(200, "text/plain", "OK");
    });

    server.on("/scan", HTTP_GET, [](AsyncWebServerRequest *request) {
      String json = "[";
      int n       = WiFi.scanComplete();
      if (n == WIFI_SCAN_FAILED)
        WiFi.scanNetworks(false, false, false, 100);

      n = WiFi.scanComplete();

      if (n) {
        for (int i = 0; i < n; ++i) {
          if (i)
            json += ",";
          json += "{";
          json += "\"rssi\":" + String(WiFi.RSSI(i));
          json += ",\"ssid\":\"" + WiFi.SSID(i) + "\"";
          json += ",\"bssid\":\"" + WiFi.BSSIDstr(i) + "\"";
          json += ",\"channel\":" + String(WiFi.channel(i));
          json += ",\"secure\":" + String(WiFi.encryptionType(i));
          json += "}";
        }
        WiFi.scanDelete();
      }
      json += "]";
      Serial.println(json);
      request->send(200, "application/json", json);
      json = String();
    });

    tempTimer.attach(2, getTemp);
  }

  server.onNotFound(onRequest);
  server.begin();

  mqttReconnectTimer =
      xTimerCreate("mqttTimer", pdMS_TO_TICKS(2000), pdFALSE, (void *)0,
                   reinterpret_cast<TimerCallbackFunction_t>(connectToMqtt));

  // otaInit();

  safetyTimer.attach_ms(2115L, safetyCheck);
  sendTimer.attach_ms(10000L, sendData);

  // controlTimer = timer.setInterval(5530L, tControl);
  // timer.disable(controlTimer); // enable it after button is pressed

  // rampTimer = timer.setInterval(RATEUPDATE * 1000L, rampRate);
  // timer.disable(rampTimer); // enable it after button is pressed

  // slowCool = timer.setInterval(RATEUPDATE * 1000L, rampDown);
  // timer.disable(slowCool);

  esp_reset_reason_t reset_reason = esp_reset_reason();
  if (reset_reason == ESP_RST_PANIC || reset_reason == ESP_RST_INT_WDT ||
      reset_reason == ESP_RST_TASK_WDT || reset_reason == ESP_RST_WDT ||
      reset_reason == ESP_RST_BROWNOUT) {
    errorLog =
        new PapertrailLogger(PAPERTRAIL_HOST, PAPERTRAIL_PORT, LogLevel::Error,
                             "\033[0;31m", "untrol.io", "kiln");

    DBG("Reset Reason [%d]\n", reset_reason);
    errorLog->printf("Reset Reason [%d]\n", reset_reason);

    // Blynk.syncVirtual(V10);
  }

  server.onNotFound(onRequest);
  server.begin();
}

void loop()
{
  if (WiFi.getMode() == WIFI_MODE_AP || WiFi.getMode() == WIFI_MODE_APSTA)
    dnsServer.processNextRequest();

  // ArduinoOTA.handle();
}