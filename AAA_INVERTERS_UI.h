const char INVCONFIG_START[] PROGMEM = R"=====(
<!doctype html>
<html>
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Inverters · APsystems ECU</title>
<link rel="icon" href="/favicon.ico">
<link rel="stylesheet" href="/stylesheet">
</head>
<body onload="%LOADBAG%">
<header class="topbar">
<a class="brand" href="/">APsystems ECU</a>
<nav class="nav">
<a class="button secondary" href="/menu">Menu</a>
</nav>
</header>
<main class="page">
<div class="eyebrow">Radio devices</div>
<h1>Inverters</h1>
<p>Select an inverter to edit, or add another unit. Save its identity before pairing so the ECU knows which serial number to request.</p>
<div class="actions">
%INVERTER_NAV%
</div>
<form id="formulier" class="form-card section" method="get" action="/inverter/save" onsubmit="return confirm('Save these inverter settings?')">%<FORMPAGE>%<div class="actions">
<button type="submit">Save inverter</button>
<a class="button secondary" href="/menu">Cancel</a>
</div>
</form>
<div class="actions" style="display:%PAIR_ACTION_STYLE%">
<a class="button" href="/inverter/pair" onclick="return confirm('Pair this inverter now?')">Pair inverter</a>
<a class="button danger" href="/inverter/delete" onclick="return confirm('Delete this inverter?')">Delete inverter</a>
</div>
</main>
<script src="/inverter/script">
</script>
</body>
</html>
)=====";

 const char INVERTER_GENERAL[] PROGMEM = R"=====(
<div class="card-head">
<div>
<div class="eyebrow">Configuration</div>
<h2>{heading}</h2>
</div>
<span class="badge">Network ID: unpaired</span>
</div>
<div class="form-grid">
<div class="field">
<label for="iv">Serial number</label>
<input id="iv" name="iv" inputmode="numeric" pattern="[0-9]{12}" minlength="12" maxlength="12" required value="000000">
<span class="help">The 12-digit number printed on the inverter label.</span>
</div>
<div class="field">
<label for="sel">Inverter model</label>
<select name="invt" id="sel" onchange="myFunction()">
<option value="0" invtype_0>YC600</option>
<option value="2" invtype_2>DS3</option>
<option value="1" invtype_1>QS1</option>
</select>
<span class="help">The model controls decoding and how many PV inputs are displayed.</span>
</div>
<div class="field">
<label for="il">Display name</label>
<input id="il" name="il" maxlength="12" value="{location}">
<span class="help">A short, recognizable name such as Shed East.</span>
</div>
<div class="field">
<label for="cv">Power-limit correction</label>
<input id="cv" name="cal" type="number" min="-15" max="15" step="1" value="{cal}">
<span class="help">Correction in percentage points for throttle commands. Leave at 0 unless measured limiting is consistently inaccurate.</span>
</div>
<div class="field">
<label for="mqidx">Domoticz device ID</label>
<input id="mqidx" name="mqidx" type="number" min="0" max="65535" value="{idx}">
<span class="help">Only used by the legacy Domoticz MQTT format. Leave at 0 otherwise.</span>
</div>
<div class="field">
<label>Connected PV inputs</label>
<div class="actions">
<label>
<input type="checkbox" name="pan1" #1check> Input 1</label>
<label>
<input type="checkbox" name="pan2" #2check> Input 2</label>
<span id="invspan">
<label>
<input type="checkbox" name="pan3" #3check> Input 3</label> <label>
<input type="checkbox" name="pan4" #4check> Input 4</label>
</span>
</div>
<span class="help">Disable an input only when no panel is connected. DS3 and YC600 expose two inputs; QS1 exposes four.</span>
</div>
</div>
)=====";

// **********************************************************************************
//                         script
// **********************************************************************************

const char INV_SCRIPT[] PROGMEM = R"=====(
function showFunction() {
  //alert("showFunction");
  document.getElementById("invspan").style.display = "inline";
}

function hideFunction() {
  //alert("showFunction");
  document.getElementById("invspan").style.display = "none";
}

function myFunction(){
 if(document.getElementById("sel").value == 1 ) {
    showFunction();
 } else {
   hideFunction();
 }
}

)=====";




//*******************************************************************************************
//             prepare for saving the data
// *****************************************************************************************
