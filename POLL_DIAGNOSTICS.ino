// Opt-in companion to the flight recorder. Only completed attempts are saved,
// in minute batches from the flight-recorder loop; the radio worker never writes.
bool apsRadioLoadPeer(const char *serial, uint16_t *pan, uint16_t *source);
namespace {
constexpr size_t POLL_DIAG_RAM = 32;
constexpr size_t POLL_DIAG_SLOTS = 256;
constexpr char POLL_DIAG_FILE[] = "/poll-diagnostics-v1.bin";
portMUX_TYPE pollDiagMux = portMUX_INITIALIZER_UNLOCKED;
SemaphoreHandle_t pollDiagStorageMutex = nullptr;
PollDiagRecord pollDiagRing[POLL_DIAG_RAM] = {};
PollDiagRecord pollDiagCurrent = {};
uint32_t pollDiagTotals[PD_COUNT] = {};
uint32_t pollDiagBaseline[PD_COUNT] = {};
uint32_t pollDiagSequence = 0, pollDiagSaved = 0, pollDiagBoot = 0;
uint32_t pollDiagPendingLost = 0;
bool pollDiagActive = false, pollDiagWriteFailed = false;
bool pollDiagPersist[POLL_DIAG_RAM] = {};

bool pollDiagStore(const PollDiagRecord &record) {
  File f = SPIFFS.open(POLL_DIAG_FILE, SPIFFS.exists(POLL_DIAG_FILE) ? "r+" : "w+");
  if (!f || f.isDirectory()) { if (f) f.close(); return false; }
  const size_t expected = POLL_DIAG_SLOTS * sizeof(record);
  if (f.size() < expected && f.seek(f.size())) {
    uint8_t zeros[64] = {};
    size_t remaining = expected - f.size();
    while (remaining) {
      size_t n = min(remaining, sizeof(zeros));
      if (f.write(zeros, n) != n) { f.close(); return false; }
      remaining -= n;
      yield();
    }
  }
  bool ok = f.seek(((record.sequence - 1) % POLL_DIAG_SLOTS) * sizeof(record)) &&
    f.write(reinterpret_cast<const uint8_t *>(&record), sizeof(record)) == sizeof(record);
  f.flush(); f.close();
  return ok;
}
void pollDiagAppendRow(String &out, const PollDiagRecord &r, const char *storage) {
  char line[240];
  snprintf(line, sizeof(line), "%s,%lu,%lu,%lld,%.39s,%u,%.4s,%u,%lu,%lu,%u,%u,%04X,%04X,%04X",
    storage, (unsigned long)r.sequence, (unsigned long)r.boot, (long long)r.localEpoch,
    r.firmware, r.inverter, r.serialTail, r.attempt, (unsigned long)r.startedMs,
    (unsigned long)r.elapsedMs, r.txOk, r.result, r.pan, r.source, r.replyMask);
  out += line;
  for (size_t i = 0; i < PD_COUNT; ++i) {
    snprintf(line, sizeof(line), ",%lu", (unsigned long)r.counters[i]); out += line;
  }
  out += '\n';
}
} // namespace

void pollDiagnosticsBegin() {
  pollDiagStorageMutex = xSemaphoreCreateMutex();
  pollDiagBoot = esp_random();
  if (!pollDiagStorageMutex) return;
  File f = SPIFFS.open(POLL_DIAG_FILE, "r");
  if (!f || f.isDirectory()) { if (f) f.close(); return; }
  PollDiagRecord r;
  for (size_t i = 0; i < POLL_DIAG_SLOTS; ++i) {
    if (f.read(reinterpret_cast<uint8_t *>(&r), sizeof(r)) != sizeof(r)) break;
    if (pollDiagValid(r) && r.sequence > pollDiagSequence) pollDiagSequence = r.sequence;
  }
  pollDiagSaved = pollDiagSequence;
  f.close();
}

