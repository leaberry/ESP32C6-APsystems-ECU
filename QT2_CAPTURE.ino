#include "QT2_CAPTURE.h"
#include "QT2_PROTOCOL.h"
#include <memory>
#include <atomic>

namespace {
constexpr char QT2_CAPTURE_FILE[] = "/qt2-capture-v1.bin";
constexpr size_t QT2_CAPTURE_QUEUE = 4;
Qt2CaptureRecord qt2Pending[QT2_CAPTURE_QUEUE] = {};
size_t qt2PendingCount = 0;
uint32_t qt2CaptureSequence = 0, qt2CaptureBoot = 0;
std::atomic<uint32_t> qt2CaptureDropped{0};
size_t qt2CaptureCapacity = 0;
SemaphoreHandle_t qt2CaptureMutex = nullptr;
bool qt2CaptureReady = false;
uint32_t qt2CaptureRetryAt = 0;

bool qt2CaptureEnsureFile() {
  if (qt2CaptureReady) return true;
  if ((int32_t)(millis() - qt2CaptureRetryAt) < 0) return false;
  qt2CaptureRetryAt = millis() + 60000;
  const size_t bytes = qt2CaptureCapacity * sizeof(Qt2CaptureRecord);
  // Leave room for settings/history and SPIFFS garbage collection.
  if (SPIFFS.totalBytes() - SPIFFS.usedBytes() < bytes + 96*1024) return false;
  File file = SPIFFS.open(QT2_CAPTURE_FILE, "w");
  if (!file) return false;
  Qt2CaptureRecord zero = {};
  bool ok = true;
  for (size_t i = 0; i < qt2CaptureCapacity; ++i) {
    if (file.write((const uint8_t *)&zero, sizeof(zero)) != sizeof(zero)) {ok=false;break;}
    if ((i & 15) == 15) yield();
  }
  file.close();
  qt2CaptureReady = ok;
  return ok;
}

String qt2CaptureJson(const Qt2CaptureRecord &r) {
  JsonDocument doc;
  doc["schema"] = "qt2-capture/v1";
  doc["sequence"] = r.sequence;
  doc["boot_id"] = r.bootId;
  doc["uptime_ms"] = r.uptimeMs;
  doc["local_epoch"] = r.localEpoch;
  doc["utc_offset_minutes"] = r.utcOffsetMinutes;
  doc["serial"] = r.serial;
  doc["ecu_firmware"] = r.ecuVersion;
  doc["inverter_firmware"] = r.inverterVersion;
  doc["source"] = r.source;
  doc["cluster"] = r.cluster;
  doc["rssi_dbm"] = r.rssi;
  doc["lqi"] = r.lqi;
  doc["connected_mask"] = r.connectedMask;
  doc["event"] = r.event ? "poll_timeout" : "rx";
  char hex[601];
  static const char digits[] = "0123456789ABCDEF";
  for (size_t i=0; i<r.length; ++i) {hex[2*i]=digits[r.data[i]>>4];hex[2*i+1]=digits[r.data[i]&15];}
  hex[2*r.length]=0;
  doc["asdu_hex"] = hex;
  Qt2Telemetry decoded = {};
  bool valid = !r.event && decodeQt2(hex, r.length*2, r.serial, decoded);
  doc["valid_telemetry"] = valid;
  if (valid) {
    doc["inverter_seconds"] = decoded.timestamp;
    doc["frequency_hz"] = decoded.frequency;
    doc["temperature_c"] = decoded.temperature;
    for (int i=0;i<3;++i) doc["phase_v"][i] = decoded.acv[i];
    for (int i=0;i<4;++i) {
      doc["energy_raw"][i] = decoded.energy[i];
      doc["dc_v"][i] = decoded.dcv[i];
      doc["dc_a_provisional"][i] = decoded.dcc[i];
    }
  }
  String line;
  serializeJson(doc, line);
  line += '\n';
  return line;
}
} // namespace

void qt2CaptureBegin() {
  qt2CaptureMutex = xSemaphoreCreateMutex();
  qt2CaptureBoot = esp_random();
  qt2CaptureCapacity = SPIFFS.totalBytes() >= 1024*1024 ? 512 : 128;
  File file = SPIFFS.open(QT2_CAPTURE_FILE, "r");
  if (!file) return;
  if (file.size() == qt2CaptureCapacity*sizeof(Qt2CaptureRecord)) {
    Qt2CaptureRecord record;
    while (file.read((uint8_t *)&record, sizeof(record)) == sizeof(record)) {
      if (qt2CaptureValid(record) && record.sequence > qt2CaptureSequence)
        qt2CaptureSequence = record.sequence;
      yield();
    }
    qt2CaptureReady = true;
  }
  file.close();
}

