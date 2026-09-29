#include "PAIRING_TRACE.h"
#include <memory>
#include <new>

static PairTraceCapture pairTrace;
static portMUX_TYPE pairTraceMux = portMUX_INITIALIZER_UNLOCKED;
static bool pairTraceOwned = false, pairTraceActive = false, pairTraceBusy = false;
static uint16_t pairTraceReaders = 0;
static uint8_t pairTraceCurrent = PAIR_TRACE_STAGES;

void pairTraceBegin(const uint8_t uid[6], bool enabled) {
  portENTER_CRITICAL(&pairTraceMux);
  // A download owns the old trace, never the radio operation. Skip this attempt's
  // optional trace instead of delaying or failing pairing for a slow reader.
  pairTraceOwned = !pairTraceReaders;
  if (pairTraceOwned) {
    memset(static_cast<void *>(&pairTrace), 0, sizeof(pairTrace));
    memcpy(pairTrace.target, uid, 6); pairTrace.started = millis();
  }
  pairTraceBusy = pairTraceOwned;
  pairTraceActive = pairTraceOwned && enabled;
  pairTraceCurrent = PAIR_TRACE_STAGES;
  portEXIT_CRITICAL(&pairTraceMux);
}
void pairTraceStage(const char *name, uint16_t pan, uint16_t source) {
  portENTER_CRITICAL(&pairTraceMux);
  pairTraceCurrent = PAIR_TRACE_STAGES;
  if (pairTraceActive) {
    if (pairTrace.count == PAIR_TRACE_STAGES) ++pairTrace.omittedStages;
    else {
      pairTraceCurrent = pairTrace.count++;
      auto &s = pairTrace.stages[pairTraceCurrent];
      s.name = name; s.pan = pan; s.source = source; s.at = millis();
    }
  }
  portEXIT_CRITICAL(&pairTraceMux);
}
void pairTraceTx(uint16_t cluster, const uint8_t *data, size_t n, uint32_t at, bool ok, int error) {
  portENTER_CRITICAL(&pairTraceMux);
  if (pairTraceActive && pairTraceCurrent < PAIR_TRACE_STAGES) {
    auto &s = pairTrace.stages[pairTraceCurrent];
    if (s.sent < 3) {
      auto &t = s.tx[s.sent++]; t.at = at; t.cluster = cluster; t.ok = ok; t.error = error;
      t.length = n < sizeof(t.data) ? n : sizeof(t.data); memcpy(t.data, data, t.length);
    }
  }
  portEXIT_CRITICAL(&pairTraceMux);
}
void pairTraceObserve(const uint8_t *b, size_t n, uint32_t at, int8_t rssi, uint8_t lqi) {
  portENTER_CRITICAL(&pairTraceMux);
  if (pairTraceActive && pairTraceCurrent < PAIR_TRACE_STAGES && b && n <= 128) {
    auto &s = pairTrace.stages[pairTraceCurrent];
    bool related = n >= 18 && s.source && (pairLe16(b + 8) == s.source || pairLe16(b + 14) == s.source);
    for (size_t i = 1; !related && i + 8 <= n; ++i) related = !memcmp(b + i, pairTrace.target, 6);
    if (related && (int32_t)(at - s.at) >= 0) {
      auto &r = s.raw[s.related++ ? 1 : 0];
      r.at = at; r.length = n; r.rssi = rssi; r.lqi = lqi;
      if (n >= 18) { r.pan = pairLe16(b + 4); r.source = pairLe16(b + 14); }
      PairReply reply; r.reason = checkPairReply(b, n, pairTrace.target, reply);
      memcpy(r.data, b, n);
    }
  }
  portEXIT_CRITICAL(&pairTraceMux);
}
void pairTraceAsdu(uint16_t pan, uint16_t source, uint16_t cluster, const uint8_t *data, size_t n, uint32_t at) {
  portENTER_CRITICAL(&pairTraceMux);
  if (pairTraceActive && pairTraceCurrent < PAIR_TRACE_STAGES && data && n <= 300) {
    uint32_t i = pairTrace.asdus++;
    auto &a = pairTrace.answers[i < 2 ? i : 2 + i % 2];
    a.at = at; a.pan = pan; a.source = source; a.cluster = cluster;
    a.stage = pairTraceCurrent; a.length = n; memcpy(a.data, data, n);
  }
  portEXIT_CRITICAL(&pairTraceMux);
}
void pairTraceDiscovery(uint16_t source, bool conflict) {
  portENTER_CRITICAL(&pairTraceMux);
  if (pairTraceActive && pairTraceCurrent < PAIR_TRACE_STAGES) {
    auto &s = pairTrace.stages[pairTraceCurrent]; s.found = source; s.conflict = conflict;
  }
  portEXIT_CRITICAL(&pairTraceMux);
}
void pairTraceValidation(uint8_t request, uint8_t reason) {
  portENTER_CRITICAL(&pairTraceMux);
  if (pairTraceActive && pairTraceCurrent < PAIR_TRACE_STAGES && request < 2) {
    auto &s = pairTrace.stages[pairTraceCurrent]; s.checked |= 1U << request;
    s.reason[request] = reason;
    if (reason == 0) s.validated |= 1U << request;
  }
  portEXIT_CRITICAL(&pairTraceMux);
}
uint16_t pairTraceOmitted() {
  portENTER_CRITICAL(&pairTraceMux);
  uint32_t omitted = pairTrace.omittedStages;
  for (uint8_t i = 0; i < pairTrace.count; ++i)
    if (pairTrace.stages[i].related > 2) omitted += pairTrace.stages[i].related - 2;
  portEXIT_CRITICAL(&pairTraceMux);
  return omitted > 65535 ? 65535 : omitted;
}
void pairTracePause() {
  portENTER_CRITICAL(&pairTraceMux); pairTraceActive = false; portEXIT_CRITICAL(&pairTraceMux);
}
void pairTraceResult(bool saved, uint8_t mode, bool restored) {
  portENTER_CRITICAL(&pairTraceMux);
  if (pairTraceOwned) {
    pairTrace.saved = saved; pairTrace.mode = mode; pairTrace.restored = restored; pairTrace.ended = millis();
  }
  pairTraceOwned = pairTraceActive = pairTraceBusy = false;
  portEXIT_CRITICAL(&pairTraceMux);
}
bool pairingTraceClear() {
  portENTER_CRITICAL(&pairTraceMux);
  bool ok = !pairTraceBusy && !pairTraceReaders;
  if (ok) memset(static_cast<void *>(&pairTrace), 0, sizeof(pairTrace));
  portEXIT_CRITICAL(&pairTraceMux);
  return ok;
}

