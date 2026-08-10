const char MENUPAGE[] PROGMEM = R"=====menu(
<!doctype html><html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>APsystems ECU menu</title><link rel="stylesheet" href="/STYLESHEET"></head>
<body><header class="topbar"><a class="brand" href="/">APsystems ECU</a><nav class="nav"><a class="button secondary" href="/">Dashboard</a></nav></header><main class="page"><div class="eyebrow">Administration</div><h1>Menu</h1><p>Configure the ECU, inspect its status, and manage your inverters.</p><div class="menu-grid">
<a class="menu-item" href="/BASISCONFIG"><strong>Polling and access</strong><span>Automatic polling, interval, ECU ID and user password</span></a>
<a class="menu-item" href="/GEOCONFIG"><strong>Time and location</strong><span>Coordinates, time zone and daylight-aware polling</span></a>
<a class="menu-item" href="/NETWORK"><strong>Network</strong><span>Hostname, DHCP, static addressing and Wi-Fi details</span></a>
<a class="menu-item" href="/INV_CONFIG"><strong>Inverters</strong><span>Add, pair, edit or remove microinverters</span></a>
<a class="menu-item" href="/ENERGY"><strong>Energy history</strong><span>Hourly output today and recorded daily totals</span></a>
<a class="menu-item" href="/ABOUT"><strong>System information</strong><span>Firmware, memory, radio, network and polling status</span></a>
<a class="menu-item" href="/MQTT"><strong>MQTT</strong><span>Broker and publishing configuration</span></a>
<a class="menu-item" href="/GRIDPROFILE"><strong>Grid profiles</strong><span>Read and cautiously manage protection settings</span></a>
<a class="menu-item" href="/LOGPAGE"><strong>Journal</strong><span>Recent operational messages</span></a>
<a class="menu-item" href="/CONSOLE"><strong>Diagnostics console</strong><span>Live diagnostic output over the network</span></a>
<a class="menu-item" href="/DIAGNOSTICS"><strong>Diagnostic snapshot</strong><span>Download current radio and system diagnostics</span></a>
<a class="menu-item" href="/FWUPDATE"><strong>Firmware update</strong><span>Install an OTA image on supported 8 MB builds</span></a>
</div><div class="actions"><a class="button danger" onclick="return confirm('Restart the ECU?')" href="/REBOOT">Restart ECU</a><a class="button secondary" onclick="return confirm('Start the setup access point?')" href="/STARTAP">Start setup access point</a></div></main></body></html>
)=====menu";
