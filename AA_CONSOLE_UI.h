//<link rel="icon" type="image/x-icon" href="/favicon.ico" />

const char CONSOLE_HTML_LEGACY[] PROGMEM = R"=====(
<!DOCTYPE html><html><head>
<meta http-equiv="Content-Type" content="text/html; charset=utf-8"/>
<title>ESP-ECU</title>
<meta name="viewport" content="width=device-width, initial-scale=1">

<link rel="stylesheet" type="text/css" href="/STYLESHEET">
<script>
function helpfunctie() {
document.getElementById("hulp").style.display = "block";
}
function sl() {
document.getElementById("hulp").style.display = "none";
}

</script>

<style>
 tr {height:16px !important;
 font-size:15px !important;
 }
 li a:hover {
   background-color: #333 !important;
}
#hulp {
  background-color: #ffffff;
  border: solid 2px;
  display:none;
  padding:4px;
  width:94vw;
}
.divstl { width: 60vw; height:84vh; background: #dbd89c; border:1px solid; padding-left:10px;
}
</style>
</head>
<body>
  <div id='hulp'>
  <span class='close' onclick='sl();'>&times;</span><h3>CONSOLE COMMANDS</h3>
  <b>10;ZBT=message: </b> send a zigbee message (e.g. 2710).<br><br>
  <b>10;SENDRAW=message: </b> send a raw zigbee message (no checksum etc).<br><br>
  <b>10;QUERY=x: </b> query inverter data.<br><br>
  <b>10;DELETE=filename: </b> delete a file.<br><br>
  <b>10;INV_REBOOT=x: </b> reboot an unresponsive inverter<br><br>
  <b>10;HEALTH: </b> healthcheck zigbee hw/system<br><br>
  <b>10;POLL=x: </b> poll inverter #x<br><br>
  <b>10;INIT_N: </b> start the zigbee coordinator<br><br>
  <b>10;DIAG: </b> change debug, 0=disable, 1=console, 2=serial<br><br>
  <b>10;EDIT=0-AABB: </b> mark an inverter as paired<br><br>
  <b>10;ERASE: </b> delete all inverter files<br><br>
  <b>10;FILES: </b> show filesystem<br><br>
  <b>10;TESTMQTT: </b>sends a mqtt testmessage<br><br>
  <b>10;CLEAR: </b> clear console window<br><br>
  <b>10;THROTTLE=x-500; </b> throttle inverter x 500
  </div>

<div id='msect'>
<div id='menu'>
<a href='/MENU' onclick='confirmExit()' class='close'>&times;</span></a>
<a href='#' onclick='helpfunctie()'>help</a>
<a><input type="text" placeholder="type here" id="tiep"></a>
</div>
<br>
  <div class='divstl'>
  <table id='tekstveld'></table>
  </div>
 </div>

<script>
  var field = document.getElementById('tekstveld');
  var gateway = `${(window.location.protocol == "https:"?"wss":"ws")}://${window.location.hostname}/ws`;
  var websocket;
  var inputField = document.getElementById('tiep');

  window.onbeforeunload = confirmExit;
  function confirmExit()
  {
      alert("close the console?");
      ws.close();
  }

  window.addEventListener('load', onLoad);
  function initWebSocket() {
    console.log('Trying to open a WebSocket connection...');
    websocket = new WebSocket(gateway);
    websocket.onopen    = onOpen;
    websocket.onclose   = onClose;
    websocket.onmessage = onMessage; // <-- add this line
  }
  function onOpen(event) {
    console.log('Connection opened');
    field.insertAdjacentHTML('beforeend', "<tr><td>* * connection opened * *");
    inputField.focus();
    }
  function onClose(event) {
    console.log('Connection closed');
    field.insertAdjacentHTML('beforeend', "<tr><td>* * connection closed * *");
    //setTimeout(initWebSocket, 2000);
  }
  function onMessage(event) {
    //var message = event.data;
    field.insertAdjacentHTML('beforeend', "<tr><td>" + event.data );
    if (field.rows.length > 20) {
    var rtm = field.rows.length - 20;
    for (let x=0; x<rtm; x++) { field.deleteRow(0); }
  }
    if (event.data == "clearWindow") {
    for (let i = 0; i < 22; i++) {
        field.deleteRow(0); }
    }
   }

  function onLoad(event) {
    initWebSocket();
    sendEvent();
  }

  function sendEvent() {
    inputField.addEventListener('keyup', function(happen) {
    if (happen.keyCode === 13) {
       happen.preventDefault();
       sendData();
       }
    });
  }
  function sendData(){
  var data = inputField.value;
  websocket.send(data, 1);
  inputField.value = "";
  }

function disConnect() {
  alert("close the console");
  ws.close();
}
</script>
</body>
</html>
)=====";

const char CONSOLE_HTML[] PROGMEM = R"=====(
<!doctype html><html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Diagnostics console · APsystems ECU</title><link rel="icon" href="/favicon.ico"><link rel="stylesheet" href="/stylesheet"></head><body><header class="topbar"><a class="brand" href="/">ESP32-C6 ECU</a><nav class="nav"><a class="button secondary" href="/diagnostics">Snapshot</a><a class="button secondary" href="/menu">Menu</a></nav></header><main class="page"><div class="eyebrow">Live support tools</div><h1>Diagnostics console</h1><p>Live firmware messages appear below. Commands are intended for troubleshooting and can change device state.</p><div class="alert">Common commands: <code>10;POLL=0</code> polls inverter 1, <code>10;QUERY=0</code> requests firmware information, <code>10;HEALTH</code> checks the radio, and <code>10;DIAG</code> changes tracing level.</div><section class="card section"><pre id="output" style="min-height:320px;max-height:55vh;overflow:auto;white-space:pre-wrap"></pre><div class="field"><label for="command">Console command</label><input id="command" autocomplete="off" placeholder="10;POLL=0"></div><div class="actions"><button id="send">Send command</button><button id="clear" class="secondary">Clear display</button></div></section></main><script>
const output=document.getElementById('output'),input=document.getElementById('command'),gateway=`${location.protocol==='https:'?'wss':'ws'}://${location.host}/ws`;let socket;
function line(text){output.textContent+=text+'\n';output.scrollTop=output.scrollHeight}
function connect(){socket=new WebSocket(gateway);socket.onopen=()=>{line('Connected to ECU console.');input.focus()};socket.onclose=()=>line('Console connection closed. Reload to reconnect.');socket.onerror=()=>line('Console connection error.');socket.onmessage=e=>{if(e.data==='clearWindow')output.textContent='';else line(e.data)}}
function send(){let value=input.value.trim();if(value&&socket&&socket.readyState===1){socket.send(value);input.value=''}}
document.getElementById('send').onclick=send;document.getElementById('clear').onclick=()=>output.textContent='';input.addEventListener('keydown',e=>{if(e.key==='Enter'){e.preventDefault();send()}});connect();
</script></body></html>
)=====";
