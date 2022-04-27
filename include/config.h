#include "Arduino.h"

const char HTTP_CONFIG[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta charset="UTF-8">
  <title>%HTML_HEAD_TITLE%</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <link rel="icon" href="data:,">
<style>%CSS_TEMPLATE%</style>
</head>
<body class="invert">
  <div class="wrap">
    <div class="topnav">
      <h1>%HTML_HEAD_TITLE%</h1>
    </div>
    <div class="content">
      <div class="card-grid">
        <div class="card">
          <form action="/" method="POST">
            <p>
              <label for="ssid">SSID</label>
              <input type="text" id ="ssid" name="ssid"><br>
              <label for="pass">Password</label>
              <input type="text" id ="pass" name="pass"><br>
              <label for="server">MQTT Server</label>
              <input type="text" id ="server" name="server" value="io.adafruit.com"><br>
              <label for="user">MQTT Username</label>
              <input type="text" id ="user" name="user"><br>
              <label for="mqtt_pass">MQTT Password</label>
              <input type="text" id ="mqtt_pass" name="mqtt_pass"><br>
              <label for="port">Port</label>
              <input type="number" id ="port" name="port"min="0" max="9999" value=1883><br>
              <input type ="submit" value ="Submit">
            </p>
          </form>
        </div>
      </div>
    </div>
  </div>
</body>
</html>
)rawliteral";
