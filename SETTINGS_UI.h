#pragma once
static const char SETTINGS_UI[] PROGMEM=R"SETTINGS(
<section class="card section"><h2>Download settings</h2><p>Includes ECU and radio identity, inverter configuration and pairing records, polling and power limits, time/location, MQTT, network, antenna and login settings.</p><p><strong>This file contains passwords in plain text.</strong> Keep it private. It does not contain production history, live readings, diagnostic logs, crash dumps or inverter grid-protection settings.</p><a class="button" href="/settings/backup">Download settings backup (.json)</a><a class="button secondary" href="/energy">Production history backup</a></section>
<section class="form-card section"><h2>Restore settings</h2><p>Restore replaces the installation configuration and restarts the ECU. The administrator and read-only passwords from the backup become active. Production history is not modified.</p><p>For a replacement board, the original ECU_ID and radio address are preserved. <strong>Keep the old ECU powered off when transferring its identity.</strong> Pairing continuity on replacement hardware still requires a field check.</p>
<div class="field"><label for="settingsFile">Settings backup (.json, up to 32 KB)</label><input id="settingsFile" type="file" accept=".json,application/json"></div>
<div class="field"><label><input id="restoreNetwork" type="checkbox"> Restore Wi-Fi credentials, hostname and IP settings</label><span class="help">Unchecked: keep this board's network settings. Restoring a static address may change how you reconnect.</span></div>
<div class="field"><label><input id="restoreAntenna" type="checkbox"> Restore antenna board and GPIO settings</label><span class="help">Unchecked: keep this board's antenna settings. Only select this when the saved board wiring matches the destination.</span></div>
<div class="actions"><button id="validateSettings" type="button">Validate and preview</button><button id="applySettings" type="button" disabled>Restore settings and restart</button></div>
<pre id="settingsPreview" style="white-space:pre-wrap;overflow-wrap:anywhere" aria-live="polite"></pre><p id="settingsStatus" role="status"></p></section>
<script>
(()=>{
const file=document.getElementById('settingsFile'),validate=document.getElementById('validateSettings'),apply=document.getElementById('applySettings'),preview=document.getElementById('settingsPreview'),status=document.getElementById('settingsStatus');
let backup=null,revision=0;
function invalidate(){++revision;backup=null;apply.disabled=true;preview.textContent='';status.textContent=''}
file.addEventListener('change',invalidate);
for(const id of ['restoreNetwork','restoreAntenna'])document.getElementById(id).addEventListener('change',invalidate);
validate.onclick=async()=>{
 invalidate();const selected=file.files[0];
 if(!selected||!selected.size||selected.size>32768){status.textContent='Select a settings JSON backup of up to 32 KB.';return}
 validate.disabled=true;const currentRevision=revision;
 try{
  const text=await selected.text();const response=await fetch('/settings/validate',{method:'POST',headers:{'Content-Type':'application/json','X-ECU-Settings':'1'},body:text});
  if(!response.ok)throw new Error(await response.text());
  const result=await response.json();if(currentRevision!==revision)return;backup=text;
  preview.textContent=`Fleet: ${result.fleet}\nECU_ID: ${result.ecuId}\nInverters: ${result.inverters}\nRadio address: ${result.radioIEEE}\nDestination: ${result.replacement?'replacement board':'original board'}\nNetwork: ${document.getElementById('restoreNetwork').checked?'restore from backup':'keep current'}\nAntenna: ${document.getElementById('restoreAntenna').checked?'restore from backup':'keep current'}\nLogin passwords: restore from backup\nProduction history: unchanged`;
  apply.disabled=false;status.textContent='Backup validated. Review the settings above before restoring.';
 }catch(error){status.textContent=error.message}finally{validate.disabled=false}
};
apply.onclick=async()=>{
 if(!backup||!confirm('Replace this ECU installation with the reviewed settings and restart? Login passwords will come from the backup. Keep the old ECU off when moving its identity.'))return;
 apply.disabled=true;validate.disabled=true;
 const headers={'Content-Type':'application/json','X-ECU-Settings':'1','X-ECU-Restore':'confirmed'};
 if(document.getElementById('restoreNetwork').checked)headers['X-ECU-Network']='restore';
 if(document.getElementById('restoreAntenna').checked)headers['X-ECU-Antenna']='restore';
 try{const response=await fetch('/settings/restore',{method:'POST',headers,body:backup});const message=await response.text();if(!response.ok)throw new Error(message);status.textContent=message;file.disabled=true}
 catch(error){status.textContent=error.message;apply.disabled=false;validate.disabled=false}
};
})();
</script>
)SETTINGS";
