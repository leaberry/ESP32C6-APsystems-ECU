#include "PASSIVE_CAPTURE.h"
#include <atomic>
#include <memory>
#include <new>

static portMUX_TYPE passiveMux = portMUX_INITIALIZER_UNLOCKED;
static std::atomic<bool> passiveOwned{false};
static std::unique_ptr<PassiveCapture> passiveStore;
static bool passivePending = false, passiveStarting = false, passiveStop = false;
static uint16_t passiveReaders = 0;
static const char *passiveState = "idle";
static uint32_t passiveDropStart = 0, passiveDropEnd = 0;

bool rawRadioBeginPassive();
uint32_t rawRadioDroppedFrames();

bool passiveCaptureOwnsRadio() { return passiveOwned.load(); }

bool passiveCaptureRequest() {
  // Allocate outside the critical section; a failed request keeps the old capture.
  std::unique_ptr<PassiveCapture> fresh(new (std::nothrow) PassiveCapture);
  if (!fresh) return false;
  portENTER_CRITICAL(&passiveMux);
  bool ok = !passivePending && !passiveStarting && !passiveReaders && (!passiveStore || !passiveStore->running);
  if (ok) {
    passiveStore.swap(fresh); passivePending = true; passiveStop = false;
    passiveState = "queued; wait for recording before starting the old controller";
  }
  portEXIT_CRITICAL(&passiveMux);
  return ok;
}

bool passiveCaptureLoop() {
  portENTER_CRITICAL(&passiveMux);
  bool start = passivePending;
  passivePending = false;
  passiveStarting = start;
  portEXIT_CRITICAL(&passiveMux);
  if (start) {
    // loop() owns radio transactions. Once claimed, only reboot resumes them.
    passiveOwned.store(true);
    bool ok = rawRadioBeginPassive();
    portENTER_CRITICAL(&passiveMux);
    passiveDropStart = passiveDropEnd = rawRadioDroppedFrames();
    if (ok && !passiveStop) {
      passiveStore->begin(millis()); passiveState = "recording";
    } else {
      passiveState = ok ? "stopped before recording" : "radio setup failed; restart required";
      passiveStore->reason = passiveState;
    }
    passiveStarting = false;
    portEXIT_CRITICAL(&passiveMux);
  }
  if (!passiveOwned.load()) return false;
  portENTER_CRITICAL(&passiveMux);
  if (passiveStore && passiveStore->running) {
    if (passiveStop) passiveStore->stop(millis(), "stopped by user");
    passiveStore->tick(millis());
    passiveDropEnd = rawRadioDroppedFrames();
    if (!passiveStore->running) passiveState = passiveStore->reason;
  }
  portEXIT_CRITICAL(&passiveMux);
  return true;
}

bool passiveCaptureObserve(const uint8_t *data, size_t n, uint32_t at,
                           uint8_t channel, int8_t rssi, uint8_t lqi) {
  if (!passiveOwned.load()) return false;
  portENTER_CRITICAL(&passiveMux);
  if (passiveStore && passiveStore->running) {
    passiveStore->observe(data, n, at, channel, rssi, lqi);
    passiveDropEnd = rawRadioDroppedFrames();
    if (!passiveStore->running) passiveState = passiveStore->reason;
  }
  portEXIT_CRITICAL(&passiveMux);
  return true; // Never pass sniffed traffic to pairing, telemetry or software ACKs.
}

// HTTP handlers only queue work or read a stopped capture.
String passiveCaptureStatus() {
  char status[200];
  portENTER_CRITICAL(&passiveMux);
  snprintf(status, sizeof(status), "%s; packets=%u/256; queue_drops=%lu; restart required to resume normal operation=%u",
           passiveState, unsigned(passiveStore ? passiveStore->count : 0),
           (unsigned long)(passiveDropEnd - passiveDropStart), passiveOwned.load());
  portEXIT_CRITICAL(&passiveMux);
  return String(status);
}

