/*
 * Persistent failure flight recorder.
 *
 * A real ESP-IDF panic is captured by the existing coredump partition. This
 * ring complements it for failures which do not panic (notably a dead Wi-Fi
 * connection while the main loop and radio continue running). One compact
 * record is written to SPIFFS each minute and immediately after a Wi-Fi event
 * only when the persisted opt-in setting is enabled. The fixed-size ring never
 * grows with uptime. Wi-Fi reconnect supervision remains active when recording
 * is disabled.
 */
namespace {
constexpr uint32_t FLIGHT_MAGIC = 0x46524331UL;  // "FRC1"
constexpr uint16_t FLIGHT_RECORD_VERSION = 1;
constexpr uint16_t FLIGHT_RECORD_COUNT = 720;   // twelve hours at one/minute
constexpr uint32_t FLIGHT_INTERVAL_MS = 60000UL;
constexpr char FLIGHT_FILE[] = "/flight-recorder.bin";

enum FlightEvent : uint8_t {
  FLIGHT_HEARTBEAT = 0,
  FLIGHT_BOOT = 1,
  FLIGHT_WIFI_LOST = 2,
  FLIGHT_WIFI_RESTORED = 3,
  FLIGHT_WIFI_RECONNECT = 4,
  FLIGHT_LOW_MEMORY = 5,
  FLIGHT_ENABLED = 6,
  FLIGHT_DISABLED = 7
};

struct __attribute__((packed)) FlightRecord {
  uint32_t magic;
  uint16_t version;
  uint16_t size;
  uint32_t sequence;
  uint32_t uptimeMs;
  int64_t epoch;
  uint32_t freeHeap;
  uint32_t minimumFreeHeap;
  uint32_t largestFreeBlock;
  uint16_t loopStackWords;
  uint16_t radioStackWords;
  uint16_t modbusStackWords;
  int8_t wifiStatus;
  int8_t wifiRssi;
  uint8_t wifiReason;
  uint8_t event;
  int16_t dieTempTenthsC;
  uint8_t pollActive;
  uint8_t pollInverter;
  char activity[20];
  uint32_t checksum;
};

uint32_t flightSequence = 0;
uint32_t flightLastWriteMs = 0;
uint32_t flightWifiLostAtMs = 0;
uint32_t flightLastReconnectMs = 0;
bool flightWifiWasConnected = false;
bool flightStationManaged = false;
bool flightStorageReady = false;
volatile uint32_t flightWifiEventCount = 0;
uint32_t flightWifiEventHandled = 0;
char flightActivity[20] = "startup";

uint32_t flightChecksum(const FlightRecord &record) {
  const uint8_t *data = reinterpret_cast<const uint8_t *>(&record);
  uint32_t hash = 2166136261UL;
  for (size_t i = 0; i < offsetof(FlightRecord, checksum); ++i) {
    hash ^= data[i];
    hash *= 16777619UL;
  }
  return hash;
}

bool flightRecordValid(const FlightRecord &record) {
  return record.magic == FLIGHT_MAGIC &&
         record.version == FLIGHT_RECORD_VERSION &&
         record.size == sizeof(FlightRecord) &&
         record.checksum == flightChecksum(record);
}

const char *flightEventName(uint8_t event) {
  switch (event) {
    case FLIGHT_BOOT: return "boot";
    case FLIGHT_WIFI_LOST: return "wifi-lost";
    case FLIGHT_WIFI_RESTORED: return "wifi-restored";
    case FLIGHT_WIFI_RECONNECT: return "wifi-reconnect";
    case FLIGHT_LOW_MEMORY: return "low-memory";
    case FLIGHT_ENABLED: return "enabled";
    case FLIGHT_DISABLED: return "disabled";
    default: return "heartbeat";
  }
}

void flightWrite(uint8_t event) {
  if (!flightRecorderEnabled) return;
  FlightRecord record = {};
  record.magic = FLIGHT_MAGIC;
  record.version = FLIGHT_RECORD_VERSION;
  record.size = sizeof(record);
  record.sequence = flightSequence + 1;
  record.uptimeMs = millis();
  record.epoch = timeRetrieved ? (int64_t)ecuNow() : 0;
  record.freeHeap = ESP.getFreeHeap();
  record.minimumFreeHeap = ESP.getMinFreeHeap();
  record.largestFreeBlock = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
  record.loopStackWords = uxTaskGetStackHighWaterMark(nullptr);
  record.radioStackWords = rawRadioStackHighWaterWords();
  record.modbusStackWords = sunspecTaskHandle ? uxTaskGetStackHighWaterMark(sunspecTaskHandle) : 0;
  record.wifiStatus = (int8_t)WiFi.status();
  record.wifiRssi = WiFi.status() == WL_CONNECTED ? (int8_t)WiFi.RSSI() : 0;
  record.wifiReason = lastWifiDisconnectReason;
  record.event = event;
  record.dieTempTenthsC = (int16_t)lroundf(systemTemperatureCurrentC() * 10.0f);
  record.pollActive = pollingRoundInProgress() ? 1 : 0;
  record.pollInverter = pollSchedulerCurrentInverter();
  strlcpy(record.activity, flightActivity, sizeof(record.activity));
  record.checksum = flightChecksum(record);

  if (!flightStorageReady) return;
  File file = SPIFFS.open(FLIGHT_FILE, "r+");
  if (!file) return;
  const size_t offset = (record.sequence % FLIGHT_RECORD_COUNT) * sizeof(record);
  bool wrote = file.seek(offset) &&
      file.write(reinterpret_cast<const uint8_t *>(&record), sizeof(record)) == sizeof(record);
  file.flush();
  file.close();
  if (!wrote) {
    Serial.println(F("Flight recorder write failed"));
    return;
  }
  flightSequence = record.sequence;
  flightLastWriteMs = millis();
}

bool flightEnsureFile() {
  const size_t expected = (size_t)FLIGHT_RECORD_COUNT * sizeof(FlightRecord);
  File existing = SPIFFS.open(FLIGHT_FILE, FILE_READ);
  if (existing) {
    const bool validSize = existing.size() == expected;
    existing.close();
    if (validSize) return true;
  }

  File file = SPIFFS.open(FLIGHT_FILE, FILE_WRITE);
  if (!file) return false;
  uint8_t zeros[sizeof(FlightRecord)] = {};
  bool ok = true;
  for (uint16_t i = 0; i < FLIGHT_RECORD_COUNT; ++i) {
    if (file.write(zeros, sizeof(zeros)) != sizeof(zeros)) {
      ok = false;
      break;
    }
    if ((i & 31U) == 31U) yield();
  }
  file.flush();
  file.close();
  return ok;
}

void flightScanLatest() {
  File file = SPIFFS.open(FLIGHT_FILE, FILE_READ);
  if (!file) return;
  FlightRecord record;
  while (file.read(reinterpret_cast<uint8_t *>(&record), sizeof(record)) == sizeof(record)) {
    if (flightRecordValid(record) && record.sequence > flightSequence)
      flightSequence = record.sequence;
  }
  file.close();
}
}  // namespace

