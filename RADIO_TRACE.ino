/* Raw 802.15.4 trace and APsystems proprietary pairing-reply parser. */

bool rawRadioSetPan(uint16_t pan);
bool rawRadioSetPromiscuous(bool enabled);
bool apsRadioRememberPeer(const char *serial, uint16_t pan, uint16_t source);

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
uint16_t radioTraceMatchedPan = 0;
uint16_t radioTraceMatchedSource = 0;

bool radioTraceFindPairReplyInternal(const char *serial, char inverterId[5]) {
  if (!serial || strlen(serial) != 12 || !inverterId) return false;

  uint8_t ieee[6];
  for (uint8_t i = 0; i < 6; ++i) {
    char hi = serial[i * 2];
    char lo = serial[i * 2 + 1];
    if (hi < '0' || hi > '9' || lo < '0' || lo > '9') return false;
    ieee[i] = ((uint8_t)(hi - '0') << 4) | (uint8_t)(lo - '0');
  }

  for (uint8_t i = 0; i < radioTraceCount; ++i) {
    RadioTraceFrame &entry = radioTraceFrames[i];
    if (entry.consumed || entry.captured < 28) continue;
    const uint8_t *b = entry.bytes;

    // Proprietary APsystems NWK command 0x1009. This is the frame ZBOSS
    // discarded before APS delivery and is why the raw transport is needed.
    if (b[1] != 0x41 || b[2] != 0x88 ||
        b[10] != 0x09 || b[11] != 0x10 ||
        b[12] != 0xFC || b[13] != 0xFF ||
        b[18] != 0xFF || b[19] != 0xFF ||
        memcmp(b + 20, ieee, sizeof(ieee)) != 0) continue;

    uint16_t id = ((uint16_t)b[26] << 8) | b[27];
    if (id == 0 || id >= 0xFFF8) continue;

    snprintf(inverterId, 5, "%02X%02X", b[26], b[27]);
    entry.consumed = 1;
    radioTraceMatchedPan = (uint16_t)b[4] | ((uint16_t)b[5] << 8);
    radioTraceMatchedSource = (uint16_t)b[8] | ((uint16_t)b[9] << 8);
    apsRadioRememberPeer(serial, radioTraceMatchedPan,
                         radioTraceMatchedSource);
    char line[168];
    snprintf(line, sizeof(line),
             "raw pairing reply serial=%s invID=%s mac_src=0x%04X pan=0x%04X rssi=%d",
             serial, inverterId, radioTraceMatchedSource,
             radioTraceMatchedPan, entry.rssi);
    diagnosticsAppend(String(line));
    return true;
  }
  return false;
}
}  // namespace

void radioTraceObserve(const uint8_t *frame, uint8_t captured,
                       uint8_t channel, int8_t rssi, uint8_t lqi) {
  if (!radioTraceActive || !frame) return;
  if (radioTraceCount >= RADIO_TRACE_CAPACITY) {
    ++radioTraceDropped;
    return;
  }
  RadioTraceFrame &entry = radioTraceFrames[radioTraceCount++];
  entry.captured = min(captured, (uint8_t)RADIO_TRACE_FRAME_BYTES);
  entry.channel = channel;
  entry.rssi = rssi;
  entry.lqi = lqi;
  entry.consumed = 0;
  memcpy(entry.bytes, frame, entry.captured);
}

bool radioTraceFindPairReply(const char *serial, char inverterId[5]) {
  return radioTraceFindPairReplyInternal(serial, inverterId);
}

uint16_t radioTraceLastPairPan() { return radioTraceMatchedPan; }
uint16_t radioTraceLastPairShort() { return radioTraceMatchedSource; }

bool radioTraceSetHardwarePan(uint16_t pan) { return rawRadioSetPan(pan); }
bool radioTraceUseFilteredReception() { return rawRadioSetPromiscuous(false); }
esp_err_t radioTraceRegister() { return ESP_OK; }

void radioTraceBegin() {
  radioTraceActive = false;
  radioTraceCount = 0;
  radioTraceDropped = 0;
  radioTraceMatchedPan = 0;
  radioTraceMatchedSource = 0;
  memset(radioTraceFrames, 0, sizeof(radioTraceFrames));
  radioTraceActive = rawRadioSetPromiscuous(true);
  diagnosticsAppend(radioTraceActive
                        ? F("802.15.4 pairing trace start: OK")
                        : F("802.15.4 pairing trace start: FAILED"));
}

void radioTraceEnd() {
  radioTraceActive = false;
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
