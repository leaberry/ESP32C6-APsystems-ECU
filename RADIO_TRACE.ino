/* Raw 802.15.4 trace and APsystems proprietary pairing-reply parser. */

bool rawRadioSetPan(uint16_t pan);
bool rawRadioSetPromiscuous(bool enabled);
bool apsRadioRememberPeer(const char *serial, uint16_t pan, uint16_t source);
bool apsRadioLoadPeer(const char *serial, uint16_t *pan, uint16_t *source);

namespace {
constexpr uint8_t RADIO_TRACE_CAPACITY = 48;
constexpr uint8_t RADIO_TRACE_FRAME_BYTES = 128;
constexpr uint8_t RADIO_TRACE_LOG_BYTES = 68;

struct RadioTraceFrame {
  uint8_t captured;
  uint8_t channel;
  int8_t rssi;
  uint8_t lqi;
  uint8_t consumed;
  uint8_t bytes[RADIO_TRACE_FRAME_BYTES];
};

RadioTraceFrame radioTraceFrames[RADIO_TRACE_CAPACITY] = {};
uint8_t radioTraceCount = 0;
uint16_t radioTraceDropped = 0;
bool radioTraceActive = false;
portMUX_TYPE pairReceiveMux = portMUX_INITIALIZER_UNLOCKED;
PairReplySession pairReceiveSession;
int8_t pairReceiveRssi = 0;
uint8_t pairReceiveLqi = 0;
}  // namespace

// Matching runs before the bounded trace store, so a full log never loses pairing.
void radioTraceObserve(const uint8_t *frame, uint8_t captured,
                       uint8_t channel, int8_t rssi, uint8_t lqi) {
  if (!frame || channel != 16) return;
  PairReply latest;
  portENTER_CRITICAL(&pairReceiveMux);
  bool changed = pairReceiveSession.observe(frame, captured);
  if (changed) { latest = pairReceiveSession.latest; pairReceiveRssi = rssi; pairReceiveLqi = lqi; }
  if (radioTraceActive) {
    if (radioTraceCount >= RADIO_TRACE_CAPACITY) ++radioTraceDropped;
    else {
      RadioTraceFrame &entry = radioTraceFrames[radioTraceCount];
      entry.captured = min(captured, (uint8_t)RADIO_TRACE_FRAME_BYTES);
      entry.channel = channel;
      entry.rssi = rssi;
      entry.lqi = lqi;
      memcpy(entry.bytes, frame, entry.captured);
      ++radioTraceCount;
    }
  }
  portEXIT_CRITICAL(&pairReceiveMux);
  if (changed) {
    char line[160];
    snprintf(line, sizeof(line),
             "pair RX %s pan=0x%04X source=0x%04X prefix=%04X rssi=%d",
             latest.announcement ? "announcement" : "direct", latest.pan,
             latest.source, latest.prefix, rssi);
    diagnosticsAppend(String(line));
  }
}

bool pairReceiveBegin(const char *serial, uint16_t pan) {
  portENTER_CRITICAL(&pairReceiveMux);
  bool ok = pairReceiveSession.begin(serial, pan);
  portEXIT_CRITICAL(&pairReceiveMux);
  return ok;
}

bool pairReceiveActive() {
  portENTER_CRITICAL(&pairReceiveMux);
  bool active = pairReceiveSession.active;
  portEXIT_CRITICAL(&pairReceiveMux);
  return active;
}

void pairReceiveVerify() {
  portENTER_CRITICAL(&pairReceiveMux);
  pairReceiveSession.verify();
  portEXIT_CRITICAL(&pairReceiveMux);
}

bool pairReceiveFinish(char inverterId[5], uint16_t *pan, uint16_t *source) {
  portENTER_CRITICAL(&pairReceiveMux);
  bool ok = pairReceiveSession.success();
  PairReply verified = pairReceiveSession.verified;
  PairReply latest = pairReceiveSession.latest;
  uint16_t replies = pairReceiveSession.replies, dropped = radioTraceDropped;
  bool conflict = pairReceiveSession.conflict;
  int8_t rssi = pairReceiveRssi;
  uint8_t lqi = pairReceiveLqi;
  portEXIT_CRITICAL(&pairReceiveMux);
  pairAuditEvent(PA_RX_SUMMARY, ok, 0, latest.pan, latest.source,
                 replies, dropped, rssi, lqi, conflict);
  if (!ok) return false;
  snprintf(inverterId, 5, "%02X%02X", verified.id & 0xFF, verified.id >> 8);
  *pan = verified.pan;
  *source = verified.source;
  return true;
}

void pairReceiveStop() {
  portENTER_CRITICAL(&pairReceiveMux);
  pairReceiveSession.active = false;
  portEXIT_CRITICAL(&pairReceiveMux);
}

bool radioTraceSetHardwarePan(uint16_t pan) { return rawRadioSetPan(pan); }
bool radioTraceUseFilteredReception() { return rawRadioSetPromiscuous(false); }
esp_err_t radioTraceRegister() { return ESP_OK; }

bool radioTraceBegin() {
  // Espressif disables automatic MAC ACKs in promiscuous mode. Keep normal
  // filtering during the handshake; direct replies use the selected PAN.
  bool filtered = rawRadioSetPromiscuous(false);
  portENTER_CRITICAL(&pairReceiveMux);
  radioTraceCount = 0;
  radioTraceDropped = 0;
  radioTraceActive = filtered;
  portEXIT_CRITICAL(&pairReceiveMux);
  diagnosticsAppend(filtered ? F("pairing trace start: filtered reception, MAC ACK enabled")
                             : F("pairing trace start: FAILED"));
  return filtered;
}

void radioTraceEnd() {
  portENTER_CRITICAL(&pairReceiveMux);
  radioTraceActive = false;
  portEXIT_CRITICAL(&pairReceiveMux);
  bool filtered = rawRadioSetPromiscuous(false);
  char line[192];
  snprintf(line, sizeof(line),
           "802.15.4 pairing trace stop: %s frames=%u dropped=%u",
           filtered ? "OK" : "FAILED", radioTraceCount, radioTraceDropped);
  diagnosticsAppend(String(line));

  static const char hex[] = "0123456789ABCDEF";
  for (uint8_t i = 0; i < radioTraceCount; ++i) {
    const RadioTraceFrame &entry = radioTraceFrames[i];
    uint8_t show = min(entry.captured, (uint8_t)RADIO_TRACE_LOG_BYTES);
    int used = snprintf(line, sizeof(line),
                        "MAC RX %u ch=%u rssi=%d lqi=%u len=%u data=",
                        i, entry.channel, entry.rssi, entry.lqi,
                        entry.captured ? entry.bytes[0] : 0);
    for (uint8_t b = 0; b < show && used + 2 < (int)sizeof(line); ++b) {
      line[used++] = hex[entry.bytes[b] >> 4];
      line[used++] = hex[entry.bytes[b] & 0x0F];
    }
    if (show < entry.captured && used + 3 < (int)sizeof(line)) {
      line[used++] = '.'; line[used++] = '.'; line[used++] = '.';
    }
    line[used] = 0;
    diagnosticsAppend(String(line));
  }
}
