String webEscape(const String &input) {
  String escaped;
  escaped.reserve(input.length() + 8);
  for (size_t i = 0; i < input.length(); ++i) {
    switch (input[i]) {
      case '&': escaped += F("&amp;"); break;
      case '<': escaped += F("&lt;"); break;
      case '>': escaped += F("&gt;"); break;
      case '"': escaped += F("&quot;"); break;
      case '\'': escaped += F("&#39;"); break;
      default: escaped += input[i]; break;
    }
  }
  return escaped;
}

String ecuPageStart(const String &title, const String &description) {
  String page;
  page.reserve(5000);
  page += F("<!doctype html><html><head><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\"><title>");
  page += webEscape(title);
  page += F(" · APsystems ECU</title><link rel=\"icon\" href=\"/favicon.ico\"><link rel=\"stylesheet\" href=\"/stylesheet\"></head><body><header class=\"topbar\"><a class=\"brand\" href=\"/\">ESP32-C6 ECU</a><nav class=\"nav\"><a class=\"button secondary\" href=\"/menu\">Menu</a></nav></header><main class=\"page\"><div class=\"eyebrow\">ESP32-C6 controller</div><h1>");
  page += webEscape(title);
  page += F("</h1><p>");
  page += webEscape(description);
  page += F("</p>");
  return page;
}

String ecuPageEnd() { return F("</main></body></html>"); }

String ecuClockText() {
  if (!timeRetrieved) return F("Not synchronized");
  const time_t current = ecuNow();
  char value[32];
  snprintf(value, sizeof(value), "%04d-%02d-%02d %02d:%02d:%02d",
           ecuYear(current), ecuMonth(current), ecuDay(current),
           ecuHour(current), ecuMinute(current), ecuSecond(current));
  return String(value);
}

String ecuSolarWindowText() {
  if (!daylightPolling) return F("24-hour polling selected");
  if (!timeRetrieved) return F("24-hour fallback — local time unavailable");
  if (!locationConfigured) return F("24-hour fallback — location not configured");
  char value[48];
  snprintf(value, sizeof(value), "%02d:%02d to %02d:%02d",
           ecuHour(switchonTime), ecuMinute(switchonTime),
           ecuHour(switchoffTime), ecuMinute(switchoffTime));
  return String(value);
}

void handleFleetNamePage(AsyncWebServerRequest *request) {
  String page = ecuPageStart(F("Fleet name"),
      F("Choose the name shown at the top of the production dashboard."));
  page += F("<form class=\"form-card\" method=\"post\" action=\"/fleet-name/save\"><div class=\"field\"><label for=\"fleetName\">Dashboard fleet name</label><input id=\"fleetName\" name=\"fleetName\" maxlength=\"48\" value=\"");
  page += webEscape(fleetName);
  page += F("\" required><span class=\"help\">1 to 48 printable characters. Default: APsystems Fleet.</span></div><div class=\"actions\"><button type=\"submit\">Save name</button><a class=\"button secondary\" href=\"/\">Cancel</a></div></form>");
  page += ecuPageEnd();
  request->send(200, "text/html; charset=utf-8", page);
}

void handleFleetNameSave(AsyncWebServerRequest *request) {
  if (!request->hasParam("fleetName", true)) {
    request->send(400, "text/plain", "Missing fleet name");
    return;
  }
  String value = request->getParam("fleetName", true)->value();
  value.trim();
  if (value.isEmpty() || value.length() > 48) {
    request->send(400, "text/plain", "Fleet name must contain 1 to 48 characters");
    return;
  }
  for (size_t i = 0; i < value.length(); ++i) {
    uint8_t c = (uint8_t)value[i];
    if (c < 32 || c == 127) {
      request->send(400, "text/plain", "Fleet name contains an unsupported control character");
      return;
    }
  }
  strlcpy(fleetName, value.c_str(), sizeof(fleetName));
  basisConfigsave();
  request->redirect("/");
}

