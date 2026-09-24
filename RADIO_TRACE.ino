#include "PAIRING_DIAGNOSTICS.h"
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
PairDiagnosticCapture pairDiagnosticCapture;
int8_t pairReceiveRssi = 0;
uint8_t pairReceiveLqi = 0;
}  // namespace

// Matching runs before the bounded trace store, so a full log never loses pairing.
void radioTraceObserve(const uint8_t *frame, uint8_t captured,
                       uint8_t channel, int8_t rssi, uint8_t lqi) {
  if (!frame || channel != 16) return;
  PairReply latest;
  portENTER_CRITICAL(&pairReceiveMux);
  pairDiagnosticCapture.observe(frame, captured, pairReceiveSession.target, millis(), rssi, lqi);
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
  pairDiagnosticCapture.begin(flightRecorderEnabled, millis());
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
  pairDiagnosticCapture.active = false;
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

// Phases 0-3 handshake, 4 settle, 5-7 operating-PAN verification,
// 8-10 saved-PAN verification, 11 restore. No writes from the radio worker.
void pairDiagnosticsStage(uint8_t phase, uint16_t pan) {
  portENTER_CRITICAL(&pairReceiveMux);
  pairDiagnosticCapture.stage(phase, pan);
  portEXIT_CRITICAL(&pairReceiveMux);
}

String pairDiagnosticsReport() {
  String out = F("\nPAIRING DETAIL (RAM; last attempt, lost on restart)\n"
    "Opt-in at pairing start using the flight recorder switch. No raw payloads or keys.\n"
    "Phases: 0-3 handshake, 4 settle, 5-7 operating verification, 8-10 saved-network verification.\n"
    "identity: 1=serial seen, 2=reversed serial seen, 3=both; a relay echo is not a reply.\n"
    "Samples retain first two and latest two relevant frames per phase. Zero status bytes on nonextended frames are not decoded status.\n");
  bool enabled, active;
  portENTER_CRITICAL(&pairReceiveMux);
  enabled=pairDiagnosticCapture.enabled; active=pairDiagnosticCapture.active;
  portEXIT_CRITICAL(&pairReceiveMux);
  if (!enabled) return out + F("No detail captured: enable the flight recorder before pairing.\n");
  if (active) return out + F("Pairing in progress; download again after it finishes.\n");
  char line[260];
  for (uint8_t i=0;i<12;++i) {
    PairDiagnosticPhase p;
    portENTER_CRITICAL(&pairReceiveMux);
    p=pairDiagnosticCapture.phases[i];
    portEXIT_CRITICAL(&pairReceiveMux);
    if (!p.entered) continue;
    snprintf(line,sizeof(line),"phase=%u requested_pan=%04X frames=%lu relevant=%lu matched=%lu omitted=%lu\n",
      i,p.requestedPan,(unsigned long)p.frames,(unsigned long)p.relevant,
      (unsigned long)p.matched,(unsigned long)(p.relevant>4?p.relevant-4:0)); out+=line;
    for(uint8_t reason=1;reason<9;++reason) {
      if (!p.rejected[reason]) continue;
      snprintf(line,sizeof(line),"  rejected %s=%lu\n",pairReplyCheckName((PairReplyCheck)reason),
        (unsigned long)p.rejected[reason]); out+=line;
    }
    for(uint8_t j=0;j<4 && j<p.relevant;++j) {
      const PairDiagnosticSample &s=p.samples[j];
      snprintf(line,sizeof(line),"  ms=%lu pan=%04X src=%04X mac_src=%04X nwk=%04X cluster=%04X bytes=%u payload=%u prefix=%04X status=%02X%02X%02X%02X%02X identity=%u check=%s rssi=%d lqi=%u\n",
        (unsigned long)s.elapsed,s.pan,s.source,s.macSource,s.nwk,s.cluster,s.bytes,s.payloadBytes,s.prefix,
        s.status[0],s.status[1],s.status[2],s.status[3],s.status[4],s.identity,pairReplyCheckName((PairReplyCheck)s.reason),s.rssi,s.lqi);
      out+=line;
    }
  }
  return out;
}
