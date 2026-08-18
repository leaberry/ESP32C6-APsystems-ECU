// ************************************************************************************
// *                        START Wi-Fi
// ************************************************************************************
volatile uint8_t lastWifiDisconnectReason = 0;
namespace {
portMUX_TYPE wifiDisconnectMux = portMUX_INITIALIZER_UNLOCKED;
volatile uint32_t wifiDisconnectCount = 0;
volatile int64_t lastWifiDisconnectAtUs = 0;
}

static void wifiDisconnectSnapshot(uint8_t &reason, uint32_t &count,
                                   int64_t &atUs) {
  portENTER_CRITICAL(&wifiDisconnectMux);
  reason = lastWifiDisconnectReason;
  count = wifiDisconnectCount;
  atUs = lastWifiDisconnectAtUs;
  portEXIT_CRITICAL(&wifiDisconnectMux);
}

String wifiLastDisconnectReasonText() {
  uint8_t reason; uint32_t count; int64_t atUs;
  wifiDisconnectSnapshot(reason, count, atUs);
  if (!count) return F("None");
  const char *name = WiFi.STA.disconnectReasonName((wifi_err_reason_t)reason);
  return String(name && name[0] ? name : "unknown") + F(" (code ") +
      String(reason) + ')';
}

uint32_t wifiDisconnectsSinceBoot() {
  uint8_t reason; uint32_t count; int64_t atUs;
  wifiDisconnectSnapshot(reason, count, atUs);
  return count;
}

String wifiLastDisconnectTimestamp() {
  uint8_t reason; uint32_t count; int64_t atUs;
  wifiDisconnectSnapshot(reason, count, atUs);
  if (!count || !atUs) return F("Never since boot");
  int64_t ageUs = esp_timer_get_time() - atUs;
  if (ageUs < 0) ageUs = 0;
  uint64_t ageSeconds = (uint64_t)ageUs / 1000000ULL;
  if (!timeRetrieved || ecuYear(ecuNow()) < 2020) {
    String text = F("Uptime ");
    text += String((unsigned long)(atUs / 1000000LL));
    text += F(" seconds");
    return text;
  }
  time_t occurred = ecuNow() - (time_t)ageSeconds;
  char text[24];
  snprintf(text, sizeof(text), "%04d-%02d-%02d %02d:%02d:%02d",
           ecuYear(occurred), ecuMonth(occurred), ecuDay(occurred),
           ecuHour(occurred), ecuMinute(occurred), ecuSecond(occurred));
  return String(text);
}

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
        portENTER_CRITICAL(&wifiDisconnectMux);
        lastWifiDisconnectReason = info.wifi_sta_disconnected.reason;
        ++wifiDisconnectCount;
        lastWifiDisconnectAtUs = esp_timer_get_time();
        portEXIT_CRITICAL(&wifiDisconnectMux);
        flightRecorderWifiEvent();
      },
      WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_DISCONNECTED);

  for(uint8_t t=0;t<2 && WiFi.status() != WL_CONNECTED;t++){
    WiFi.begin(storedSsid.c_str(), storedPassword.c_str());
    uint32_t startedAt = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startedAt < 20000UL) {
      delay(250);
      Serial.print('.');
    }
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