void handleNetworkPage(AsyncWebServerRequest *request) {
  String storedSsid, storedPassword, hostname;
  String staticIp, netmask, gateway;
  bool useDhcp = true;
  loadStoredWifiCredentials(storedSsid, storedPassword, hostname);
  loadStoredWifiAddressing(useDhcp, staticIp, netmask, gateway);

  String page = ecuPageStart(F("Network"),
      F("Review the active Wi-Fi connection or change hostname and IP addressing. Saving restarts the ECU."));
  page += F("<div class=\"metrics\"><div class=\"metric\"><strong>");
  page += WiFi.localIP().toString();
  page += F("</strong><span>Current IP address</span></div><div class=\"metric\"><strong>");
  page += String(WiFi.RSSI());
  page += F(" dBm</strong><span>Wi-Fi signal</span></div><div class=\"metric\"><strong>");
  page += webEscape(WiFi.SSID());
  page += F("</strong><span>Connected network</span></div></div><form class=\"form-card section\" method=\"post\" action=\"/network/save\"><div class=\"form-grid\"><div class=\"field full\"><label for=\"ssid\">Wi-Fi network name</label><input id=\"ssid\" name=\"ssid\" maxlength=\"32\" value=\"");
  page += webEscape(storedSsid);
  page += F("\" required></div><div class=\"field full\"><label for=\"wifiPassword\">Wi-Fi password</label><input id=\"wifiPassword\" name=\"wifiPassword\" type=\"password\" maxlength=\"63\" placeholder=\"Leave blank to keep the current password\"></div><div class=\"field full\"><label for=\"hostname\">Device hostname</label><input id=\"hostname\" name=\"hostname\" maxlength=\"32\" value=\"");
  page += webEscape(hostname);
  page += F("\" required><span class=\"help\">Letters, numbers and hyphens. Sent in DHCP requests after restart.</span></div><div class=\"field full\"><label for=\"addressing\">IP addressing</label><select id=\"addressing\" name=\"addressing\" onchange=\"addressFields()\"><option value=\"dhcp\"");
  if (useDhcp) page += F(" selected");
  page += F(">DHCP (recommended)</option><option value=\"static\"");
  if (!useDhcp) page += F(" selected");
  page += F(">Static IPv4</option></select></div><div id=\"staticFields\" class=\"field full\"><div class=\"form-grid\"><div class=\"field\"><label for=\"ip\">Static IP address</label><input id=\"ip\" name=\"ip\" value=\"");
  page += webEscape(staticIp);
  page += F("\"></div><div class=\"field\"><label for=\"netmask\">Netmask</label><input id=\"netmask\" name=\"netmask\" value=\"");
  page += webEscape(netmask);
  page += F("\"></div><div class=\"field\"><label for=\"gateway\">Gateway and DNS</label><input id=\"gateway\" name=\"gateway\" value=\"");
  page += webEscape(gateway);
  page += F("\"></div></div></div></div><div class=\"alert\">Changing the SSID, password or static address can make this page unreachable. The setup access point opens automatically if the ECU cannot reconnect.</div><div class=\"actions\"><button type=\"submit\">Save and restart</button><a class=\"button secondary\" href=\"/menu\">Cancel</a></div></form><script>function addressFields(){document.getElementById('staticFields').style.display=document.getElementById('addressing').value==='static'?'flex':'none'}addressFields()</script>");
  page += ecuPageEnd();
  request->send(200, "text/html", page);
}

