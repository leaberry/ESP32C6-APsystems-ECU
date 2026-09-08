// Persistent bounded attempt snapshots. Only the main loop writes; downloads
// share a mutex. No radio-worker flash writes or generic log-string ingestion.
namespace {
constexpr size_t PAIR_AUDIT_SLOTS = 24;
constexpr char PAIR_AUDIT_FILE[] = "/pairing-audit.bin";
SemaphoreHandle_t pairAuditMutex = nullptr;
PairAuditRecord pairAuditCurrent = {};
uint32_t pairAuditSequence = 0;
bool pairAuditWriting = false;
bool pairAuditWriteFailed = false;

bool pairAuditStore() {
  pairAuditCurrent.checksum = pairAuditChecksum(pairAuditCurrent);
  // Missing SPIFFS read/update paths can return a truthy directory handle.
  // exists() excludes directories; choose creation before opening the file.
  File f = SPIFFS.open(PAIR_AUDIT_FILE,
                       SPIFFS.exists(PAIR_AUDIT_FILE) ? "r+" : "w+");
  if (!f || f.isDirectory()) {
    if (f) f.close();
    pairAuditWriteFailed = true;
    return false;
  }
  // Extend a new/short file without discarding any intact older snapshots.
  const size_t expected = PAIR_AUDIT_SLOTS * sizeof(PairAuditRecord);
  if (f && f.size() < expected && f.seek(f.size())) {
    uint8_t zeros[64] = {};
    size_t remaining = expected - f.size();
    while (remaining) {
      size_t count = min(remaining, sizeof(zeros));
      if (f.write(zeros, count) != count) break;
      remaining -= count;
    }
  }
  bool ok = f && f.seek(((pairAuditCurrent.sequence - 1) % PAIR_AUDIT_SLOTS) *
                        sizeof(PairAuditRecord)) &&
      f.write(reinterpret_cast<const uint8_t *>(&pairAuditCurrent),
              sizeof(pairAuditCurrent)) == sizeof(pairAuditCurrent);
  if (f) { f.flush(); f.close(); }
  pairAuditWriteFailed |= !ok;
  return ok;
}
const char *pairAuditStageName(uint8_t stage) {
  switch (stage) {
    case PA_START: return "start";
    case PA_RADIO: return "radio";
    case PA_COMMAND: return "handshake-tx";
    case PA_VERIFY_QUERY: return "verify-tx";
    case PA_RX_SUMMARY: return "receive-summary";
    case PA_RESTORE: return "radio-restore";
    case PA_STORAGE: return "save-pairing";
    case PA_DONE: return "complete";
    case PA_SAVED_NETWORK: return "saved-network-query";
    default: return "unknown";
  }
}
} // namespace

void pairAuditBeginStorage() {
  pairAuditMutex = xSemaphoreCreateMutex();
  if (!pairAuditMutex) return;
  File f = SPIFFS.open(PAIR_AUDIT_FILE, "r");
  if (!f || f.isDirectory()) { if (f) f.close(); return; }
  for (size_t slot = 0; slot < PAIR_AUDIT_SLOTS; ++slot) {
    PairAuditRecord r = {};
    if (f.read(reinterpret_cast<uint8_t *>(&r), sizeof(r)) != sizeof(r)) break;
    if (pairAuditValid(r) && r.sequence > pairAuditSequence) pairAuditSequence = r.sequence;
  }
  f.close();
}

void pairAuditBegin(int which, const char *serial) {
  if (!pairAuditMutex) return;
  xSemaphoreTake(pairAuditMutex, portMAX_DELAY);
  memset(&pairAuditCurrent, 0, sizeof(pairAuditCurrent));
  pairAuditCurrent.magic = 0x50414931;
  pairAuditCurrent.sequence = ++pairAuditSequence;
  pairAuditCurrent.startedMs = millis();
  pairAuditCurrent.localEpoch = ecuNow();
  pairAuditCurrent.inverter = which;
  strlcpy(pairAuditCurrent.firmware, VERSION, sizeof(pairAuditCurrent.firmware));
  strlcpy(pairAuditCurrent.build, __DATE__ " " __TIME__, sizeof(pairAuditCurrent.build));
  if (serial && strlen(serial) == 12)
    for (uint8_t i = 0; i < 4; ++i)
      pairAuditCurrent.serialTail[i] = serial[8+i] >= '0' && serial[8+i] <= '9' ? serial[8+i] : '?';
  pairAuditCurrent.count = 1;
  pairAuditCurrent.events[0].stage = PA_START;
  pairAuditCurrent.events[0].result = PA_PENDING;
  pairAuditWriting = true;
  pairAuditWriteFailed = false;
  pairAuditStore(); // A reset before completion leaves an explicitly pending attempt.
  xSemaphoreGive(pairAuditMutex);
}

