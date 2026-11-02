#include "QS1_DIAGNOSTICS.h"
#include <memory>
#include <new>

static portMUX_TYPE qs1Mux = portMUX_INITIALIZER_UNLOCKED;
static std::unique_ptr<Qs1Capture> qs1Capture;
static bool qs1Recording = false, qs1Probe = false, qs1ProbeWindow = false;
static uint32_t qs1ProbeStart = 0, qs1ProbeEnd = 0, qs1DropsStart = 0;
static uint16_t qs1Readers = 0, qs1ListenPan = 0;
static const char *qs1Stage = "idle";

bool qs1CaptureBegin(const char *serial, uint16_t pan) {
  std::unique_ptr<Qs1Capture> next(new (std::nothrow) Qs1Capture);
  if (!next) return false;
  snprintf(next->target, sizeof(next->target), "%s", serial);
  next->operatingPan = pan; next->started = millis();
  portENTER_CRITICAL(&qs1Mux);
  bool ok = !qs1Readers && !qs1Recording;
  if (ok) {
    qs1Capture.swap(next); qs1Recording = true; qs1Stage = "setup";
    qs1ListenPan = pan; qs1ProbeWindow = false;
    qs1DropsStart = rawRadioDroppedFrames();
  }
  portEXIT_CRITICAL(&qs1Mux);
  return ok;
}

void qs1CaptureStage(const char *stage, uint16_t listenPan) {
  portENTER_CRITICAL(&qs1Mux);
  qs1Stage = stage; qs1ListenPan = listenPan;
  if (qs1Recording) {
    Qs1Packet p = {}; p.direction = 'S'; p.stage = stage; p.at = millis();
    p.listenPan = listenPan; p.unfiltered = qs1Probe; p.channel = 16;
    qs1Capture->add(p);
  }
  portEXIT_CRITICAL(&qs1Mux);
}

void qs1CaptureSetProbe(bool enabled) {
  portENTER_CRITICAL(&qs1Mux);
  if (enabled && !qs1Probe) { qs1ProbeStart = millis(); qs1ProbeWindow = false; }
  if (!enabled && qs1Probe) { qs1ProbeEnd = millis(); qs1ProbeWindow = true; }
  qs1Probe = enabled;
  portEXIT_CRITICAL(&qs1Mux);
}

bool qs1CaptureObserve(const uint8_t *b, size_t n, uint32_t at,
                       uint8_t channel, int8_t rssi, uint8_t lqi) {
  portENTER_CRITICAL(&qs1Mux);
  if (qs1ProbeWindow && uint32_t(millis() - qs1ProbeEnd) > 60000) qs1ProbeWindow = false;
  // A delayed queue entry from the unfiltered check must never become telemetry.
  bool observationOnly = qs1Probe || (qs1ProbeWindow && int32_t(at - qs1ProbeStart) >= 0 && int32_t(at - qs1ProbeEnd) <= 0);
  if (qs1Recording && b && n && n <= 128 && int32_t(at - qs1Capture->started) >= 0) {
    Qs1Packet p = {}; p.direction = 'R'; p.stage = qs1Stage; p.at = at;
    p.listenPan = qs1ListenPan; p.unfiltered = observationOnly;
    p.channel = channel; p.rssi = rssi; p.lqi = lqi; p.length = n;
    memcpy(p.data, b, n); qs1Capture->add(p);
  }
  portEXIT_CRITICAL(&qs1Mux);
  return observationOnly;
}

void qs1CaptureTx(const uint8_t *frame, size_t n, uint32_t at, bool ok, int error) {
  portENTER_CRITICAL(&qs1Mux);
  if (qs1Recording && frame && n <= 125) {
    Qs1Packet p = {}; p.direction = 'T'; p.stage = qs1Stage; p.at = at;
    p.listenPan = qs1ListenPan; p.unfiltered = qs1Probe; p.channel = 16;
    p.error = ok ? 0 : (error ? error : -1); p.length = n + 3;
    p.data[0] = n + 2; memcpy(p.data + 1, frame, n); // TX FCS slots stay zero.
    qs1Capture->add(p);
  }
  portEXIT_CRITICAL(&qs1Mux);
}

