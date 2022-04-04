#include "Arduino.h"

const char HTTP_JS[] PROGMEM = R"rawliteral(
function toggleCheckbox(element) {
  var xhr = new XMLHttpRequest();
  if(element.checked){ xhr.open("GET", "/gpio?output="+element.id+"&state=1", true); }
  else { xhr.open("GET", "/gpio?output="+element.id+"&state=0", true); }
  xhr.send();
}

window.addEventListener('load', getReadings);

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
    console.log("temperature", e.data);
    document.getElementById("temperature").innerHTML = e.data;
  }, false);
}

)rawliteral";