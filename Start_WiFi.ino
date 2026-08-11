// ************************************************************************************
// *                        START Wi-Fi
// ************************************************************************************
volatile uint8_t lastWifiDisconnectReason = 0;

void start_wifi() {
  String storedSsid;
  String storedPassword;
  String hostname;
  bool useDhcp = true;
  String staticIpText;
  String netmaskText;
  String gatewayText;
  loadStoredWifiCredentials(storedSsid, storedPassword, hostname);
  loadStoredWifiAddressing(useDhcp, staticIpText, netmaskText, gatewayText);

  if (storedSsid.isEmpty()) {
    Serial.println(F("No saved Wi-Fi configuration"));
    start_portal();
    return;
  }

  Serial.println("Connecting to " + storedSsid + " as " + hostname);
  WiFi.persistent(false);
  WiFi.setAutoReconnect(false);
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(hostname.c_str());
  if (!useDhcp) {
    IPAddress staticIp;
    IPAddress netmask;
    IPAddress gateway;
    if (!staticIp.fromString(staticIpText) ||
        !netmask.fromString(netmaskText) ||
        !gateway.fromString(gatewayText) ||
        !WiFi.config(staticIp, gateway, netmask, gateway)) {
      Serial.println(F("Invalid stored static IP configuration; opening setup portal"));
      start_portal();
      return;
    }
    Serial.println("Static IP: " + staticIp.toString());
  }
  WiFi.onEvent(
      [](WiFiEvent_t, WiFiEventInfo_t info) {
        lastWifiDisconnectReason = info.wifi_sta_disconnected.reason;
        flightRecorderWifiEvent();
      },
      WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_DISCONNECTED);

  WiFi.begin(storedSsid.c_str(), storedPassword.c_str());
  uint32_t startedAt = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startedAt < 20000UL) {
    delay(250);
    Serial.print('.');
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println();
    Serial.println("Wi-Fi connection failed, reason " +
                   String(lastWifiDisconnectReason) + " (" +
                   WiFi.STA.disconnectReasonName(
                       (wifi_err_reason_t)lastWifiDisconnectReason) +
                   ")");
    start_portal();
    return;
  }

  WiFi.setAutoReconnect(true);
  flightRecorderManageStation(true);
  Serial.println();
  Serial.println("Wi-Fi connected: " + WiFi.localIP().toString());
  Serial.println("DHCP hostname: " + hostname);
  start_server();
}

bool loginBoth(AsyncWebServerRequest *request, String who) {
  if (who == "admin") {
    if (!request->authenticate("admin", pswd)) {
      request->requestAuthentication();
      return false;
    }
  }
  if (who == "both") {
    if (!request->authenticate("admin", pswd) &&
        !request->authenticate("user", userPwd)) {
      request->requestAuthentication();
      return false;
    }
  }
  return true;
}