// Called only under a completed-capture reader lease. Never allocates a report String.
static size_t pairingTraceLine(size_t index, char *line, size_t cap) {
  if (index == 0) return snprintf(line, cap,
      "Pairing trace %s; target=%02X%02X%02X%02X%02X%02X started=%lu ended=%lu saved=%u mode=%u restored=%u stages=%u omitted_stages=%lu asdus=%lu\n",
      VERSION, pairTrace.target[0], pairTrace.target[1], pairTrace.target[2], pairTrace.target[3], pairTrace.target[4], pairTrace.target[5],
      (unsigned long)pairTrace.started, (unsigned long)pairTrace.ended, pairTrace.saved, pairTrace.mode,
      pairTrace.restored, pairTrace.count, (unsigned long)pairTrace.omittedStages, (unsigned long)pairTrace.asdus);
  if (index == 1) return snprintf(line, cap,
      "RAM only: download before restart or another pairing. Detailed recording follows the flight recorder switch. No stages means detailed recording was off. Times are uptime milliseconds. Modes: 0=auto 1=AES 2=plain. Raw samples: first/latest; ASDUs: first two/latest two. TX error 3 means no MAC ACK. Power reasons: 0=valid 1=missing 2=identity 3=mode 4=decrypt 5=length 6=kind 7=checksum 8=value. Raw reason is the discovery parser result, not a power-packet validation result.\n");
  index -= 2;
  const uint8_t *data = nullptr; size_t length = 0; int used = 0;
  if (index < size_t(pairTrace.count) * 6) {
    const auto &s = pairTrace.stages[index / 6]; size_t row = index % 6;
    if (!row) return snprintf(line, cap,
        "stage=%u name=%s at=%lu pan=%04X source=%04X found=%04X conflict=%u tx=%u related=%lu raw_omitted=%lu checked=%u validated=%u reason=%u/%u\n",
        (unsigned)(index / 6), s.name, (unsigned long)s.at, s.pan, s.source, s.found, s.conflict, s.sent,
        (unsigned long)s.related, (unsigned long)(s.related > 2 ? s.related - 2 : 0), s.checked, s.validated, s.reason[0], s.reason[1]);
    if (row <= 3) {
      const auto &t = s.tx[row - 1];
      used = snprintf(line, cap, " TX at=%lu cluster=%04X ok=%u error=%d bytes=%u data=", (unsigned long)t.at, t.cluster, t.ok, t.error, t.length);
      data = t.data; length = t.length;
    } else {
      const auto &r = s.raw[row - 4];
      used = snprintf(line, cap, " RX at=%lu pan=%04X source=%04X rssi=%d lqi=%u reason=%s bytes=%u raw=",
          (unsigned long)r.at, r.pan, r.source, r.rssi, r.lqi, pairReplyCheckName((PairReplyCheck)r.reason), r.length);
      data = r.data; length = r.length;
    }
  } else {
    index -= size_t(pairTrace.count) * 6;
    if (index >= 4 || index >= pairTrace.asdus) return 0;
    const auto &a = pairTrace.answers[index];
    used = snprintf(line, cap, " ASDU stage=%u at=%lu pan=%04X source=%04X cluster=%04X bytes=%u data=",
        a.stage, (unsigned long)a.at, a.pan, a.source, a.cluster, a.length);
    data = a.data; length = a.length;
  }
  if (used < 0 || (size_t)used >= cap) return 0;
  for (size_t i = 0; i < length && (size_t)used + 3 < cap; ++i) used += snprintf(line + used, cap - used, "%02X", data[i]);
  if ((size_t)used + 1 < cap) line[used++] = '\n';
  line[used] = 0; return used;
}