void qs1CaptureEnd(bool saved, bool restored) {
  portENTER_CRITICAL(&qs1Mux);
  if (qs1Recording) {
    qs1Capture->saved = saved; qs1Capture->restored = restored;
    qs1Capture->ended = millis(); qs1Capture->drops = rawRadioDroppedFrames() - qs1DropsStart;
  }
  qs1Recording = false;
  portEXIT_CRITICAL(&qs1Mux);
}

static size_t qs1CaptureLine(size_t index, char *line, size_t cap) {
  const auto &s = *qs1Capture;
  if (!index) return snprintf(line, cap,
      "# qs1-active-v1 firmware=%s target=%s operating_pan=%04X started=%lu ended=%lu saved=%u restored=%u rx=%lu tx=%lu overwritten=%lu queue_drops=%lu\n"
      "# First 64 and latest 192 events retained; R=RX T=TX S=stage; TX error=0 means local success; RX includes driver FCS/metadata, TX has zero FCS slots. Unfiltered checks cannot verify pairing.\n"
      "uptime_ms,direction,stage,listen_pan,unfiltered,channel,rssi,lqi,error,raw_hex\n",
      VERSION, s.target, s.operatingPan, (unsigned long)s.started, (unsigned long)s.ended,
      s.saved, s.restored, (unsigned long)s.rx, (unsigned long)s.tx,
      (unsigned long)(s.total - s.count()), (unsigned long)s.drops);
  if (index > s.count()) return 0;
  const auto &p = s.at(index - 1);
  int used = snprintf(line, cap, "%lu,%c,%s,%04X,%u,%u,%d,%u,%d,",
      (unsigned long)p.at, p.direction, p.stage, p.listenPan, p.unfiltered,
      p.channel, p.rssi, p.lqi, p.error);
  if (used < 0 || size_t(used) >= cap) return 0;
  for (size_t i = 0; i < p.length && size_t(used) + 3 < cap; ++i)
    used += snprintf(line + used, cap - used, "%02X", p.data[i]);
  if (size_t(used) + 1 < cap) line[used++] = '\n';
  return used;
}

struct Qs1Download {
  bool leased = false;
  size_t index = 0, used = 0, offset = 0;
  char line[800];
  ~Qs1Download() {
    if (leased) { portENTER_CRITICAL(&qs1Mux); --qs1Readers; portEXIT_CRITICAL(&qs1Mux); }
  }
};
void qs1CaptureDownload(AsyncWebServerRequest *request) {
  auto cursor = std::shared_ptr<Qs1Download>(new (std::nothrow) Qs1Download);
  if (!cursor) { request->send(503, "text/plain", "Not enough memory."); return; }
  portENTER_CRITICAL(&qs1Mux);
  bool ready = qs1Capture && !qs1Recording;
  if (ready) { ++qs1Readers; cursor->leased = true; }
  portEXIT_CRITICAL(&qs1Mux);
  if (!ready) { request->send(409, "text/plain", "Run a QS1 pairing attempt and wait for it to finish first."); return; }
  auto response = request->beginChunkedResponse("text/csv; charset=utf-8",
    [cursor](uint8_t *buffer, size_t room, size_t) -> size_t {
      size_t written = 0;
      while (written < room) {
        if (cursor->offset == cursor->used) {
          cursor->used = qs1CaptureLine(cursor->index++, cursor->line, sizeof(cursor->line)); cursor->offset = 0;
          if (!cursor->used) break;
        }
        size_t take = min(room - written, cursor->used - cursor->offset);
        memcpy(buffer + written, cursor->line + cursor->offset, take);
        written += take; cursor->offset += take;
      }
      return written;
    });
  if (!response) { request->send(503, "text/plain", "Not enough memory."); return; }
  response->addHeader("Content-Disposition", "attachment; filename=aps-qs1-pairing-capture.csv");
  response->addHeader("Cache-Control", "no-store"); request->send(response);
}