void handleNetworkSave(AsyncWebServerRequest *request) {
  const char *required[] = {"ssid", "hostname", "addressing"};
  for (const char *name : required) if (!request->hasParam(name, true)) {
    request->send(400, "text/plain", "Missing required network setting");
    return;
  }
  String oldSsid, oldPassword, oldHostname;
  loadStoredWifiCredentials(oldSsid, oldPassword, oldHostname);
  String submittedSsid = request->getParam("ssid", true)->value();
  String submittedHostname = normalizedWifiHostname(
      request->getParam("hostname", true)->value());
  String submittedPassword = request->hasParam("wifiPassword", true)
      ? request->getParam("wifiPassword", true)->value() : String();
  if (submittedPassword.isEmpty()) submittedPassword = oldPassword;
  bool useDhcp = request->getParam("addressing", true)->value() != "static";
  String ip = request->hasParam("ip", true) ? request->getParam("ip", true)->value() : String();
  String netmask = request->hasParam("netmask", true) ? request->getParam("netmask", true)->value() : String();
  String gateway = request->hasParam("gateway", true) ? request->getParam("gateway", true)->value() : String();
  submittedSsid.trim(); ip.trim(); netmask.trim(); gateway.trim();
  IPAddress parsedIp, parsedNetmask, parsedGateway;
  bool validStatic = useDhcp || (parsedIp.fromString(ip) &&
      parsedNetmask.fromString(netmask) && parsedGateway.fromString(gateway));
  if (submittedSsid.isEmpty() || submittedSsid.length() > 32 ||
      submittedPassword.length() > 63 || !validStatic) {
    request->send(400, "text/plain", "Invalid Wi-Fi or static IP setting");
    return;
  }
  saveStoredWifiConfiguration(submittedSsid, submittedPassword,
      submittedHostname, useDhcp, ip, netmask, gateway);
  String response = ecuPageStart(F("Network settings saved"),
      F("The ECU is restarting. Reconnect using its reserved address or new static IP."));
  response += F("<div class=\"alert info\">New hostname: <strong>");
  response += webEscape(submittedHostname);
  response += F("</strong></div>");
  response += ecuPageEnd();
  request->send(200, "text/html", response);
  actionFlag = 10;
}