void pollDiagnosticsCount(PollDiagCounter counter, uint32_t count) {
  portENTER_CRITICAL(&pollDiagMux);
  pollDiagTotals[counter] += count;
  portEXIT_CRITICAL(&pollDiagMux);
}

void IRAM_ATTR pollDiagnosticsRawDrop() {
  portENTER_CRITICAL_ISR(&pollDiagMux);
  ++pollDiagTotals[PD_RAW_DROP];
  portEXIT_CRITICAL_ISR(&pollDiagMux);
}

void pollDiagnosticsRaw(uint16_t pan, uint16_t source) {
  portENTER_CRITICAL(&pollDiagMux);
  if (pollDiagActive && pollDiagCurrent.pan == pan && pollDiagCurrent.source == source)
    ++pollDiagTotals[PD_TARGET_RX];
  portEXIT_CRITICAL(&pollDiagMux);
}

void pollDiagnosticsReply(int which) {
  portENTER_CRITICAL(&pollDiagMux);
  if (pollDiagActive && which >= 0 && which < 9) {
    pollDiagCurrent.replyMask |= 1U << which;
    if (which == pollDiagCurrent.inverter) ++pollDiagTotals[PD_TARGET_ASDU];
  }
  portEXIT_CRITICAL(&pollDiagMux);
}

void pollDiagnosticsStart(int which, uint8_t attempt) {
  uint16_t pan = 0, source = 0;
  apsRadioLoadPeer(Inv_Prop[which].invSerial, &pan, &source);
  PollDiagRecord r = {};
  r.magic = 0x50443131; r.version = 1; r.size = sizeof(r);
  r.boot = pollDiagBoot; r.startedMs = millis();
  r.localEpoch = timeRetrieved ? (int64_t)ecuNow() : 0;
  strlcpy(r.firmware, VERSION, sizeof(r.firmware));
  if (strlen(Inv_Prop[which].invSerial) == 12)
    strlcpy(r.serialTail, Inv_Prop[which].invSerial + 8, sizeof(r.serialTail));
  r.inverter = which; r.attempt = attempt; r.pan = pan; r.source = source;
  portENTER_CRITICAL(&pollDiagMux);
  pollDiagCurrent = r;
  memcpy(pollDiagBaseline, pollDiagTotals, sizeof(pollDiagBaseline));
  pollDiagActive = true;
  portEXIT_CRITICAL(&pollDiagMux);
}

void pollDiagnosticsFinish(bool txOk, int result) {
  portENTER_CRITICAL(&pollDiagMux);
  if (pollDiagActive) {
    PollDiagRecord &r = pollDiagCurrent;
    r.sequence = ++pollDiagSequence;
    r.elapsedMs = millis() - r.startedMs; r.txOk = txOk; r.result = result;
    for (size_t i = 0; i < PD_COUNT; ++i) r.counters[i] = pollDiagTotals[i] - pollDiagBaseline[i];
    r.checksum = pollDiagChecksum(r);
    size_t slot = (r.sequence - 1) % POLL_DIAG_RAM;
    if (pollDiagPersist[slot]) ++pollDiagPendingLost;
    pollDiagRing[slot] = r;
    pollDiagPersist[slot] = flightRecorderEnabled;
    pollDiagActive = false;
  }
  portEXIT_CRITICAL(&pollDiagMux);
}