void flightRecorderActivity(const char *activity) {
  if (activity) strlcpy(flightActivity, activity, sizeof(flightActivity));
}

void flightRecorderWifiEvent() { ++flightWifiEventCount; }

void flightRecorderManageStation(bool enabled) {
  flightStationManaged = enabled;
  flightWifiWasConnected = enabled && WiFi.status() == WL_CONNECTED;
}

void flightRecorderBegin() {
  if (!flightRecorderEnabled) {
    Serial.println(F("Flight recorder disabled (Wi-Fi recovery remains active)"));
    return;
  }
  flightStorageReady = flightEnsureFile();
  if (!flightStorageReady) {
    Serial.println(F("Flight recorder storage allocation failed"));
    return;
  }
  flightScanLatest();
  const uint32_t previousRecords = flightSequence;
  flightWrite(FLIGHT_BOOT);
  Serial.printf("Flight recorder ready: boot reset=%d prior records=%lu\n",
                (int)esp_reset_reason(), (unsigned long)previousRecords);
}

void flightRecorderSetEnabled(bool enabled) {
  if (enabled == flightRecorderEnabled && (!enabled || flightStorageReady)) return;
  if (!enabled) {
    // Capture the setting transition before closing the write gate.
    flightWrite(FLIGHT_DISABLED);
    flightRecorderEnabled = false;
    Serial.println(F("Flight recorder disabled"));
    return;
  }

  flightRecorderEnabled = true;
  flightStorageReady = flightEnsureFile();
  if (!flightStorageReady) {
    flightRecorderEnabled = false;
    Serial.println(F("Flight recorder storage allocation failed"));
    return;
  }
  flightScanLatest();
  flightWrite(FLIGHT_ENABLED);
  Serial.printf("Flight recorder enabled: prior sequence=%lu\n",
                (unsigned long)(flightSequence ? flightSequence - 1 : 0));
}

