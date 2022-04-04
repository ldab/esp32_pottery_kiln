#include "Arduino.h"

const char HTTP_INFO[] PROGMEM = R"rawliteral(
    <html lang="en">

  <head>
    <meta name="format-detection" content="telephone=no">
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width,initial-scale=1,user-scalable=no">
    <title>%HTML_HEAD_TITLE%</title>
    <script>
      function c(l) {
        document.getElementById('s').value = l.getAttribute('data-ssid') || l.innerText || l.textContent;
        p = l.nextElementSibling.classList.contains('l');
        document.getElementById('p').disabled = !p;
        if (p) document.getElementById('p').focus();
      }

    </script>
    <style>
    %CSS_TEMPLATE%
    </style>
  </head>

  <body class="invert">
    <div class="wrap">
      <div class="msg S">%HTML_INFO_BOX%</em></div>
      <h3>esp32</h3>
      <hr>
      <dl>
        <dt>Uptime</dt>
        <dd>1 Mins 16 Secs</dd>
        <dt>Chip ID</dt>
        <dd>4ea4ae30</dd>
        <dt>Chip Rev</dt>
        <dd>1</dd>
        <dt>Flash Size</dt>
        <dd>2097152 bytes</dd>
        <dt>PSRAM Size</dt>
        <dd>0 bytes</dd>
        <dt>CPU Frequency</dt>
        <dd>240MHz</dd>
        <dt>Memory - Free Heap</dt>
        <dd>254144 bytes available</dd>
        <dt>Memory - Sketch Size</dt>
        <dd>Used / Total bytes<br>907104 / 2217824<br><progress value="907104" max="2217824"></progress></dd><br>
        <h3>WiFi</h3>
        <hr>
        <dt>Connected</dt>
        <dd>Yes</dd>
        <dt>Station SSID</dt>
        <dd>ubx</dd>
        <dt>Station IP</dt>
        <dd>10.12.71.29</dd>
        <dt>Station Gateway</dt>
        <dd>10.12.71.254</dd>
        <dt>Station Subnet</dt>
        <dd>255.255.255.0</dd>
        <dt>DNS Server</dt>
        <dd>195.34.89.241</dd>
        <dt>Hostname</dt>
        <dd>toilet</dd>
        <dt>Station MAC</dt>
        <dd>30:AE:A4:4E:B5:F4</dd>
        <dt>Access Point IP</dt>
        <dd>192.168.4.1</dd>
        <dt>Access Point MAC</dt>
        <dd>30:AE:A4:4E:B5:F5</dd>
        <dt>Access Point Hostname</dt>
        <dd></dd>
        <dt>BSSID</dt>
        <dd>80:2A:A8:03:5A:DE</dd>
      </dl>
      <h3>About</h3>
      <hr>
      <dl>
        <dt>WiFiManager</dt>
        <dd>v1.0.10-beta</dd>
        <dt>Arduino</dt>
        <dd>1.0.6</dd>
        <dt>Build Date</dt>
        <dd>Mar 29 2022 10:59:00</dd>
      </dl>
      <form action="/update" method="get"><button>Update</button></form><br>
      <hr><br><br>
      <form action="/erase" method="get"><button class="D">Erase WiFi Config</button></form><br>
      <p>Github <a href="https://github.com/tzapu/WiFiManager">https://github.com/tzapu/WiFiManager</a>.</p>
    </div>
  </body>

</html>
)rawliteral";