void pairAuditEvent(PairAuditStage stage, bool ok, uint8_t step, uint16_t pan,
                    uint16_t source, uint16_t replies, uint16_t traceDropped,
                    int8_t rssi, uint8_t lqi, bool conflict) {
  if (!pairAuditMutex) return;
  xSemaphoreTake(pairAuditMutex, portMAX_DELAY);
  if (pairAuditWriting) {
    uint8_t index = pairAuditCurrent.count;
    if (index == 20) { index = 19; ++pairAuditCurrent.droppedEvents; }
    else ++pairAuditCurrent.count;
    PairAuditEvent &e = pairAuditCurrent.events[index];
    e.elapsedMs = millis() - pairAuditCurrent.startedMs;
    e.stage = stage; e.result = ok ? PA_OK : PA_FAILED; e.step = step;
    e.pan = pan; e.source = source; e.replies = replies; e.traceDropped = traceDropped;
    e.rssi = rssi; e.lqi = lqi; e.conflict = conflict;
    if (stage == PA_DONE) pairAuditCurrent.result = e.result;
    pairAuditStore();
    if (stage == PA_DONE) pairAuditWriting = false;
  }
  xSemaphoreGive(pairAuditMutex);
}

void pairAuditStep(PairAuditStage stage, bool ok, uint8_t step, uint16_t pan) {
  pairAuditEvent(stage, ok, step, pan, 0, 0, 0, 0, 0, false);
}

String pairingAuditReport(size_t limit) {
  String out = F("Persistent pairing metadata: newest 24 attempts; no raw payloads or keys.\n"
      "result: 0=pending/interrupted, 1=success, 2=failed. local_epoch is local wall time, not UTC.\n");
  if (!pairAuditMutex) return out + F("Pairing log unavailable (mutex allocation failed).\n");
  xSemaphoreTake(pairAuditMutex, portMAX_DELAY);
  if (pairAuditWriteFailed) out += F("WARNING: a pairing log write failed this boot.\n");
  out += F("attempt,firmware,build,slot,serial_last4,local_epoch,start_uptime_ms,attempt_result,dropped_events,elapsed_ms,stage,result,step,pan,source,matched_replies,trace_dropped,rssi,lqi,conflict\n");
  File f = SPIFFS.open(PAIR_AUDIT_FILE, "r");
  if (f && f.isDirectory()) f.close();
  limit = min(limit, PAIR_AUDIT_SLOTS);
  uint32_t oldest = pairAuditSequence > limit ? pairAuditSequence - limit + 1 : 1;
  for (uint32_t seq = oldest; f && seq <= pairAuditSequence; ++seq) {
    PairAuditRecord r = {};
    if (!f.seek(((seq-1) % PAIR_AUDIT_SLOTS) * sizeof(r)) ||
        f.read(reinterpret_cast<uint8_t *>(&r), sizeof(r)) != sizeof(r) ||
        !pairAuditValid(r) || r.sequence != seq) continue;
    for (uint8_t i = 0; i < r.count; ++i) {
      const PairAuditEvent &e = r.events[i];
      char line[260];
      snprintf(line, sizeof(line), "%lu,%.31s,%.23s,%u,%.4s,%lld,%lu,%u,%u,%lu,%s,%u,%u,%04X,%04X,%u,%u,%d,%u,%u\n",
          (unsigned long)r.sequence, r.firmware, r.build, r.inverter, r.serialTail,
          (long long)r.localEpoch, (unsigned long)r.startedMs, r.result, r.droppedEvents,
          (unsigned long)e.elapsedMs, pairAuditStageName(e.stage), e.result, e.step,
          e.pan, e.source, e.replies, e.traceDropped, e.rssi, e.lqi, e.conflict);
      out += line;
    }
  }
  if (f) f.close();
  xSemaphoreGive(pairAuditMutex);
  return out;
}
