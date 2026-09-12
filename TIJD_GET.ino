namespace {
portMUX_TYPE ntpSettingsMux = portMUX_INITIALIZER_UNLOCKED;
char configuredNtpServer[254] = "pool.ntp.org";
int8_t ntpSyncStatus = 0;
}

String ntpServerSetting() {
  char copy[254];
  portENTER_CRITICAL(&ntpSettingsMux);
  memcpy(copy, configuredNtpServer, sizeof(copy));
  portEXIT_CRITICAL(&ntpSettingsMux);
  return String(copy);
}

void ntpSetServer(const char *server) {
  portENTER_CRITICAL(&ntpSettingsMux);
  strlcpy(configuredNtpServer, server, sizeof(configuredNtpServer));
  ntpSyncStatus = 0;
  portEXIT_CRITICAL(&ntpSettingsMux);
}

String ntpStatusText() {
  portENTER_CRITICAL(&ntpSettingsMux);
  int8_t status = ntpSyncStatus;
  portEXIT_CRITICAL(&ntpSettingsMux);
  return status == 1 ? F("Last synchronization succeeded") :
      status == -1 ? F("Last synchronization failed; will retry") :
      F("Waiting for synchronization");
}

bool ntpNeedsSync() {
  portENTER_CRITICAL(&ntpSettingsMux);
  bool needed = ntpSyncStatus != 1;
  portEXIT_CRITICAL(&ntpSettingsMux);
  return needed;
}

void getTijd() {
  // The library retains this pointer. Keep it alive and snapshot the setting
  // so an HTTP save cannot alter a request in flight.
  static char activeServer[254];
  static uint32_t lastAttempt = 0;
  static bool attempted = false;
  portENTER_CRITICAL(&ntpSettingsMux);
  bool changed = ntpSyncStatus == 0;
  portEXIT_CRITICAL(&ntpSettingsMux);
  if (attempted && !changed && uint32_t(millis() - lastAttempt) < 60000) return;
  lastAttempt = millis();
  attempted = true;
  ntpServerSetting().toCharArray(activeServer, sizeof(activeServer));
  timeClient.setPoolServerName(activeServer);
  timeClient.begin();
  bool received = timeClient.forceUpdate();
  unsigned long epochTime = received ? timeClient.getEpochTime() : 0;
  timeClient.end();
  bool synced = received && epochTime >= 1577836800UL &&
      ecuSetLocalTimeFromUtc((time_t)epochTime);
  portENTER_CRITICAL(&ntpSettingsMux);
  if (!strcmp(activeServer, configuredNtpServer)) ntpSyncStatus = synced ? 1 : -1;
  portEXIT_CRITICAL(&ntpSettingsMux);
  if (!synced) {
    // Preserve the running clock on an outage. At cold boot timeRetrieved
    // remains false so daylight polling falls back to 24-hour operation.
    Update_Log(1, "time sync failed");
    return;
  }
  timeRetrieved = true;
  Update_Log(1, "got time");
  datum = ecuDay(ecuNow());
  sun_setrise();
}
