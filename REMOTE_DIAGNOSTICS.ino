namespace {
constexpr size_t DIAGNOSTIC_LINE_COUNT = 96;
constexpr size_t DIAGNOSTIC_LINE_LENGTH = 192;

struct DiagnosticLine {
  uint32_t sequence;
  uint32_t milliseconds;
  char text[DIAGNOSTIC_LINE_LENGTH];
};

DiagnosticLine diagnosticLines[DIAGNOSTIC_LINE_COUNT] = {};
uint32_t diagnosticSequence = 0;
portMUX_TYPE diagnosticMux = portMUX_INITIALIZER_UNLOCKED;
}  // namespace

void diagnosticsAppend(const String &message) {
  char copy[DIAGNOSTIC_LINE_LENGTH];
  strlcpy(copy, message.c_str(), sizeof(copy));

  portENTER_CRITICAL(&diagnosticMux);
  uint32_t sequence = ++diagnosticSequence;
  DiagnosticLine &line = diagnosticLines[(sequence - 1) % DIAGNOSTIC_LINE_COUNT];
  line.sequence = sequence;
  line.milliseconds = millis();
  strlcpy(line.text, copy, sizeof(line.text));
  portEXIT_CRITICAL(&diagnosticMux);
}

String diagnosticsText() {
  uint32_t newest;
  portENTER_CRITICAL(&diagnosticMux);
  newest = diagnosticSequence;
  portEXIT_CRITICAL(&diagnosticMux);

  uint32_t oldest = newest > DIAGNOSTIC_LINE_COUNT
                        ? newest - DIAGNOSTIC_LINE_COUNT + 1
                        : 1;
  String output;
  output.reserve(DIAGNOSTIC_LINE_COUNT * 100);
  output += F("ESP32-C6 APS-ECU diagnostic trace\n");
  output += F("Times are milliseconds since boot. Newest entries are last.\n\n");

  for (uint32_t sequence = oldest; sequence <= newest; ++sequence) {
    DiagnosticLine snapshot;
    portENTER_CRITICAL(&diagnosticMux);
    snapshot = diagnosticLines[(sequence - 1) % DIAGNOSTIC_LINE_COUNT];
    portEXIT_CRITICAL(&diagnosticMux);
    if (snapshot.sequence != sequence) continue;
    output += '[';
    output += snapshot.milliseconds;
    output += F(" ms] ");
    output += snapshot.text;
    if (!output.endsWith("\n")) output += '\n';
  }
  return output;
}

String diagnosticsReportText() {
  String report;
  report.reserve(18000);
  report += F("ESP32-C6 APsystems ECU diagnostic report\n=========================================\n");
  report += F("Firmware: "); report += VERSION;
  report += F("\nBuild: "); report += __DATE__; report += ' '; report += __TIME__;
  report += F("\nUptime seconds: "); report += millis() / 1000UL;
  report += F("\nFree heap bytes: "); report += ESP.getFreeHeap();
  report += F("\nFlash bytes: "); report += ESP.getFlashChipSize();
  report += F("\nWi-Fi SSID: "); report += WiFi.SSID();
  report += F("\nWi-Fi IP: "); report += WiFi.localIP().toString();
  report += F("\nWi-Fi RSSI dBm: "); report += WiFi.RSSI();
  report += F("\nWi-Fi MAC: "); report += WiFi.macAddress();
  report += F("\nRadio state: "); report += zigbeeUp;
  report += F("\nConfigured inverters: "); report += inverterCount;
  report += F("\nAutomatic polling: "); report += Polling ? F("enabled") : F("disabled");
  report += F("\nPoll interval seconds: "); report += pollIntervalSeconds;
  report += F("\nSunSpec/Modbus TCP: "); report += sunspecEnabled ? F("enabled on port 502") : F("disabled");
  report += F("\nLocal time: "); report += ecuClockText();
  report += F("\nTimezone: "); report += timeZoneId;
  report += F("\n\nINVERTERS\n---------\n");
  for (uint8_t i = 0; i < inverterCount; ++i) {
    uint16_t pan = 0, source = 0;
    bool peer = apsRadioLoadPeer(Inv_Prop[i].invSerial, &pan, &source);
    char line[300];
    snprintf(line, sizeof(line),
             "[%u] name=%s serial=%s model=%d id=%s paired=%s polled=%s encrypted=%s peer_pan=0x%04X peer_source=0x%04X firmware=%s radio_rssi_dbm=%d raw_lqi=%u\n",
             i, Inv_Prop[i].invLocation, Inv_Prop[i].invSerial, Inv_Prop[i].invType,
             Inv_Prop[i].invID, strcmp(Inv_Prop[i].invID, "0000") ? "yes" : "no",
             polled[i] ? "yes" : "no", apsInverterUsesEncryption(i) ? "yes" : "no",
             peer ? pan : 0, peer ? source : 0, Inv_Data[i].firmwareVersion,
             Inv_Data[i].radioMetricsValid ? Inv_Data[i].radioRssi : 0,
             Inv_Data[i].radioMetricsValid ? Inv_Data[i].radioLqi : 0);
    report += line;
  }
  report += F("\nTRACE (bounded to the newest 96 entries)\n----------------------------------------\n");
  report += diagnosticsText();
  return report;
}

void diagnosticsDumpToSerial() {
  Serial.print(diagnosticsText());
}
