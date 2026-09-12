#pragma once
const char ANTENNA_FORM[] PROGMEM = R"ant(
<style>#advanced{min-width:0;margin:0;border:1px solid var(--line);border-radius:10px;padding:14px}#advanced .field,#advanced .checkline{margin-bottom:12px}#advanced legend{font-weight:650;padding:0 6px}</style>
<form class="form-card" method="post" action="/antenna/save">
<div class="form-grid">
<div class="field full"><label for="mode">Antenna selection</label>
<select id="mode" name="mode" onchange="antennaFields()">
<option value="0" {mode0}>Unmanaged (default)</option>
<option value="1" {mode1}>Internal</option><option value="2" {mode2}>External</option>
</select><span class="help">Unmanaged leaves antenna control to the board. Internal or External requires a board with an RF switch. Changes take effect after restart.</span></div>
<div id="boardFields" class="field full"><label for="board">Board</label>
<select id="board" name="board" onchange="antennaFields()">
<option value="0" {board0}>Seeed Studio XIAO ESP32-C6</option>
<option value="1" {board1}>Advanced</option></select>
<p id="boardPhoto"><a href="https://files.seeedstudio.com/wiki/SeeedStudio-XIAO-ESP32C6/img/XIAO_ESP32-C6_front_pinout.png" target="_blank" rel="noopener noreferrer">View XIAO ESP32-C6 board photo</a></p>
<span class="help" id="presetHelp">Uses the manufacturer's antenna switch settings. Select Advanced only for a different switch wiring.</span></div>
<fieldset id="advanced" class="field full"><legend>Advanced antenna wiring</legend>
<p>Use ESP32-C6 GPIO numbers, not board labels such as D3. Check your board schematic. Reserved flash, USB, serial, boot, LED and button pins cannot be selected.</p>
<div class="field"><label for="selectPin">Antenna select GPIO</label><input id="selectPin" name="selectPin" type="number" min="0" max="23" step="1" required value="{selectPin}"></div>
<div class="field"><label for="externalHigh">Select level for external antenna</label><select id="externalHigh" name="externalHigh"><option value="1" {external1}>HIGH (internal is LOW)</option><option value="0" {external0}>LOW (internal is HIGH)</option></select></div>
<div class="checkline"><input id="useEnable" name="useEnable" type="checkbox" {useEnable} onchange="antennaFields()"><label for="useEnable">RF switch has an enable pin</label></div>
<div id="enableFields"><div class="field"><label for="enablePin">Enable GPIO</label><input id="enablePin" name="enablePin" type="number" min="0" max="23" step="1" required value="{enablePin}"></div>
<div class="field"><label for="enableHigh">Enable active level</label><select id="enableHigh" name="enableHigh"><option value="0" {enable0}>LOW</option><option value="1" {enable1}>HIGH</option></select></div></div>
</fieldset></div>
<div class="actions"><button type="submit">Save antenna settings</button><a class="button secondary" href="/menu">Cancel</a></div></form>
<script>
function antennaFields(){
  const managed=document.getElementById('mode').value!=='0';
  const advanced=managed&&document.getElementById('board').value==='1';
  const enable=advanced&&document.getElementById('useEnable').checked;
  document.getElementById('boardFields').style.display=managed?'flex':'none';
  document.getElementById('board').disabled=!managed;
  document.getElementById('advanced').style.display=advanced?'block':'none';
  document.getElementById('advanced').disabled=!advanced;
  document.getElementById('boardPhoto').style.display=advanced?'none':'block';
  document.getElementById('presetHelp').style.display=advanced?'none':'block';
  document.getElementById('enableFields').style.display=enable?'block':'none';
  document.getElementById('enablePin').disabled=!enable;
  document.getElementById('enableHigh').disabled=!enable;
}
antennaFields();
</script>
)ant";
