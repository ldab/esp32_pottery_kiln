#include "Arduino.h"

const char HTTP_CONFIG[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <title>%HTML_HEAD_TITLE%</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <link rel="icon" href="data:,">
  <link rel="stylesheet" href="https://use.fontawesome.com/releases/v5.7.2/css/all.css" integrity="sha384-fnmOCqbTlWIlj8LyTjo7mOUStjsKC4pOpQbqyi7RrhN7udi9RwhKkMHpvLbHG9Sr" crossorigin="anonymous">
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
              <label for="server">Server</label>
              <input type="text" id ="server" name="server"><br>
              <label for="token">Token</label>
              <input type="text" id ="token" name="token"><br>
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
