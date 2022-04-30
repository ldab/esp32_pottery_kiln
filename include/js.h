#include "Arduino.h"

const char HTTP_JS[] PROGMEM = R"rawliteral(
function toggleCheckbox(element) {
  var xhr = new XMLHttpRequest();
  if(element.checked){ xhr.open("GET", "/gpio?output="+element.id+"&state=1", true); }
  else { xhr.open("GET", "/gpio?output="+element.id+"&state=0", true); }
  xhr.send();
}

Highcharts.setOptions({
    time: {
        timezone: 'Europe/Stockholm'
    }
});

const chart = Highcharts.chart('container', {
    chart: {
        type: 'spline',
        zoomType: 'x'
    },
    title: {
        text: ''
    },
    xAxis: {
      type: 'datetime',
      labels: {
        enabled: false
      }
    },
    yAxis: {
        title: {
            text: ''
        },
    },           
    plotOptions: {
        series: {
            marker: {
                enabled: false
            }
        }
    },
    series: [{
        name: 'T',
        data: (function() {        
          var _d = %GRAPH_DATA%;
          for (index = 0; index < _d.length; index++)
          {
            _d[index][0] = _d[index][0] * 1000;
          }
          return _d;
        }()),
        showInLegend: false
    }]
});

// window.addEventListener('load', getReadings);

function plotTemperature(t) {

  var x = (new Date()).getTime();
  // console.log(x);
  // console.log(t);

  chart.series[0].addPoint([x, t]);
  // chart.series[0].addPoint(t);
}

function getReadings(){
  var xhr = new XMLHttpRequest();
  xhr.onreadystatechange = function() {
    if (this.readyState == 4 && this.status == 200) {
      var myObj = this.responseText;
      console.log(myObj);
      var temp = myObj.temperature;
    }
  }; 
  xhr.open("GET", "/readings", true);
  xhr.send();
}

if (!!window.EventSource) {
  var source = new EventSource('/events');
  
  source.addEventListener('open', function(e) {
    console.log("Events Connected");
  }, false);

  source.addEventListener('error', function(e) {
    if (e.target.readyState != EventSource.OPEN) {
      console.log("Events Disconnected");
    }
  }, false);
  
  source.addEventListener('message', function(e) {
    console.log("message", e.data);
  }, false);
  
  source.addEventListener('temperature', function(e) {
    // console.log("temperature", e.data);
    document.getElementById("temperature").innerHTML = e.data;
    plotTemperature(parseFloat(e.data));
  }, false);

  source.addEventListener('KW', function(e) {
    // console.log("KW", e.data);
    document.getElementById("KW").innerHTML = e.data;
  }, false);

  source.addEventListener('display', function(e) {
    document.getElementById("display").innerHTML = e.data;
  }, false);
}

)rawliteral";