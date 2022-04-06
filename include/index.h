#include "Arduino.h"

const char HTTP_INDEX[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML>
<html>
  <head>
    <title>%HTML_HEAD_TITLE%</title>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <link rel="icon" href="data:,">
    <style>
      %CSS_TEMPLATE%
    </style>
  </head>
  <body class="invert">
    <div class="wrap">
      <h2>%HTML_HEAD_TITLE%</h2>
      <h3>Temp: <span id="temperature"></span> &degC</h3>
      <div id="container" style="width:100%%; height:200px;"></div>
      <form action='/setup' method='get'><button>Setup</button></form><br />
      <form action='/config' method='get'><button>Config</button></form><br />
      <form action='/update' method='get'><button>Update</button></form><br />
      <script src="https://code.highcharts.com/highcharts.js"></script>
      <script>
        %INDEX_JS%
      </script>
    </div>
  </body>
</html>
)rawliteral";