#include "PASSIVE_CAPTURE.h"
#include <atomic>
#include <memory>
#include <new>

static portMUX_TYPE passiveMux = portMUX_INITIALIZER_UNLOCKED;
static std::atomic<bool> passiveOwned{APS_PASSIVE_ONLY != 0};
static std::unique_ptr<PassiveCapture> passiveStore;
static bool passivePending = false, passiveStarting = false, passiveStop = false;
static uint16_t passiveReaders = 0;
static const char *passiveState = "idle";
static uint32_t passiveDropStart = 0, passiveDropEnd = 0;
static uint32_t passiveSession = 0;

bool rawRadioBeginPassive();
uint32_t rawRadioDroppedFrames();

bool passiveCaptureOwnsRadio() { return passiveOwned.load(); }

bool passiveCaptureRequest() {
  portENTER_CRITICAL(&passiveMux);
  bool needsStore = !passiveStore;
  portEXIT_CRITICAL(&passiveMux);
  std::unique_ptr<PassiveCapture> fresh;
  if (needsStore) {
    fresh.reset(new (std::nothrow) PassiveCapture);
    if (!fresh) return false;
  }
  portENTER_CRITICAL(&passiveMux);
  bool ok = !passivePending && !passiveStarting && !passiveReaders && (!passiveStore || !passiveStore->running);
  if (ok) {
    if (!passiveStore) passiveStore.swap(fresh);
    passiveStore->begin(0); passiveStore->stop(0, "queued");
    passivePending = true; passiveStop = false;
    passiveSession = esp_random();
    if (!passiveSession) passiveSession = 1;
    passiveState = "queued; waiting for the radio";
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
    // The dedicated passive image never releases ownership, even on reboot.
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

// Each browser request acknowledges only packets delivered by a prior request.
String passiveCaptureStatus() {
  char status[256];
  portENTER_CRITICAL(&passiveMux);
  snprintf(status, sizeof(status), "%s; channel=16; packets=%u; buffer_drops=%lu; queue_drops=%lu; receive-only=%u; passive_image=%u",
           passiveState, unsigned(passiveStore ? passiveStore->count : 0),
           (unsigned long)(passiveStore ? passiveStore->dropped : 0),
           (unsigned long)(passiveDropEnd - passiveDropStart), passiveOwned.load(), APS_PASSIVE_ONLY);
  portEXIT_CRITICAL(&passiveMux);
  return String(status);
}

static size_t passiveCaptureLine(const PassivePacket &p, char *line, size_t cap) {
  size_t n = snprintf(line, cap, "%lu,%u,%d,%u,", (unsigned long)p.at, p.channel, p.rssi, p.lqi);
  for (size_t i = 0; i < p.length && n + 3 < cap; ++i)
    n += snprintf(line + n, cap - n, "%02X", p.bytes[i]);
  if (n + 1 < cap) line[n++] = '\n';
  return n;
}

void passiveCaptureDownload(AsyncWebServerRequest *request) {
  if (!request->hasParam("session") || !request->hasParam("after")) {
    request->send(400, "text/plain", "Use Start 15-minute capture on Diagnostic snapshot."); return;
  }
  String sessionText = request->getParam("session")->value();
  String afterText = request->getParam("after")->value();
  char *sessionEnd = nullptr, *afterEnd = nullptr;
  unsigned long session = strtoul(sessionText.c_str(), &sessionEnd, 10);
  unsigned long after = strtoul(afterText.c_str(), &afterEnd, 10);
  if (!sessionText.length() || !afterText.length() || *sessionEnd || *afterEnd ||
      session > UINT32_MAX || after > UINT32_MAX) {
    request->send(400, "text/plain", "Invalid capture cursor."); return;
  }
  portENTER_CRITICAL(&passiveMux);
  bool ready = passiveStore && session == passiveSession && !passiveReaders &&
               passiveStore->acknowledge(uint32_t(after));
  if (ready) ++passiveReaders;
  portEXIT_CRITICAL(&passiveMux);
  if (!ready) { request->send(409, "text/plain", "Capture changed or cursor expired. Save the partial file and start again."); return; }
  // Copy each frame under the lock, then format outside it. Lost HTTP responses
  // can be retried without discarding or duplicating frames in the browser.
  String body;
  bool allocated = body.reserve(5500);
  char line[320];
  uint32_t next = after;
  if (allocated) {
    for (unsigned i = 0; i < 16; ++i) {
      PassivePacket packet;
      portENTER_CRITICAL(&passiveMux);
      bool found = passiveStore->read(next, packet);
      portEXIT_CRITICAL(&passiveMux);
      if (!found) break;
      size_t bytes = passiveCaptureLine(packet, line, sizeof(line));
      body.concat(line, bytes); ++next;
    }
  }
  char header[640];
  portENTER_CRITICAL(&passiveMux);
  const auto &s = *passiveStore;
  snprintf(header, sizeof(header),
      "# aps-passive-v1 firmware=%s session=%lu channel=16 started_ms=%lu ended_ms=%lu elapsed_ms=%lu packets=%lu next=%lu running=%u pending=%u buffer_drops=%lu queue_drops=%lu reason=%s\n",
      VERSION, (unsigned long)passiveSession, (unsigned long)s.started,
      (unsigned long)s.ended, (unsigned long)(s.running ? millis()-s.started : s.ended-s.started),
      (unsigned long)s.count, (unsigned long)next, s.running, passivePending || passiveStarting,
      (unsigned long)s.dropped, (unsigned long)(passiveDropEnd-passiveDropStart), passiveState);
  --passiveReaders;
  portEXIT_CRITICAL(&passiveMux);
  if (!allocated) { request->send(503, "text/plain", "Not enough memory; retry this batch."); return; }
  auto response = request->beginResponse(200, "text/plain; charset=utf-8", String(header) + body);
  if (!response) { request->send(503, "text/plain", "Not enough memory; retry this batch."); return; }
  response->addHeader("Cache-Control", "no-store"); request->send(response);
}

void passiveCaptureControl(AsyncWebServerRequest *request) {
  if (!request->hasHeader("X-ECU-Diagnostics") || request->getHeader("X-ECU-Diagnostics")->value() != "1") {
    request->send(400, "text/plain", "Use the passive capture controls on Diagnostic snapshot."); return;
  }
  bool start = request->hasParam("start", true);
  if (start) {
    bool ok = passiveCaptureRequest();
    if (ok) request->send(202, "text/plain", String(passiveSession));
    else request->send(409, "text/plain", "Capture busy or insufficient memory. Stop the current capture first.");
  } else {
    portENTER_CRITICAL(&passiveMux); passiveStop = true; portEXIT_CRITICAL(&passiveMux);
    request->send(202, "text/plain", "Stop requested. Refresh status before downloading.");
  }
}
