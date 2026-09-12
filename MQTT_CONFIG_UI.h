#pragma once
static const char MQTT_CONFIG_UI[] PROGMEM = R"mqtt(
<style>
#haFields,#legacyFields{border:0;padding:12px 0 0;margin:0;min-width:0}
input[role=switch]{appearance:none;width:42px;height:24px;border:1px solid var(--line);border-radius:20px;background:var(--panel2);vertical-align:middle;cursor:pointer;position:relative;margin-right:8px}
input[role=switch]::before{content:"";position:absolute;left:3px;top:3px;width:16px;height:16px;border-radius:50%;background:var(--text)}
input[role=switch]:checked{background:#249653}
input[role=switch]:checked::before{left:21px}
input[role=switch]:focus-visible{outline:2px solid var(--accent2);outline-offset:3px}
</style>
<form method="post" action="/mqtt/save" class="form-card"><h2>Shared broker</h2><p>Both modes use this connection. Either or both modes may be enabled.</p><div class="form-grid">
<div class="field"><label for="broker">Broker address</label><input id="broker" name="mqtAdres" maxlength="29" value="{broker}"></div>
<div class="field"><label for="port">Port</label><input id="port" name="mqtPort" maxlength="4" value="{port}"></div>
<div class="field"><label for="user">Username</label><input id="user" name="mqtUser" maxlength="25" value="{user}"></div>
<div class="field"><label for="password">Password</label><input id="password" name="mqtPas" maxlength="25" type="password" placeholder="Leave blank to keep current password"></div>
</div><div class="actions"><button type="button" class="secondary" id="testBroker">Test broker connection</button></div><p id="brokerResult" role="status" aria-live="polite">Tests the broker settings above without saving. A blank password uses the saved password. This checks connection and login, not topic permissions.</p><section id="home-assistant" class="section"><h2>Home Assistant</h2><label><input type="checkbox" role="switch" id="haEnabled" name="haEnabled" {haEnabled}> Enable/Disable Home Assistant</label><fieldset id="haFields"><div class="field"><label for="prefix">Discovery prefix</label><input id="prefix" name="prefix" maxlength="48" value="{prefix}" required><span class="help">Default: homeassistant. Enables discovery, solar sensors and output controls.</span></div></fieldset></section>
<section class="section"><h2>Domoticz</h2><label><input type="checkbox" role="switch" id="legacyEnabled" name="legacyEnabled" {legacyEnabled}> Enable/Disable Domoticz</label><fieldset id="legacyFields"><div class="form-grid"><div class="field"><label for="fm">Message format</label><select id="fm" name="fm">{formats}</select><span class="help">Keep the format expected by your existing subscriber. All five legacy formats are unchanged.</span></div><div class="field"><label for="idx">State device ID</label><input id="idx" name="mqidx" type="number" min="0" max="65535" value="{idx}" required></div><div class="field"><label for="out">Publish topic</label><input id="out" name="mqtoutTopic" maxlength="39" value="{out}"></div></div><a class="button secondary" href="/mqtt/test">Send test using saved settings</a></fieldset></section><p>Disabling a mode keeps its settings for later.</p><div class="actions"><button>Save and restart ECU</button><a class="button secondary" href="/menu">Cancel</a></div></form>
<script>
for (const [toggle,fields] of [['haEnabled','haFields'],['legacyEnabled','legacyFields']]) {
 const control=document.getElementById(toggle), group=document.getElementById(fields);
 const update=()=>{group.disabled=!control.checked;group.hidden=!control.checked;};
 control.addEventListener('change',update);update();
}
document.getElementById('testBroker').addEventListener('click',async()=>{
 const button=document.getElementById('testBroker'),result=document.getElementById('brokerResult');
 button.disabled=true;result.textContent='Testing broker connection…';
 try {
  const data=new URLSearchParams();
  for(const [id,name] of [['broker','mqtAdres'],['port','mqtPort'],['user','mqtUser'],['password','mqtPas']])data.set(name,document.getElementById(id).value);
  let response=await fetch('/mqtt/connection-test',{method:'POST',body:data});
  if(!response.ok)throw new Error(await response.text());
  const started=await response.json();
  for(let attempt=0;attempt<30;++attempt){
   await new Promise(resolve=>setTimeout(resolve,1000));
   response=await fetch('/mqtt/connection-test?id='+started.id,{cache:'no-store'});
   if(!response.ok)throw new Error(await response.text());
   const state=await response.json();
   if(!state.running){result.textContent=state.message;return;}
  }
  throw new Error('The connection test is taking too long. Try again later.');
 } catch(error){result.textContent='Connection test: '+error.message;}
 finally{button.disabled=false;}
});
</script>
)mqtt";