// A reader lease prevents a new capture from replacing its backing store.
struct PassiveDownload {
  bool leased = false;
  size_t index = 0, used = 0, offset = 0;
  char line[512];
  ~PassiveDownload() {
    if (leased) {
      portENTER_CRITICAL(&passiveMux); --passiveReaders; portEXIT_CRITICAL(&passiveMux);
    }
  }
};

static size_t passiveCaptureLine(size_t index, char *line, size_t cap) {
  const auto &s = *passiveStore;
  if (index == 0) return snprintf(line, cap,
      "# aps-passive-v1 firmware=%s channel=16 started_ms=%lu ended_ms=%lu packets=%u reason=%s queue_drops=%lu\n"
      "# raw_hex includes PHY length byte and driver-provided FCS bytes; timestamps are receive-callback uptime milliseconds\n"
      "uptime_ms,channel,rssi_dbm,lqi,raw_hex\n", VERSION,
      (unsigned long)s.started, (unsigned long)s.ended, unsigned(s.count), s.reason,
      (unsigned long)(passiveDropEnd - passiveDropStart));
  if (index > s.count) return 0;
  const auto &p = s.packets[index - 1];
  size_t n = snprintf(line, cap, "%lu,%u,%d,%u,", (unsigned long)p.at, p.channel, p.rssi, p.lqi);
  for (size_t i = 0; i < p.length && n + 3 < cap; ++i)
    n += snprintf(line + n, cap - n, "%02X", p.bytes[i]);
  if (n + 1 < cap) line[n++] = '\n';
  return n;
}

void passiveCaptureDownload(AsyncWebServerRequest *request) {
  auto cursor = std::shared_ptr<PassiveDownload>(new (std::nothrow) PassiveDownload);
  if (!cursor) { request->send(503, "text/plain", "Not enough memory."); return; }
  portENTER_CRITICAL(&passiveMux);
  bool ready = passiveStore && !passivePending && !passiveStarting && !passiveStore->running;
  if (ready) { ++passiveReaders; cursor->leased = true; }
  portEXIT_CRITICAL(&passiveMux);
  if (!ready) { request->send(409, "text/plain", "Stop the capture before downloading."); return; }
  auto response = request->beginChunkedResponse("text/csv; charset=utf-8",
    [cursor](uint8_t *buffer, size_t room, size_t) -> size_t {
      size_t written = 0;
      while (written < room) {
        if (cursor->offset == cursor->used) {
          cursor->used = passiveCaptureLine(cursor->index++, cursor->line, sizeof(cursor->line));
          cursor->offset = 0;
          if (!cursor->used) break;
        }
        size_t take = min(room - written, cursor->used - cursor->offset);
        memcpy(buffer + written, cursor->line + cursor->offset, take);
        written += take; cursor->offset += take;
      }
      return written;
    });
  if (!response) { request->send(503, "text/plain", "Not enough memory."); return; }
  response->addHeader("Content-Disposition", "attachment; filename=aps-passive-capture.csv");
  response->addHeader("Cache-Control", "no-store"); request->send(response);
}

void passiveCaptureControl(AsyncWebServerRequest *request) {
  if (!request->hasHeader("X-ECU-Diagnostics") || request->getHeader("X-ECU-Diagnostics")->value() != "1") {
    request->send(400, "text/plain", "Use the passive capture controls on Diagnostic snapshot."); return;
  }
  bool start = request->hasParam("start", true);
  if (start) {
    bool ok = passiveCaptureRequest();
    request->send(ok ? 202 : 409, "text/plain", ok ? "Capture queued. Wait for recording status." : "Capture busy or insufficient memory. Stop recording and finish downloads first.");
  } else {
    portENTER_CRITICAL(&passiveMux); passiveStop = true; portEXIT_CRITICAL(&passiveMux);
    request->send(202, "text/plain", "Stop requested. Refresh status before downloading.");
  }
}