void handleAbout(AsyncWebServerRequest *request) {
  String storedSsid, storedPassword, hostname, staticIp, netmask, gateway;
  bool useDhcp;
  loadStoredWifiCredentials(storedSsid, storedPassword, hostname);
  loadStoredWifiAddressing(useDhcp, staticIp, netmask, gateway);
  uint32_t uptimeMinutes = millis() / 60000UL;
  String page = ecuPageStart(F("System information"),
      F("Live firmware, network, storage, polling and radio status."));
  page += F("<div class=\"card-grid\"><section class=\"card\"><h2>Firmware</h2><dl class=\"kv\"><dt>Version</dt><dd>");
  page += VERSION;
  page += F("</dd><dt>Built</dt><dd>"); page += __DATE__; page += ' '; page += __TIME__;
  page += F("</dd><dt>Flash size</dt><dd>"); page += String(ESP.getFlashChipSize() / 1048576UL); page += F(" MB</dd><dt>OTA available</dt><dd>"); page += esp_ota_get_next_update_partition(nullptr) ? F("Yes") : F("No — USB only");
  page += F("</dd><dt>Free heap</dt><dd>"); page += String(ESP.getFreeHeap()); page += F(" bytes</dd><dt>SPIFFS used</dt><dd>"); page += String(SPIFFS.usedBytes()); page += F(" / "); page += String(SPIFFS.totalBytes()); page += F(" bytes</dd><dt>Uptime</dt><dd>"); page += String(uptimeMinutes / 1440); page += F("d "); page += String((uptimeMinutes / 60) % 24); page += F("h "); page += String(uptimeMinutes % 60); page += F("m</dd></dl></section>");
  page += F("<section class=\"card\"><h2>ESP32-C6 temperature</h2>");
  if (systemTemperatureAvailable()) {
    page += F("<dl class=\"kv\"><dt>Current die temperature</dt><dd>"); page += String(systemTemperatureCurrentC(), 1); page += F(" &deg;C</dd><dt>Lowest since boot</dt><dd>"); page += String(systemTemperatureLowC(), 1); page += F(" &deg;C<br><span class=\"help\">"); page += systemTemperatureLowTimestamp(); page += F("</span></dd><dt>Highest since boot</dt><dd>"); page += String(systemTemperatureHighC(), 1); page += F(" &deg;C<br><span class=\"help\">"); page += systemTemperatureHighTimestamp(); page += F("</span></dd></dl>");
  } else {
    page += F("<div class=\"alert\">The internal temperature sensor is unavailable.</div>");
  }
  page += F("</section>");
  page += F("<section class=\"card\"><h2>Network</h2><dl class=\"kv\"><dt>Hostname</dt><dd>"); page += webEscape(hostname); page += F("</dd><dt>SSID</dt><dd>"); page += webEscape(WiFi.SSID()); page += F("</dd><dt>Addressing</dt><dd>"); page += useDhcp ? F("DHCP") : F("Static"); page += F("</dd><dt>IP address</dt><dd>"); page += WiFi.localIP().toString(); page += F("</dd><dt>Netmask</dt><dd>"); page += WiFi.subnetMask().toString(); page += F("</dd><dt>Gateway</dt><dd>"); page += WiFi.gatewayIP().toString(); page += F("</dd><dt>DNS</dt><dd>"); page += WiFi.dnsIP().toString(); page += F("</dd><dt>MAC address</dt><dd>"); page += WiFi.macAddress(); page += F("</dd><dt>Signal</dt><dd>"); page += String(WiFi.RSSI()); page += F(" dBm</dd></dl><a class=\"button secondary\" href=\"/network\">Change network settings</a></section>");
  page += F("<section class=\"card\"><h2>Polling and time</h2><dl class=\"kv\"><dt>Automatic polling</dt><dd>"); page += Polling ? F("Enabled") : F("Disabled"); page += F("</dd><dt>Fleet interval</dt><dd>"); page += String(pollIntervalSeconds); page += F(" seconds</dd><dt>Poll round</dt><dd>"); page += pollingRoundInProgress() ? F("In progress") : F("Idle"); page += F("</dd><dt>Next round</dt><dd>"); if (pollingNightModeActive()) { page += F("Night Mode — resumes "); time_t resume = pollingNextResumeEpoch(); if (resume) { char value[8]; snprintf(value, sizeof(value), "%02d:%02d", ecuHour(resume), ecuMinute(resume)); page += value; } else page += F("at sunrise"); } else if (Polling && pollingAllowedNow()) { page += String(pollingSecondsUntilNextRound()); page += F(" seconds"); } else page += F("Paused"); page += F("</dd><dt>Local time</dt><dd>"); page += ecuClockText(); page += F("</dd><dt>Time zone</dt><dd>"); page += webEscape(ecuTimeZoneLabel()); page += F("</dd><dt>Solar window</dt><dd>"); page += ecuSolarWindowText(); page += F("</dd></dl></section>");
  page += F("<section class=\"card\"><h2>Radio and services</h2><dl class=\"kv\"><dt>802.15.4 radio</dt><dd>"); page += zigbeeUp == 1 ? F("Ready") : (zigbeeUp == 11 ? F("Starting") : F("Fault")); page += F("</dd><dt>Configured inverters</dt><dd>"); page += String(inverterCount); page += F("</dd><dt>Modbus/TCP</dt><dd>"); page += sunspecEnabled ? F("Enabled on port 502") : F("Disabled"); page += F("</dd><dt>MQTT</dt><dd>"); page += Mqtt_Format == 0 ? F("Disabled") : (MQTT_Client.connected() ? F("Connected") : F("Disconnected")); page += F("</dd><dt>Flight recorder</dt><dd>"); page += flightRecorderIsEnabled() ? F("Enabled") : F("Disabled"); page += F("</dd><dt>Wi-Fi disconnects since boot</dt><dd>"); page += String(wifiDisconnectsSinceBoot()); page += F("</dd><dt>Last Wi-Fi disconnect</dt><dd>"); page += webEscape(wifiLastDisconnectTimestamp()); if (wifiDisconnectsSinceBoot()) { page += F("<br><span class=\"help\">"); page += webEscape(wifiLastDisconnectReasonText()); page += F("</span>"); } page += F("</dd></dl></section></div>");
  page += ecuPageEnd();
  request->send(200, "text/html", page);
}

