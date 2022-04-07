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
        <dd>%UPTIME%</dd>
        <dt>Chip ID</dt>
        <dd>%CHIP_ID%</dd>
        <dt>Memory - Free Heap</dt>
        <dd>%FREE_HEAP%</dd>
        <dt>Memory - Sketch Size</dt>
        <dd>Used / Total bytes<br>%SKETCH_INFO%</progress></dd><br>
        <h3>WiFi</h3>
        <hr>
        <dt>Hostname</dt>
        <dd>%HOSTNAME%</dd>
        <dt>Station MAC</dt>
        <dd>%MY_MAC%</dd>
      </dl>
      <h3>About</h3>
      <hr>
      <dl>
        <dt>Firmware Version</dt>
        <dd>%FW_VER%</dd>
        <dt>IDF SDK</dt>
        <dd>%SDK_VER%</dd>
        <dt>Build Date</dt>
        <dd>%ABOUT_DATE%</dd>
      </dl>
      <form action="/update" method="get"><button>Update</button></form><br>
      <hr><br><br>
      <form action="/erase" method="get"><button class="D">Erase WiFi Config</button></form><br>
      <p>Github <a href="https://github.com/tzapu/WiFiManager">https://github.com/tzapu/WiFiManager</a>.</p>
    </div>
  </body>

</html>
)rawliteral";