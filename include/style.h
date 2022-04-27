#include "Arduino.h"

const char HTTP_STYLE[] PROGMEM = R"rawliteral(
.c,
body {
  text-align: center;
  font-family: verdana
}

div,
input,
select {
  padding: 5px;
  font-size: 1em;
  margin: 5px 0;
  box-sizing: border-box
}

input,
button,
select,
.msg {
  border-radius: .3rem;
  width: 100%%
}

input[type=radio],
input[type=checkbox] {
  width: auto
}

button,
input[type='button'],
input[type='submit'] {
  cursor: pointer;
  border: 0;
  background-color: #1fa3ec;
  color: #fff;
  line-height: 2.4rem;
  font-size: 1.2rem;
  width: 100%%
}

input[type='file'] {
  border: 1px solid #1fa3ec
}

.wrap {
  text-align: left;
  display: inline-block;
  min-width: 360px;
  max-width: 500px
}

a {
  color: #000;
  font-weight: 700;
  text-decoration: none
}

a:hover {
  color: #1fa3ec;
  text-decoration: underline
}

.q {
  height: 16px;
  margin: 0;
  padding: 0 5px;
  text-align: right;
  min-width: 38px;
  float: right
}

.q.q-0:after {
  background-position-x: 0
}

.q.q-1:after {
  background-position-x: -16px
}

.q.q-2:after {
  background-position-x: -32px
}

.q.q-3:after {
  background-position-x: -48px
}

.q.q-4:after {
  background-position-x: -64px
}

.q.l:before {
  background-position-x: -80px;
  padding-right: 5px
}

.ql .q {
  float: left
}

.q:after,
.q:before {
  content: '';
  width: 16px;
  height: 16px;
  display: inline-block;
  background-repeat: no-repeat;
  background-position: 16px 0;
  background-image: url('data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAGAAAAAQCAMAAADeZIrLAAAAJFBMVEX///8AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAADHJj5lAAAAC3RSTlMAIjN3iJmqu8zd7vF8pzcAAABsSURBVHja7Y1BCsAwCASNSVo3/v+/BUEiXnIoXkoX5jAQMxTHzK9cVSnvDxwD8bFx8PhZ9q8FmghXBhqA1faxk92PsxvRc2CCCFdhQCbRkLoAQ3q/wWUBqG35ZxtVzW4Ed6LngPyBU2CobdIDQ5oPWI5nCUwAAAAASUVORK5CYII=');
}

@media (-webkit-min-device-pixel-ratio: 2),
(min-resolution: 192dpi) {

  .q:before,
  .q:after {
    background-image: url('data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAALwAAAAgCAMAAACfM+KhAAAALVBMVEX///8AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAADAOrOgAAAADnRSTlMAESIzRGZ3iJmqu8zd7gKjCLQAAACmSURBVHgB7dDBCoMwEEXRmKlVY3L//3NLhyzqIqSUggy8uxnhCR5Mo8xLt+14aZ7wwgsvvPA/ofv9+44334UXXngvb6XsFhO/VoC2RsSv9J7x8BnYLW+AjT56ud/uePMdb7IP8Bsc/e7h8Cfk912ghsNXWPpDC4hvN+D1560A1QPORyh84VKLjjdvfPFm++i9EWq0348XXnjhhT+4dIbCW+WjZim9AKk4UZMnnCEuAAAAAElFTkSuQmCC');
    background-size: 95px 16px;
  }
}

.msg {
  padding: 20px;
  margin: 20px 0;
  border: 1px solid #eee;
  border-left-width: 5px;
  border-left-color: #777
}

.msg h4 {
  margin-top: 0;
  margin-bottom: 5px
}

.msg.P {
  border-left-color: #1fa3ec
}

.msg.P h4 {
  color: #1fa3ec
}

.msg.D {
  border-left-color: #dc3630
}

.msg.D h4 {
  color: #dc3630
}

.msg.S {
  border-left-color: #5cb85c
}

.msg.S h4 {
  color: #5cb85c
}

dt {
  font-weight: bold
}

dd {
  margin: 0;
  padding: 0 0 0.5em 0;
  min-height: 12px
}

td {
  vertical-align: top;
}

.h {
  display: none
}

button {
  transition: 0s opacity;
  transition-delay: 3s;
  transition-duration: 0s;
  cursor: pointer
}

button.D {
  background-color: #dc3630
}

button:active {
  opacity: 50%% !important;
  cursor: wait;
  transition-delay: 0s
}

body.invert,
body.invert a,
body.invert h1 {
  background-color: #060606;
  color: #fff;
}

body.invert .msg {
  color: #fff;
  background-color: #282828;
  border-top: 1px solid #555;
  border-right: 1px solid #555;
  border-bottom: 1px solid #555;
}

body.invert .q[role=img] {
  -webkit-filter: invert(1);
  filter: invert(1);
}

input:disabled {
  opacity: 0.5;
}

input,
button,
select,
.msg {
  border-radius: 0.3rem;
  width: 100%%;
}

.switch {
  position: relative;
  display: inline-block;
  width: 120px;
  height: 68px;
}

.switch input {
  display: none;
}

.slider {
  position: absolute;
  top: 0;
  left: 0;
  right: 0;
  bottom: 0;
  background-color: #ccc;
  border-radius: 6px;
}

.slider::before {
  position: absolute;
  content: "";
  height: 52px;
  width: 52px;
  left: 8px;
  bottom: 8px;
  background-color: #fff;
  -webkit-transition: 0.4s;
  transition: 0.4s;
  border-radius: 3px;
}

input:checked+.slider {
  background-color: #b30000;
}

input:checked+.slider::before {
  -webkit-transform: translateX(52px);
  -ms-transform: translateX(52px);
  transform: translateX(52px);
}
)rawliteral";