// Called only from the main application task. Queue first; never write flash
// inside the radio receive/ACK path or a synchronous inverter transaction.
void qt2CaptureObserve(int which, const uint8_t *data, size_t length,
                       uint16_t source, uint16_t cluster, int8_t rssi, uint8_t lqi) {
  if (which < 0 || which >= inverterCount || Inv_Prop[which].invType != 3) return;
  if (qt2PendingCount == QT2_CAPTURE_QUEUE || length > 300 || (!data && length)) {
    ++qt2CaptureDropped;
    return;
  }
  Qt2CaptureRecord &r = qt2Pending[qt2PendingCount++];
  r = {};
  r.magic = 0x51543231;
  r.bootId = qt2CaptureBoot;
  r.uptimeMs = esp_timer_get_time()/1000;
  r.localEpoch = timeRetrieved ? ecuNow() : 0;
  r.utcOffsetMinutes = currentUtcOffsetMinutes;
  r.length = length;
  r.source = source; r.cluster = cluster; r.rssi = rssi; r.lqi = lqi;
  r.event = data ? 0 : 1;
  strlcpy(r.serial, Inv_Prop[which].invSerial, sizeof(r.serial));
  strlcpy(r.ecuVersion, VERSION, sizeof(r.ecuVersion));
  strlcpy(r.inverterVersion, Inv_Data[which].firmwareVersion, sizeof(r.inverterVersion));
  for (int i=0;i<4;++i) if (Inv_Prop[which].conPanels[i]) r.connectedMask |= 1<<i;
  if (length) memcpy(r.data, data, length);
}

void qt2CaptureLoop() {
  if (!qt2PendingCount || !qt2CaptureMutex) return;
  if (xSemaphoreTake(qt2CaptureMutex, 0) != pdTRUE) return;
  if (qt2CaptureEnsureFile()) {
    File file = SPIFFS.open(QT2_CAPTURE_FILE, "r+");
    for (size_t i=0;i<qt2PendingCount;++i) {
      Qt2CaptureRecord &r = qt2Pending[i];
      r.sequence = qt2CaptureSequence + 1;
      r.checksum = qt2CaptureChecksum(r);
      if (file && file.seek((r.sequence % qt2CaptureCapacity)*sizeof(r)) &&
          file.write((const uint8_t *)&r, sizeof(r)) == sizeof(r)) ++qt2CaptureSequence;
      else ++qt2CaptureDropped;
    }
    file.close();
  } else qt2CaptureDropped += qt2PendingCount;
  qt2PendingCount = 0;
  xSemaphoreGive(qt2CaptureMutex);
}

void qt2CaptureDownload(AsyncWebServerRequest *request) {
  if (!qt2CaptureMutex || xSemaphoreTake(qt2CaptureMutex, pdMS_TO_TICKS(20)) != pdTRUE) {
    request->send(503, "text/plain", "Capture storage busy; retry download"); return;
  }
  struct Cursor {uint32_t next, last; String line; size_t offset=0;};
  auto cursor = std::make_shared<Cursor>();
  cursor->last = qt2CaptureSequence;
  cursor->next = cursor->last >= qt2CaptureCapacity ? cursor->last-qt2CaptureCapacity+1 : 1;
  cursor->line = "{\"schema\":\"qt2-capture/v1\",\"event\":\"export\",\"capacity\":" +
      String(qt2CaptureCapacity) + ",\"dropped_since_boot\":" + String(qt2CaptureDropped.load()) +
      ",\"storage_ready\":" + String(qt2CaptureReady ? "true" : "false") + "}\n";
  xSemaphoreGive(qt2CaptureMutex);
  auto response = request->beginChunkedResponse("application/x-ndjson",
    [cursor](uint8_t *buffer, size_t maxLen, size_t) -> size_t {
      if (cursor->offset == cursor->line.length()) {
        if (cursor->next > cursor->last) return 0;
        if (xSemaphoreTake(qt2CaptureMutex, 0) != pdTRUE) return RESPONSE_TRY_AGAIN;
        Qt2CaptureRecord r = {};
        File file = SPIFFS.open(QT2_CAPTURE_FILE, "r");
        bool ok = file && file.seek((cursor->next % qt2CaptureCapacity)*sizeof(r)) &&
            file.read((uint8_t *)&r, sizeof(r)) == sizeof(r);
        file.close();
        xSemaphoreGive(qt2CaptureMutex);
        if (ok && qt2CaptureValid(r) && r.sequence == cursor->next)
          cursor->line = qt2CaptureJson(r);
        else cursor->line = "{\"event\":\"missing_or_overwritten\",\"sequence\":" + String(cursor->next) + "}\n";
        ++cursor->next;
        cursor->offset = 0;
      }
      size_t count = min(maxLen, cursor->line.length()-cursor->offset);
      memcpy(buffer, cursor->line.c_str()+cursor->offset, count);
      cursor->offset += count;
      return count;
    });
  response->addHeader("Content-Disposition", "attachment; filename=qt2-capture.jsonl");
  response->addHeader("Cache-Control", "no-store");
  request->send(response);
}
