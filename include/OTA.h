#include "Arduino.h"

const char HTTP_UPDATE[] PROGMEM = R"rawliteral(
  <html lang="en">
  <head>
    <meta name="format-detection" content="telephone=no">
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width,initial-scale=1,user-scalable=no">
    <title>%HTML_HEAD_TITLE%</title>
    <style>
    %CSS_TEMPLATE%
    #myProgress {
      width: 100%%;
      border-radius: 25px;
      background-color: grey;
    }

    #myBar {
      width: 0;
      height: 20px;
      border-radius: 25px;
      background-color: #1fa3ec;
    }
    </style>
  </head>

  <body class="invert">
    <div class="wrap">
      <h1>%HTML_HEAD_TITLE%</h1>
      Upload New Firmware<br>
      <form method="POST" enctype="multipart/form-data" onchange="(function(el){document.getElementById('uploadbin').style.display = el.value=='' ? 'none' : 'initial';})(this)">
      <input type="file" id="update" name="update" accept=".bin,application/octet-stream"><button id="uploadbin" type="submit" class="h D">Update</button></form><br>
      <div id="myProgress"><div id="myBar"></div></div>
    </div>
  </body>

  <script>
    const fileInput = document.querySelector('input[type="file"]');
    const reader = new FileReader();
    var i = 0;

    function handleEvent(event) {
        console.log(`${event.type}: ${event.loaded} bytes transferred\n`);
    } 
    function addListeners(reader) {
        reader.addEventListener('load', handleEvent);
        reader.addEventListener('progress', handleEvent);
    }
    function handleSelected(e) {
      const selectedFile = fileInput.files[0];
      if (selectedFile) {
      var ajax = new XMLHttpRequest();
      ajax.upload.addEventListener("progress", progressHandler, false);
      }
    }

    function progressHandler(event) {
      _("loaded_n_total").innerHTML = "Uploaded " + event.loaded + " bytes of " + event.total;
      var percent = (event.loaded / event.total) * 100;
      _("progressBar").value = Math.round(percent);
      _("status").innerHTML = Math.round(percent) + "% uploaded... please wait";
    }

    fileInput.addEventListener('change', handleSelected); 
    fileInput.addEventListener('progress', console.log("hi")); 
  </script>

</html>
)rawliteral";
