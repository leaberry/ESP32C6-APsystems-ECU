/* Raw 802.15.4 trace and APsystems proprietary pairing-reply parser. */

bool rawRadioSetPan(uint16_t pan);
bool rawRadioSetPromiscuous(bool enabled);
bool apsRadioRememberPeer(const char *serial, uint16_t pan, uint16_t source);
bool apsRadioLoadPeer(const char *serial, uint16_t *pan, uint16_t *source);

namespace {
portMUX_TYPE pairReceiveMux = portMUX_INITIALIZER_UNLOCKED;
PairReplySession pairReceiveSession;
int8_t pairReceiveRssi = 0;
uint8_t pairReceiveLqi = 0;
}  // namespace

// Matching runs before the bounded trace store, so a full log never loses pairing.
void radioTraceObserve(const uint8_t *frame, uint8_t captured,
                       uint8_t channel, int8_t rssi, uint8_t lqi, uint32_t receivedAt) {
  if (!frame || channel != 16) return;
  PairReply latest;
  portENTER_CRITICAL(&pairReceiveMux);
  bool changed = pairReceiveSession.observe(frame, captured);
  if (changed) { latest = pairReceiveSession.latest; pairReceiveRssi = rssi; pairReceiveLqi = lqi; }
  portEXIT_CRITICAL(&pairReceiveMux);
  pairTraceObserve(frame, captured, receivedAt, rssi, lqi);
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
  uint16_t replies = pairReceiveSession.replies;
  bool conflict = pairReceiveSession.conflict;
  int8_t rssi = pairReceiveRssi;
  uint8_t lqi = pairReceiveLqi;
  portEXIT_CRITICAL(&pairReceiveMux);
  pairAuditEvent(PA_RX_SUMMARY, ok, 0, latest.pan, latest.source,
                 replies, pairTraceOmitted(), rssi, lqi, conflict);
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
  uint8_t target[6]; memcpy(target, pairReceiveSession.target, 6);
  portEXIT_CRITICAL(&pairReceiveMux);
  pairTraceBegin(target, filtered && flightRecorderIsEnabled());
  diagnosticsAppend(filtered ? F("pairing trace start: filtered reception, MAC ACK enabled")
                             : F("pairing trace start: FAILED"));
  return filtered;
}

void radioTraceEnd() {
  pairTracePause();
  bool filtered = rawRadioSetPromiscuous(false);
  diagnosticsAppend(filtered ? F("pairing reception restored") : F("pairing reception restore failed"));
}