struct PairTraceDownload {
  bool owns = false;
  size_t index = 0, used = 0, offset = 0;
  char line[900];
  ~PairTraceDownload() {
    if (owns) { portENTER_CRITICAL(&pairTraceMux); --pairTraceReaders; portEXIT_CRITICAL(&pairTraceMux); }
  }
};
void pairingTraceDownload(AsyncWebServerRequest *request) {
  auto *storage = new (std::nothrow) PairTraceDownload;
  if (!storage) { request->send(503, "text/plain", "Not enough memory to download the trace. Try again."); return; }
  auto cursor = std::shared_ptr<PairTraceDownload>(storage);
  portENTER_CRITICAL(&pairTraceMux);
  bool available = !pairTraceBusy && !pairTraceReaders;
  if (available) { ++pairTraceReaders; cursor->owns = true; }
  portEXIT_CRITICAL(&pairTraceMux);
  if (!available) { request->send(409, "text/plain", "Wait for pairing or the current download to finish."); return; }
  auto *response = request->beginChunkedResponse("text/plain; charset=utf-8",
    [cursor](uint8_t *buffer, size_t room, size_t) -> size_t {
      size_t written = 0;
      while (written < room) {
        if (cursor->offset == cursor->used) {
          cursor->used = pairingTraceLine(cursor->index++, cursor->line, sizeof(cursor->line)); cursor->offset = 0;
          if (!cursor->used) break;
        }
        size_t take = min(room - written, cursor->used - cursor->offset);
        memcpy(buffer + written, cursor->line + cursor->offset, take); cursor->offset += take; written += take;
      }
      return written;
    });
  if (!response) { request->send(503, "text/plain", "Not enough memory to download the trace. Try again."); return; }
  response->addHeader("Content-Disposition", "attachment; filename=aps-ecu-pairing-trace.txt");
  response->addHeader("Cache-Control", "no-store"); request->send(response);
}
