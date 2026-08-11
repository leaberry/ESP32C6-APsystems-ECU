namespace {
constexpr const char *WIFI_PREFS_NAMESPACE = "aps-wifi";
constexpr const char *WIFI_PREF_SSID = "ssid";
constexpr const char *WIFI_PREF_PASSWORD = "password";
constexpr const char *WIFI_PREF_HOSTNAME = "hostname";
constexpr const char *WIFI_PREF_DHCP = "dhcp";
constexpr const char *WIFI_PREF_IP = "ip";
constexpr const char *WIFI_PREF_NETMASK = "netmask";
constexpr const char *WIFI_PREF_GATEWAY = "gateway";
constexpr uint32_t PORTAL_REBOOT_DELAY_MS = 1800;
constexpr uint32_t PORTAL_TIMEOUT_MS = 10UL * 60UL * 1000UL;

String portalNetworkOptions;
String portalHostname;
bool portalUseDhcp = true;
String portalStaticIp;
String portalNetmask = "255.255.255.0";
String portalGateway;
volatile bool portalRebootPending = false;
uint32_t portalRebootAt = 0;

String htmlEscape(const String &input) {
  String escaped;
  escaped.reserve(input.length() + 8);
  for (size_t i = 0; i < input.length(); ++i) {
    switch (input[i]) {
      case '&': escaped += F("&amp;"); break;
      case '<': escaped += F("&lt;"); break;
      case '>': escaped += F("&gt;"); break;
      case '\"': escaped += F("&quot;"); break;
      case '\'': escaped += F("&#39;"); break;
      default: escaped += input[i]; break;
    }
  }
  return escaped;
}

String defaultWifiHostname() {
  uint8_t mac[6] = {};
  esp_read_mac(mac, ESP_MAC_WIFI_STA);
  char value[24];
  snprintf(value, sizeof(value), "aps-ecu-%02x%02x%02x", mac[3], mac[4], mac[5]);
  return String(value);
}

String normalizeWifiHostname(String value) {
  value.trim();
  value.toLowerCase();
  String normalized;
  normalized.reserve(32);
  bool previousWasDash = false;
  for (size_t i = 0; i < value.length() && normalized.length() < 32; ++i) {
    char c = value[i];
    bool alphaNumeric = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
    if (alphaNumeric) {
      normalized += c;
      previousWasDash = false;
    } else if (!normalized.isEmpty() && !previousWasDash) {
      normalized += '-';
      previousWasDash = true;
    }
  }
  while (normalized.endsWith("-")) normalized.remove(normalized.length() - 1);
  return normalized.isEmpty() ? defaultWifiHostname() : normalized;
}

String wifiStationMacAddress() {
  uint8_t mac[6] = {};
  esp_read_mac(mac, ESP_MAC_WIFI_STA);
  char value[18];
  snprintf(value, sizeof(value), "%02X:%02X:%02X:%02X:%02X:%02X",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  return String(value);
}

String buildPortalPage(const String &message = String()) {
  String page;
  page.reserve(5000 + portalNetworkOptions.length());
  page += F("<!doctype html><html><head><meta charset='utf-8'>");
  page += F("<meta name='viewport' content='width=device-width,initial-scale=1'>");
  page += F("<title>ESP32-C6 APS-ECU Setup</title><style>");
  page += F("body{font-family:system-ui,sans-serif;background:#eef3f5;margin:0;padding:18px;color:#172126}");
  page += F("main{max-width:620px;margin:auto;background:white;padding:24px;border-radius:12px;box-shadow:0 2px 12px #0002}");
  page += F("h1{font-size:1.55rem;margin-top:0}label{display:block;margin-top:15px;font-weight:600}");
  page += F("input,select,button{box-sizing:border-box;width:100%;font:inherit;padding:11px;margin-top:5px}");
  page += F("button{margin-top:22px;background:#087f5b;color:white;border:0;border-radius:7px;font-weight:700}");
  page += F(".meta{color:#52626b}.message{padding:12px;background:#e6fcf5;border-radius:7px}.hint{font-size:.9rem;color:#52626b}");
  page += F("</style></head><body><main><h1>ESP32-C6 APS-ECU</h1>");
  page += F("<p class='meta'>Device MAC address: ");
  page += wifiStationMacAddress();
  page += F("</p>");
  if (!message.isEmpty()) {
    page += F("<p class='message'>");
    page += htmlEscape(message);
    page += F("</p>");
  }
  page += F("<form method='post' action='/wifi/save'>");
  page += F("<label for='s'>Wi-Fi network</label><input id='s' name='s' list='networks' maxlength='32' required>");
  page += F("<datalist id='networks'>");
  page += portalNetworkOptions;
  page += F("</datalist><p class='hint'>Choose a detected 2.4 GHz network or type a hidden SSID.</p>");
  page += F("<label for='p'>Wi-Fi password</label><input id='p' name='p' type='password' maxlength='63' autocomplete='new-password'>");
  page += F("<label for='hostname'>Device hostname</label><input id='hostname' name='hostname' maxlength='32' value='");
  page += htmlEscape(portalHostname);
  page += F("' required><p class='hint'>Letters, numbers, and hyphens; sent to DHCP on the next boot.</p>");
  page += F("<label for='addressing'>IP addressing</label><select id='addressing' name='addressing'>");
  page += portalUseDhcp ? F("<option value='dhcp' selected>DHCP</option><option value='static'>Static</option>")
                        : F("<option value='dhcp'>DHCP</option><option value='static' selected>Static</option>");
  page += F("</select><label for='ip'>Static IP address</label><input id='ip' name='ip' inputmode='decimal' value='");
  page += htmlEscape(portalStaticIp);
  page += F("'><label for='netmask'>Static netmask</label><input id='netmask' name='netmask' inputmode='decimal' value='");
  page += htmlEscape(portalNetmask);
  page += F("'><label for='gateway'>Static gateway</label><input id='gateway' name='gateway' inputmode='decimal' value='");
  page += htmlEscape(portalGateway);
  page += F("'><p class='hint'>Static fields are ignored when DHCP is selected. The gateway is also used as DNS.</p>");
  page += F("<label for='pw'>Administrator password</label><input id='pw' name='pw' type='password' minlength='4' maxlength='10' value='");
  page += htmlEscape(String(pswd));
  page += F("' required>");
  page += F("<label for='sl'>Local-network security level</label><input id='sl' name='sl' type='number' min='0' max='9' value='");
  page += String(securityLevel);
  page += F("'><button type='submit'>Save and restart</button></form></main></body></html>");
  return page;
}

void scanPortalNetworks() {
  portalNetworkOptions = "";
  int16_t count = WiFi.scanNetworks(false, true);
  Serial.println("Portal scan found " + String(count) + " networks");
  for (int i = 0; i < count; ++i) {
    String foundSsid = WiFi.SSID(i);
    if (foundSsid.isEmpty()) continue;
    bool duplicate = false;
    for (int previous = 0; previous < i; ++previous) {
      if (WiFi.SSID(previous) == foundSsid) {
        duplicate = true;
        break;
      }
    }
    if (duplicate) continue;
    portalNetworkOptions += F("<option value='");
    portalNetworkOptions += htmlEscape(foundSsid);
    portalNetworkOptions += F("'>");
  }
  WiFi.scanDelete();
}
}  // namespace

void loadStoredWifiCredentials(String &storedSsid, String &storedPassword,
                               String &storedHostname) {
  storedSsid = "";
  storedPassword = "";
  storedHostname = defaultWifiHostname();
  Preferences wifiPrefs;
  if (!wifiPrefs.begin(WIFI_PREFS_NAMESPACE, true)) return;
  storedSsid = wifiPrefs.getString(WIFI_PREF_SSID, "");
  storedPassword = wifiPrefs.getString(WIFI_PREF_PASSWORD, "");
  storedHostname = normalizeWifiHostname(
      wifiPrefs.getString(WIFI_PREF_HOSTNAME, defaultWifiHostname()));
  wifiPrefs.end();
}

void loadStoredWifiAddressing(bool &useDhcp, String &staticIp,
                              String &netmask, String &gateway) {
  useDhcp = true;
  staticIp = "";
  netmask = "255.255.255.0";
  gateway = "";
  Preferences wifiPrefs;
  if (!wifiPrefs.begin(WIFI_PREFS_NAMESPACE, true)) return;
  useDhcp = wifiPrefs.getBool(WIFI_PREF_DHCP, true);
  staticIp = wifiPrefs.getString(WIFI_PREF_IP, "");
  netmask = wifiPrefs.getString(WIFI_PREF_NETMASK, "255.255.255.0");
  gateway = wifiPrefs.getString(WIFI_PREF_GATEWAY, "");
  wifiPrefs.end();
}

void saveStoredWifiConfiguration(const String &storedSsid,
                                 const String &storedPassword,
                                 const String &storedHostname, bool useDhcp,
                                 const String &staticIp, const String &netmask,
                                 const String &gateway) {
  Preferences wifiPrefs;
  if (!wifiPrefs.begin(WIFI_PREFS_NAMESPACE, false)) return;
  wifiPrefs.putString(WIFI_PREF_SSID, storedSsid);
  wifiPrefs.putString(WIFI_PREF_PASSWORD, storedPassword);
  wifiPrefs.putString(WIFI_PREF_HOSTNAME, normalizeWifiHostname(storedHostname));
  wifiPrefs.putBool(WIFI_PREF_DHCP, useDhcp);
  wifiPrefs.putString(WIFI_PREF_IP, staticIp);
  wifiPrefs.putString(WIFI_PREF_NETMASK, netmask);
  wifiPrefs.putString(WIFI_PREF_GATEWAY, gateway);
  wifiPrefs.end();
}

String normalizedWifiHostname(const String &value) {
  return normalizeWifiHostname(value);
}

void clearStoredWifiCredentials() {
  Preferences wifiPrefs;
  wifiPrefs.begin(WIFI_PREFS_NAMESPACE, false);
  wifiPrefs.clear();
  wifiPrefs.end();
}

void start_portal() {
  Serial.println(F("Starting Wi-Fi setup portal"));
  WiFi.setAutoReconnect(false);
  WiFi.persistent(false);
  WiFi.mode(WIFI_OFF);
  delay(300);
  WiFi.mode(WIFI_AP_STA);
  delay(200);

  IPAddress apIP(192, 168, 4, 1);
  IPAddress netmask(255, 255, 255, 0);
  portalHostname = defaultWifiHostname();
  String ignoredSsid;
  String ignoredPassword;
  loadStoredWifiCredentials(ignoredSsid, ignoredPassword, portalHostname);
  loadStoredWifiAddressing(portalUseDhcp, portalStaticIp,
                           portalNetmask, portalGateway);

  WiFi.softAPConfig(apIP, apIP, netmask);
  if (!WiFi.softAP(portalHostname.c_str(), nullptr, 6, false, 4)) {
    Serial.println(F("ERROR: could not start Wi-Fi setup access point"));
    delay(2000);
    ESP.restart();
  }
  Serial.println("Setup AP: " + portalHostname);
  Serial.println("Setup URL: http://" + WiFi.softAPIP().toString() + "/");

  scanPortalNetworks();

  dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
  dnsServer.start(DNS_PORT, "*", apIP);

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/html", buildPortalPage());
  });
  server.on("/wifi", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->redirect("/");
  });
  server.on("/wifi/save", HTTP_POST, [](AsyncWebServerRequest *request) {
    String submittedSsid = request->arg("s");
    String submittedPassword = request->arg("p");
    String submittedHostname = normalizeWifiHostname(request->arg("hostname"));
    String submittedAdminPassword = request->arg("pw");
    bool submittedDhcp = request->arg("addressing") != "static";
    String submittedIp = request->arg("ip");
    String submittedNetmask = request->arg("netmask");
    String submittedGateway = request->arg("gateway");

    submittedSsid.trim();
    submittedIp.trim();
    submittedNetmask.trim();
    submittedGateway.trim();
    IPAddress parsedIp;
    IPAddress parsedNetmask;
    IPAddress parsedGateway;
    bool staticAddressValid = submittedDhcp ||
        (parsedIp.fromString(submittedIp) &&
         parsedNetmask.fromString(submittedNetmask) &&
         parsedGateway.fromString(submittedGateway));
    if (submittedSsid.isEmpty() || submittedSsid.length() > 32 ||
        submittedPassword.length() > 63 ||
        submittedAdminPassword.length() < 4 || submittedAdminPassword.length() > 10 ||
        !staticAddressValid) {
      request->send(400, "text/html", buildPortalPage("Please check the submitted values."));
      return;
    }

    Preferences wifiPrefs;
    wifiPrefs.begin(WIFI_PREFS_NAMESPACE, false);
    wifiPrefs.putString(WIFI_PREF_SSID, submittedSsid);
    wifiPrefs.putString(WIFI_PREF_PASSWORD, submittedPassword);
    wifiPrefs.putString(WIFI_PREF_HOSTNAME, submittedHostname);
    wifiPrefs.putBool(WIFI_PREF_DHCP, submittedDhcp);
    wifiPrefs.putString(WIFI_PREF_IP, submittedIp);
    wifiPrefs.putString(WIFI_PREF_NETMASK, submittedNetmask);
    wifiPrefs.putString(WIFI_PREF_GATEWAY, submittedGateway);
    wifiPrefs.end();

    strlcpy(pswd, submittedAdminPassword.c_str(), sizeof(pswd));
    securityLevel = constrain(request->arg("sl").toInt(), 0, 9);
    wifiConfigsave();

    String response = F("<!doctype html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'></head><body><h2>Settings saved</h2><p>The APS-ECU is restarting and will request an address using hostname <strong>");
    response += htmlEscape(submittedHostname);
    response += F("</strong>.</p></body></html>");
    request->send(200, "text/html", response);
    portalRebootAt = millis() + PORTAL_REBOOT_DELAY_MS;
    portalRebootPending = true;
  });

  auto captiveRedirect = [](AsyncWebServerRequest *request) {
    request->redirect("http://192.168.4.1/");
  };
  server.on("/generate_204", HTTP_ANY, captiveRedirect);
  server.on("/redirect", HTTP_ANY, captiveRedirect);
  server.on("/hotspot-detect.html", HTTP_ANY, captiveRedirect);
  server.on("/canonical.html", HTTP_ANY, captiveRedirect);
  server.on("/ncsi.txt", HTTP_ANY, captiveRedirect);
  server.onNotFound(captiveRedirect);
  server.begin();

  uint32_t portalStartedAt = millis();
  while (true) {
    dnsServer.processNextRequest();
    if (portalRebootPending && (int32_t)(millis() - portalRebootAt) >= 0) {
      Serial.println(F("Wi-Fi settings saved; restarting"));
      delay(50);
      ESP.restart();
    }
    if ((uint32_t)(millis() - portalStartedAt) >= PORTAL_TIMEOUT_MS) {
      Serial.println(F("Wi-Fi setup portal timed out; restarting"));
      ESP.restart();
    }
    delay(2);
  }
}