void handleDiagnosticsPage(AsyncWebServerRequest *request) {
  String page = ecuPageStart(F("Diagnostic snapshot"),
      F("A bounded support report with firmware, network, radio, inverter and recent trace information. Passwords are never included."));
  page += F("<div class=\"alert info\"><strong>Persistent flight recorder: "); page += flightRecorderIsEnabled() ? F("enabled") : F("disabled"); page += F("</strong><br>It is an opt-in, fixed-size 12-hour circular flash log for diagnosing lockups without a panic. Configure it under Polling and access. Existing records remain downloadable when recording is disabled.</div><div class=\"actions\"><a class=\"button\" href=\"/diagnostics/download\">Download report</a><a class=\"button secondary\" href=\"/diagnostics/flight-recorder\">Download flight recorder</a><a class=\"button secondary\" href=\"/diagnostics/coredump\">Download crash dump</a><button class=\"secondary\" onclick=\"location.reload()\">Refresh</button><a class=\"button secondary\" href=\"/console\">Open live console</a></div><section class=\"card section\"><pre style=\"white-space:pre-wrap;word-break:break-word;max-height:65vh;overflow:auto\">");
  page += webEscape(diagnosticsReportText());
  page += F("</pre></section>");
  page += ecuPageEnd();
  request->send(200, "text/html; charset=utf-8", page);
}

void handleJournalPage(AsyncWebServerRequest *request) {
  String page = ecuPageStart(F("Journal"), F("Recent operational events retained in memory. Newest entries are shown first."));
  page += F("<div class=\"actions\"><button class=\"secondary\" onclick=\"location.reload()\">Refresh</button><a class=\"button secondary\" href=\"/diagnostics\">Diagnostic snapshot</a></div><div class=\"table-wrap section\"><table class=\"data-table\"><thead><tr><th>Time</th><th>Type</th><th>Description</th></tr></thead><tbody>");
  page += putList(F("rows"));
  page += F("</tbody></table></div>");
  page += ecuPageEnd();
  request->send(200, "text/html", page);
}