void pollDiagnosticsFlush() {
  if (!flightRecorderEnabled || !pollDiagStorageMutex) return;
  xSemaphoreTake(pollDiagStorageMutex, portMAX_DELAY);
  // Called by the main-loop flight recorder, never from polling or the radio task.
  uint32_t newest;
  portENTER_CRITICAL(&pollDiagMux); newest = pollDiagSequence; portEXIT_CRITICAL(&pollDiagMux);
  uint32_t oldest = newest > POLL_DIAG_RAM ? newest - POLL_DIAG_RAM + 1 : 1;
  for (uint32_t seq = oldest; seq <= newest; ++seq) {
    size_t slot = (seq - 1) % POLL_DIAG_RAM;
    PollDiagRecord r;
    bool pending;
    portENTER_CRITICAL(&pollDiagMux);
    r = pollDiagRing[slot]; pending = pollDiagPersist[slot] && r.sequence == seq;
    portEXIT_CRITICAL(&pollDiagMux);
    if (!pending) continue;
    if (!pollDiagStore(r)) { pollDiagWriteFailed = true; break; }
    pollDiagSaved = seq;
    portENTER_CRITICAL(&pollDiagMux); pollDiagPersist[slot] = false; portEXIT_CRITICAL(&pollDiagMux);
  }
  xSemaphoreGive(pollDiagStorageMutex);
}

String pollDiagnosticsReport(size_t limit) {
  String out = F("Poll diagnostics v1: completed attempts only; counters cover all traffic in each attempt window.\n"
    "slot is zero-based; result=0 success, 50 receive timeout; attempt=1 initial, 2 retry.\n"
    "local_epoch is local wall time, not UTC. target_rx matches learned PAN/NWK source.\n"
    "reply_mask identifies decoded inverter identities (including ignored replies).\n");
  if (!pollDiagStorageMutex) return out + F("WARNING: poll log mutex unavailable.\n");
  limit = min(limit, POLL_DIAG_SLOTS);
  xSemaphoreTake(pollDiagStorageMutex, portMAX_DELAY);
  uint32_t newest, lost, totals[PD_COUNT];
  portENTER_CRITICAL(&pollDiagMux);
  newest = pollDiagSequence; lost = pollDiagPendingLost;
  memcpy(totals, pollDiagTotals, sizeof(totals));
  portEXIT_CRITICAL(&pollDiagMux);
  char status[160];
  snprintf(status, sizeof(status), "pending_overwritten=%lu; storage_write_failed=%u; last_saved_sequence=%lu; recording=%u\n",
    (unsigned long)lost, pollDiagWriteFailed, (unsigned long)pollDiagSaved, flightRecorderEnabled);
  out += status;
  out += F("since_boot_counters (same order as rx through unfragmented_ack_requested):");
  for (size_t i = 0; i < PD_COUNT; ++i) {
    snprintf(status, sizeof(status), "%s%lu", i ? "," : "", (unsigned long)totals[i]);
    out += status;
  }
  out += '\n';
  out += F("storage,sequence,boot_id,local_epoch,firmware,slot,serial_last4,attempt,start_ms,elapsed_ms,tx_ok,result,pan,source,reply_mask,rx,target_rx,raw_drop,app_drop,parse_reject,fragment_miss,fragment_evict,ack_fail,cca_busy,coexist,tx_fail,delivered,ignored,decrypt_fail,queue_clear,target_asdu,unfragmented_ack_requested\n");
  File f = SPIFFS.open(POLL_DIAG_FILE, "r");
  if (f && f.isDirectory()) f.close();
  uint32_t oldest = newest > limit ? newest - limit + 1 : 1;
  for (uint32_t seq = oldest; seq <= newest; ++seq) {
    PollDiagRecord r = {};
    bool persisted = f && f.seek(((seq - 1) % POLL_DIAG_SLOTS) * sizeof(r)) &&
      f.read(reinterpret_cast<uint8_t *>(&r), sizeof(r)) == sizeof(r) && pollDiagValid(r) && r.sequence == seq;
    if (!persisted) {
      portENTER_CRITICAL(&pollDiagMux); r = pollDiagRing[(seq - 1) % POLL_DIAG_RAM]; portEXIT_CRITICAL(&pollDiagMux);
    }
    if (pollDiagValid(r) && r.sequence == seq) pollDiagAppendRow(out, r, persisted ? "flash" : "ram");
  }
  if (f) f.close();
  xSemaphoreGive(pollDiagStorageMutex);
  return out;
}
