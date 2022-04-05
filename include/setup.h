#include "Arduino.h"

const char HTTP_SETUP[] PROGMEM = R"rawliteral(
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
            <h3>Preheat</h3>
              <label for="s00">Setpoint</label>
              <input type="number" id ="s00" name="s00" min="1" max="250" value=100 required>°C<br>
              <label for="s01">Rate</label>
              <input type="number" id ="s01" name="s01" min="1" max="250" value=100 required>°C/hr<br>
              <label for="s02">Hold</label>
              <input type="number" id ="s02" name="s02" min="0" max="3600" value=15 required>min<br>
            <h3>Step1</h3>
              <label for="s10">Setpoint</label>
              <input type="number" id ="s10" name="s10" min="1" max="960" value=100 required>°C<br>
              <label for="s11">Rate</label>
              <input type="number" id ="s11" name="s11" min="1" max="9999" value=250 required>°C/hr<br>
              <label for="s12">Hold</label>
              <input type="number" id ="s12" name="s12" min="0" max="3600" value=0 required>min<br>
            <h3>Step2</h3>
              <label for="s20">Setpoint</label>
              <input type="number" id ="s20" name="s20" min="1" max="1240" value=1140 required>°C<br>
              <label for="s21">Rate</label>
              <input type="number" id ="s21" name="s21" min="1" max="9999" value=250 required>°C/hr<br>
              <label for="s22">Hold</label>
              <input type="number" id ="s22" name="s22" min="0" max="3600" value=0 required>min<br>
            <h3>Final</h3>
              <label for="s30">Setpoint</label>
              <input type="number" id ="s30" name="s30" min="900" max="1260" value=1240 required>°C<br>
              <label for="s31">Rate</label>
              <input type="number" id ="s31" name="s31" min="1" max="9999" value=60 required>°C/hr<br>
              <label for="s32">Hold</label>
              <input type="number" id ="s32" name="s32" min="0" max="3600" value=15 required>min<br>
              <input type ="submit" value ="FIRE">
            </p>
          </form>
        </div>
      </div>
    </div>
  </div>
</body>
</html>
)rawliteral";
