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

void diagnosticsDumpToSerial() {
  Serial.print(diagnosticsText());
}