void flightRecorderLoop() {
  const uint32_t now = millis();
  const bool connected = WiFi.status() == WL_CONNECTED;

  if (flightWifiEventHandled != flightWifiEventCount) {
    flightWifiEventHandled = flightWifiEventCount;
    flightWrite(FLIGHT_WIFI_LOST);
  }

  if (flightStationManaged && connected != flightWifiWasConnected) {
    if (connected) {
      flightWifiLostAtMs = 0;
      flightWrite(FLIGHT_WIFI_RESTORED);
    } else {
      flightWifiLostAtMs = now;
      flightWrite(FLIGHT_WIFI_LOST);
    }
    flightWifiWasConnected = connected;
  }

  // Auto-reconnect is not reliable for every AP/DHCP failure mode. Explicitly
  // retry every 30 seconds while retaining the configured static/DHCP state.
  if (flightStationManaged && !connected) {
    if (flightWifiLostAtMs == 0) flightWifiLostAtMs = now;
    if ((uint32_t)(now - flightLastReconnectMs) >= 30000UL) {
      flightLastReconnectMs = now;
      flightWrite(FLIGHT_WIFI_RECONNECT);
      WiFi.reconnect();
    }
  }

  if ((uint32_t)(now - flightLastWriteMs) >= FLIGHT_INTERVAL_MS) {
    const uint8_t event = ESP.getFreeHeap() < 50000U ? FLIGHT_LOW_MEMORY
                                                     : FLIGHT_HEARTBEAT;
    flightWrite(event);
  }
}

String flightRecorderReport(size_t limit) {
  String output;
  output.reserve(limit * 150 + 256);
  output += F("sequence,uptime_ms,local_time,event,free_heap,min_free_heap,largest_block,loop_stack_words,radio_stack_words,modbus_stack_words,wifi_status,wifi_rssi_dbm,wifi_reason,die_temp_c,poll_active,poll_inverter,activity\n");
  File file = SPIFFS.open(FLIGHT_FILE, FILE_READ);
  if (!file) return output + F("0,0,,no-records\n");
  const uint32_t oldest = flightSequence > limit ? flightSequence - limit + 1 : 1;
  for (uint32_t sequence = oldest; sequence <= flightSequence; ++sequence) {
    FlightRecord record;
    const size_t offset = (sequence % FLIGHT_RECORD_COUNT) * sizeof(record);
    if (!file.seek(offset) || file.read(reinterpret_cast<uint8_t *>(&record), sizeof(record)) != sizeof(record) ||
        !flightRecordValid(record) || record.sequence != sequence) continue;
    char stamp[32] = "";
    if (record.epoch > 0) {
      time_t epoch = (time_t)record.epoch;
      struct tm local = {};
      localtime_r(&epoch, &local);
      strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S", &local);
    }
    char line[320];
    snprintf(line, sizeof(line),
             "%lu,%lu,%s,%s,%lu,%lu,%lu,%u,%u,%u,%d,%d,%u,%.1f,%u,%u,%s\n",
             (unsigned long)record.sequence, (unsigned long)record.uptimeMs, stamp,
             flightEventName(record.event), (unsigned long)record.freeHeap,
             (unsigned long)record.minimumFreeHeap, (unsigned long)record.largestFreeBlock,
             record.loopStackWords, record.radioStackWords, record.modbusStackWords,
             record.wifiStatus, record.wifiRssi, record.wifiReason,
             record.dieTempTenthsC / 10.0f, record.pollActive, record.pollInverter,
             record.activity);
    output += line;
  }
  file.close();
  return output;
}

uint32_t flightRecorderSequence() { return flightSequence; }
bool flightRecorderIsEnabled() { return flightRecorderEnabled; }