const char ENERGY_PAGE[] PROGMEM = R"=====energy(
<!doctype html><html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Energy history &middot; APsystems ECU</title><link rel="icon" href="/favicon.ico"><link rel="stylesheet" href="/stylesheet"></head><body><header class="topbar"><a class="brand" href="/">ESP32-C6 ECU</a><nav class="nav"><a class="button secondary" href="/">Dashboard</a><a class="button secondary" href="/menu">Menu</a></nav></header><main class="page"><div class="eyebrow">Production records</div><h1>Energy history</h1><p>Hourly buckets for the current day stay in RAM. Daily totals are appended to flash once per day.</p><div id="status" class="alert" style="display:none"></div><div class="field" style="max-width:360px"><label for="source">Chart source</label><select id="source" onchange="loadHourly()"><option value="-1">All inverters</option></select></div><section class="card section"><div class="card-head"><h2>Today by hour</h2><strong id="today">&mdash;</strong></div><div class="energy-chart"><div id="yAxis" class="y-axis"><span>&mdash;</span><span>&mdash;</span><span>0 Wh</span></div><div id="bars" class="bars"></div></div></section><section class="section"><div class="card-head"><div><h2>Daily production</h2><p>Most recent 90 finalized days plus today.</p></div><a class="button secondary" href="/api/energy/history.csv">Download CSV</a></div><div class="table-wrap"><table class="data-table"><thead id="head"></thead><tbody id="days"><tr><td>Loading&hellip;</td></tr></tbody></table></div></section><section class="section"><div class="card-head"><div><h2>Backup and recovery</h2><p>The binary backup preserves finalized daily records exactly. CSV is intended for spreadsheets and cannot be restored.</p></div><a class="button" href="/energy/backup">Download restorable backup</a></div><div class="card-grid section"><form class="form-card" method="post" action="/energy/save-today" onsubmit="return confirm('Write the current daily production checkpoint to flash now?')"><h2>Save today before shutdown</h2><p>Writes a CRC-protected snapshot of today's totals to flash without ending the day. A same-day restart restores it and continues accumulating. Hourly buckets and observed min/max statistics remain RAM-only.</p><button type="submit">Save today to flash now</button></form><form class="form-card" method="post" action="/energy/restore" enctype="multipart/form-data"><h2>Restore history</h2><p>Restoring replaces the finalized journal after every record passes its native format, date-range, and CRC validation. Current-day hourly RAM is reset.</p><div class="field"><label for="backup">History backup (.bin)</label><input id="backup" type="file" name="history" accept=".bin,application/octet-stream" required></div><button type="submit" onclick="return confirm('Replace the current finalized production history with this validated backup?')">Validate and restore</button></form><form class="form-card" method="post" action="/energy/wipe" onsubmit="return confirm('Permanently delete all finalized production history and current-day RAM counters? This cannot be undone.')"><h2>Wipe history</h2><p><strong>Permanent:</strong> download a backup first. This deletes the journal, any shutdown checkpoint, and resets current-day hourly and statistics RAM.</p><div class="field"><label for="confirm">Type WIPE to confirm</label><input id="confirm" name="confirm" pattern="WIPE" autocomplete="off" required></div><button class="danger" type="submit">Permanently wipe history</button></form></div></section></main><script>
const fmt=w=>(Number(w||0)/1000).toFixed(3)+' kWh',date=n=>{let s=String(n);return s.length===8?`${s.slice(0,4)}-${s.slice(4,6)}-${s.slice(6)}`:s};let count=0;
async function loadHistory(){let p=new URLSearchParams(location.search),status=p.get('status'),notice=document.getElementById('status');if(status==='restored'||status==='wiped'||status==='saved'){notice.style.display='block';notice.textContent=status==='restored'?'Production history restored successfully. Current-day RAM counters were reset.':status==='wiped'?'Production history was permanently wiped.':'Current-day production was saved to flash. It remains an active day and will continue accumulating.'}let d=await fetch('/api/energy/days?limit=90',{cache:'no-store'}).then(r=>r.json());count=d.inverter_count;let sel=document.getElementById('source'),wanted=p.get('inv');for(let i=0;i<count;i++){let o=document.createElement('option');o.value=i;o.textContent='Inverter '+(i+1);sel.appendChild(o)}if(wanted!==null)sel.value=wanted;document.getElementById('head').innerHTML='<tr><th>Date</th><th>Total</th>'+Array.from({length:count},(_,i)=>'<th>Inverter '+(i+1)+'</th>').join('')+'</tr>';let rows=[...(d.days||[]),d.today].reverse();document.getElementById('days').innerHTML=rows.map((r,i)=>`<tr><td>${date(r.date)}${i===0?' \u00b7 today':''}</td><td>${fmt(r.total_wh)}</td>${(r.wh||[]).map(fmt).map(v=>'<td>'+v+'</td>').join('')}</tr>`).join('');loadHourly()}
const axisFmt=w=>w>=1000?(w/1000).toFixed(w>=10000?0:1)+' kWh':Math.round(w)+' Wh',niceMax=v=>{if(v<=0)return 1;let p=10**Math.floor(Math.log10(v)),n=v/p;return (n<=1?1:n<=2?2:n<=5?5:10)*p};
async function loadHourly(){let inv=document.getElementById('source').value,d=await fetch('/api/energy/hourly?inv='+inv,{cache:'no-store'}).then(r=>r.json()),a=d.hourly_wh||[],scale=niceMax(Math.max(0,...a));document.getElementById('today').textContent=fmt(d.today_wh);document.getElementById('yAxis').innerHTML=`<span>${axisFmt(scale)}</span><span>${axisFmt(scale/2)}</span><span>0 Wh</span>`;document.getElementById('bars').innerHTML=a.map((v,h)=>`<div class="bar" title="${h}:00 \u2014 ${fmt(v)}" style="height:${Math.max(v?2:0,v/scale*100)}%"><span>${h%3===0?h:''}</span></div>`).join('')}
loadHistory().catch(()=>document.getElementById('days').innerHTML='<tr><td>Unable to load history.</td></tr>');
</script></body></html>
)=====energy";
