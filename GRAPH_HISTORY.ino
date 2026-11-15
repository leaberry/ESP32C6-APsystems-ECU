// Optional power history. Polling only changes RAM; all automatic filesystem
// mutations are one batch at the UTC day rollover, outside radio transactions.
// UTC storage avoids duplicate/missing hours at daylight-saving transitions.
static SemaphoreHandle_t graphMutex = nullptr;
static bool graphEnabled = false;
static int8_t graphPending = -1;
static graphHistory::Header graphRamHeader = {};
static graphHistory::Cell *graphRam = nullptr;
static bool graphRamHasSamples = false;
static uint32_t graphLastSample[9] = {};
static uint32_t graphRoundUtc = 0;
static uint32_t graphSkippedBusy = 0;
static uint32_t graphAttemptDay = 0;
static const char *graphProblem = "";
struct GraphValidationFailure {
  char file[32];
  const char *reason;
  uint32_t utc, bytes, day, step, expectedCrc, actualCrc, count;
};
static GraphValidationFailure graphValidationFailure = {};
static const char GRAPH_TEMP[] = "/graph.tmp";
static const char GRAPH_PREVIOUS[] = "/graph.previous";
static const char GRAPH_SETTING[] = "/graph.enabled";
static const char *const graphScratchFiles[] = {GRAPH_TEMP, GRAPH_PREVIOUS};

static String graphPath(uint32_t day) { return "/graph-" + String(day % 31) + ".bin"; }

static uint32_t graphIdentity() {
  uint32_t crc = graphHistory::crcUpdate(UINT32_MAX, &inverterCount, sizeof(inverterCount));
  for (uint8_t i = 0; i < inverterCount; ++i)
    crc = graphHistory::crcUpdate(crc, Inv_Prop[i].invID, strlen(Inv_Prop[i].invID));
  return ~crc;
}

static bool graphReadHeader(File &file, graphHistory::Header &h) {
  return file && file.read(reinterpret_cast<uint8_t *>(&h), sizeof(h)) == sizeof(h) &&
      graphHistory::validHeader(h, file.size());
}

static bool graphReadRow(File &file, uint32_t step, graphHistory::Cell *row) {
  uint8_t encoded[27];
  const size_t length = graphHistory::rowBytes(step);
  if (file.read(encoded, length) != length) return false;
  size_t offset = 0;
  for (uint8_t i = 0; i < 9; ++i) {
    memcpy(&row[i].watts, encoded + offset, 2); offset += 2;
    if (step >= 300) {
      row[i].weight = encoded[offset++];
    } else row[i].weight = row[i].watts == graphHistory::missing ? 0 : 1;
    if ((row[i].watts == graphHistory::missing) != (row[i].weight == 0) || row[i].weight > 60)
      return false;
  }
  return true;
}

static bool graphInspect(File &file, graphHistory::Header &h, const char *&reason, uint32_t &actualCrc) {
  reason = ""; actualCrc = 0; h = {};
  if (!file) { reason = "open_failed"; return false; }
  if (!file.seek(0, SeekSet)) { reason = "seek_failed"; return false; }
  if (file.read(reinterpret_cast<uint8_t *>(&h), sizeof(h)) != sizeof(h)) {
    reason = "header_read_failed"; return false;
  }
  if (!graphHistory::validHeader(h, file.size())) { reason = "invalid_header_or_size"; return false; }
  uint32_t crc = graphHistory::crcUpdate(UINT32_MAX, &h, offsetof(graphHistory::Header, crc));
  uint8_t buffer[256];
  size_t remaining = file.size() - sizeof(h);
  while (remaining) {
    const size_t n = file.read(buffer, remaining < sizeof(buffer) ? remaining : sizeof(buffer));
    if (!n) { reason = "data_read_failed"; return false; }
    remaining -= n;
    crc = graphHistory::crcUpdate(crc, buffer, n);
    yield();
  }
  actualCrc = ~crc;
  if (actualCrc != h.crc) { reason = "crc_mismatch"; return false; }
  if (!file.seek(sizeof(h), SeekSet)) { reason = "seek_failed"; return false; }
  return true;
}

static bool graphValidate(File &file, graphHistory::Header &h) {
  const char *reason; uint32_t crc;
  return graphInspect(file, h, reason, crc);
}

static void graphRememberFailure(const String &path, File &file, const graphHistory::Header &h,
                                 const char *reason, uint32_t actualCrc) {
  snprintf(graphValidationFailure.file, sizeof(graphValidationFailure.file), "%s", path.c_str());
  graphValidationFailure.reason = reason;
  graphValidationFailure.utc = static_cast<uint32_t>(ecuUtcNow());
  graphValidationFailure.bytes = file ? file.size() : 0;
  graphValidationFailure.day = h.day; graphValidationFailure.step = h.step;
  graphValidationFailure.expectedCrc = h.crc; graphValidationFailure.actualCrc = actualCrc;
  ++graphValidationFailure.count;
  graphProblem = "A saved history day could not be read. Other days are still available.";
}

static size_t graphDiskBytes() {
  size_t bytes = 0;
  for (uint8_t i = 0; i < 31; ++i) {
    File file = SPIFFS.open(graphPath(i), "r");
    if (file) bytes += file.size();
  }
  for (const char *name : graphScratchFiles) {
    File file = SPIFFS.open(name, "r");
    if (file) bytes += file.size();
  }
  return bytes;
}

static bool graphRemoveFiles() {
  bool ok = true;
  for (uint8_t i = 0; i < 31; ++i) {
    const String path = graphPath(i);
    if (SPIFFS.exists(path)) ok = SPIFFS.remove(path) && ok;
  }
  for (const char *name : graphScratchFiles)
    if (SPIFFS.exists(name)) ok = SPIFFS.remove(name) && ok;
  return ok;
}

// Recover the previous complete file if power failed between the two renames.
static bool graphRecover() {
  if (SPIFFS.exists(GRAPH_PREVIOUS)) {
    File previous = SPIFFS.open(GRAPH_PREVIOUS, "r");
    graphHistory::Header h = {};
    const char *reason; uint32_t crc;
    if (!graphInspect(previous, h, reason, crc)) {
      graphRememberFailure(GRAPH_PREVIOUS, previous, h, reason, crc); return false;
    }
    previous.close();
    const String path = graphPath(h.day);
    File current = SPIFFS.open(path, "r");
    graphHistory::Header active = {};
    const bool valid = graphValidate(current, active) && active.day == h.day;
    current.close();
    if (valid) {
      if (!SPIFFS.remove(GRAPH_PREVIOUS)) return false;
    } else {
      if (SPIFFS.exists(path) && !SPIFFS.remove(path)) return false;
      if (!SPIFFS.rename(GRAPH_PREVIOUS, path)) return false;
    }
  }
  return !SPIFFS.exists(GRAPH_TEMP) || SPIFFS.remove(GRAPH_TEMP);
}

static bool graphRoom(size_t bytes) {
  const size_t total = SPIFFS.totalBytes(), used = SPIFFS.usedBytes();
  // Leave GC headroom plus space for settings/daily totals. Never consume the
  // entire partition, or remove another subsystem's files to make room.
  const size_t reserve = total / 4 + 16 * 1024;
  return graphDiskBytes() + bytes <= graphHistory::budget && used <= total &&
      bytes + reserve <= total - used;
}

static bool graphWriteRow(File &file, uint32_t step, const graphHistory::Cell *row, uint32_t &crc) {
  uint8_t encoded[27];
  size_t n = 0;
  for (uint8_t i = 0; i < 9; ++i) {
    memcpy(encoded + n, &row[i].watts, 2); n += 2;
    if (step >= 300) encoded[n++] = row[i].weight;
  }
  crc = graphHistory::crcUpdate(crc, encoded, n);
  return file.write(encoded, n) == n;
}

// Source is either today's RAM or a validated old day. At most one source row
// and one aggregate are on the stack; the whole archive is never loaded in RAM.
static bool graphSaveDay(graphHistory::Header source, File *input, uint32_t step) {
  if (!input) {
    File existing = SPIFFS.open(graphPath(source.day), "r");
    graphHistory::Header h = {};
    if (graphValidate(existing, h) && h.day >= source.day) {
      // A backward clock adjustment/reboot must not replace an already saved
      // complete day with a new partial RAM day, or overwrite a future slot.
      graphProblem = "Clock overlaps saved history; existing day preserved.";
      return false;
    }
  }
  graphHistory::Header output = source;
  output.step = step; output.count = graphHistory::countFor(step); output.crc = 0;
  if (!graphRoom(graphHistory::fileBytes(step))) {
    graphProblem = "History save skipped: storage reserve reached. Existing history is safe.";
    return false;
  }
  File file = SPIFFS.open(GRAPH_TEMP, "w");
  if (!file) { graphProblem = "Cannot create history file."; return false; }
  bool ok = file.write(reinterpret_cast<const uint8_t *>(&output), sizeof(output)) == sizeof(output);
  uint32_t crc = graphHistory::crcUpdate(UINT32_MAX, &output, offsetof(graphHistory::Header, crc));
  uint32_t next = 0;
  for (uint32_t bucket = 0; ok && bucket < output.count; ++bucket) {
    uint32_t sums[9] = {}, weights[9] = {};
    while (next < source.count && (next * source.step) / step == bucket) {
      graphHistory::Cell row[9];
      if (input) { if (!graphReadRow(*input, source.step, row)) { ok = false; break; } }
      else memcpy(row, graphRam + next * 9, sizeof(row));
      for (uint8_t i = 0; i < 9; ++i) if (row[i].weight) {
        sums[i] += static_cast<uint32_t>(row[i].watts) * row[i].weight;
        weights[i] += row[i].weight;
      }
      ++next;
    }
    graphHistory::Cell result[9];
    for (uint8_t i = 0; i < 9; ++i) result[i] = graphHistory::average(sums[i], weights[i]);
    if (ok) ok = graphWriteRow(file, step, result, crc);
    if (bucket % 32 == 0) yield();
  }
  output.crc = ~crc;
  ok = ok && file.seek(0, SeekSet) &&
      file.write(reinterpret_cast<const uint8_t *>(&output), sizeof(output)) == sizeof(output);
  file.flush(); file.close();
  File check = SPIFFS.open(GRAPH_TEMP, "r");
  graphHistory::Header checked = {};
  ok = ok && graphValidate(check, checked);
  check.close();
  if (input) input->close();
  if (!ok) {
    SPIFFS.remove(GRAPH_TEMP); graphProblem = "History write failed; previous file retained."; return false;
  }
  const String path = graphPath(source.day);
  if (SPIFFS.exists(path) && !SPIFFS.rename(path, GRAPH_PREVIOUS)) {
    SPIFFS.remove(GRAPH_TEMP); graphProblem = "Cannot preserve previous history file."; return false;
  }
  if (!SPIFFS.rename(GRAPH_TEMP, path)) {
    if (SPIFFS.exists(GRAPH_PREVIOUS)) SPIFFS.rename(GRAPH_PREVIOUS, path);
    graphProblem = "Cannot activate history file."; return false;
  }
  if (SPIFFS.exists(GRAPH_PREVIOUS) && !SPIFFS.remove(GRAPH_PREVIOUS)) {
    graphProblem = "History saved; recovery cleanup failed."; return false;
  }
  return true;
}

static void graphStartDay(uint32_t day) {
  const bool identityChanged = graphRamHeader.identity != graphIdentity();
  free(graphRam); graphRam = nullptr;
  graphRamHasSamples = false;
  graphRamHeader = {graphHistory::magic, day, graphIdentity(), graphHistory::stepFor(pollIntervalSeconds), 0, 0};
  graphRamHeader.count = graphHistory::countFor(graphRamHeader.step);
  const size_t bytes = graphRamHeader.count * 9 * sizeof(graphHistory::Cell);
  if (ESP.getFreeHeap() < bytes + 48 * 1024) {
    graphProblem = "Detailed history paused: insufficient RAM."; return;
  }
  graphRam = static_cast<graphHistory::Cell *>(malloc(bytes));
  if (!graphRam) { graphProblem = "Detailed history paused: RAM allocation failed."; return; }
  for (uint32_t i = 0; i < graphRamHeader.count * 9; ++i) graphRam[i] = {graphHistory::missing, 0};
  if (identityChanged) memset(graphLastSample, 0, sizeof(graphLastSample));
}

void graphBegin() {
  graphMutex = xSemaphoreCreateMutex();
  if (!graphMutex) return;
  xSemaphoreTake(graphMutex, portMAX_DELAY);
  File setting = SPIFFS.open(GRAPH_SETTING, "r");
  graphEnabled = setting && setting.size() == 1 && setting.read() == 1;
  setting.close();
  if (graphEnabled) { if (!graphRecover()) graphProblem = "History recovery needs attention; disable to clear."; }
  else if (!graphRemoveFiles()) graphProblem = "Unable to reclaim all optional history files.";
  xSemaphoreGive(graphMutex);
}

void graphLoop() {
  if (!graphMutex) return;
  xSemaphoreTake(graphMutex, portMAX_DELAY);
  if (graphPending != -1) {
    const bool wanted = graphPending != 0; graphPending = -1;
    bool ok = true;
    if (wanted && !graphEnabled) {
      File setting = SPIFFS.open(GRAPH_SETTING, "w");
      const uint8_t value = 1;
      ok = setting && setting.write(&value, 1) == 1;
      setting.close();
    } else if (!wanted && SPIFFS.exists(GRAPH_SETTING)) ok = SPIFFS.remove(GRAPH_SETTING);
    if (ok) {
      graphEnabled = wanted; graphProblem = "";
      if (!wanted) {
        free(graphRam); graphRam = nullptr; graphRamHeader = {}; graphAttemptDay = 0;
        graphValidationFailure = {}; graphSkippedBusy = 0;
        if (!graphRemoveFiles()) graphProblem = "Disabled, but some history files could not be removed.";
      }
    } else graphProblem = "Could not save the history setting.";
  }
  if (graphEnabled && timeRetrieved) {
    const uint32_t utc = static_cast<uint32_t>(ecuUtcNow());
    const uint32_t day = utc / graphHistory::daySeconds;
    if (day >= 18262 && day <= 47481) {
      if (!graphRamHeader.day) graphStartDay(day);
      else if (day > graphRamHeader.day && day != graphAttemptDay) {
        graphAttemptDay = day; // never retry in a tight loop, including on full flash
        graphProblem = "";
        if (graphRecover()) {
          bool storageReady = true;
          // Expire files first, before allocating any temporary replacement.
          for (uint8_t slot = 0; slot < 31; ++slot) {
            const String path = graphPath(slot);
            File input = SPIFFS.open(path, "r");
            if (!input && !SPIFFS.exists(path)) continue;
            graphHistory::Header h = {};
            const char *reason; uint32_t crc;
            if (!graphInspect(input, h, reason, crc)) {
              graphRememberFailure(path, input, h, reason, crc); input.close(); continue;
            }
            if (h.day > day) continue; // clock moved back: never overwrite future days
            const uint32_t age = day - h.day;
            if (age > 30 || h.identity != graphIdentity()) {
              input.close();
              if (!SPIFFS.remove(path)) graphProblem = "Could not expire an old history day.";
              continue;
            }
          }
          for (uint8_t age = 30; age > 0; --age) {
            File input = SPIFFS.open(graphPath(day - age), "r");
            graphHistory::Header h = {};
            if (!graphValidate(input, h) || h.day != day - age || h.identity != graphIdentity()) continue;
            const uint32_t step = graphHistory::retentionStep(age, h.step);
            if (step > h.step && !graphSaveDay(h, &input, step)) { storageReady = false; break; }
          }
          if (storageReady && graphRam && graphRamHasSamples && day - graphRamHeader.day <= 30 &&
              graphRamHeader.identity == graphIdentity())
            graphSaveDay(graphRamHeader, nullptr,
                         graphHistory::retentionStep(day - graphRamHeader.day, graphRamHeader.step));
        } else graphProblem = "History recovery failed; no new data written.";
        graphStartDay(day);
      } else if (graphRamHeader.identity != graphIdentity()) {
        graphStartDay(day); // never attribute another inverter's samples to this slot
      }
    }
  }
  xSemaphoreGive(graphMutex);
}

void graphPollRoundBegin() {
  graphRoundUtc = timeRetrieved ? static_cast<uint32_t>(ecuUtcNow()) : 0;
}

void graphRecord(uint8_t inverter) {
  // Do not wait on HTTP, allocate, write flash, change counters or publish here.
  if (!graphMutex) return;
  if (xSemaphoreTake(graphMutex, 0) != pdTRUE) { ++graphSkippedBusy; return; }
  // Responses within a fleet poll share a bucket even across its boundary.
  // A round crossing UTC midnight is never assigned to a different day's RAM.
  const uint32_t utc = pollingRoundInProgress() && graphRoundUtc ? graphRoundUtc : static_cast<uint32_t>(ecuUtcNow());
  if (graphEnabled && graphRam && timeRetrieved && inverter < inverterCount &&
      graphRamHeader.identity == graphIdentity() && utc / 86400 == graphRamHeader.day &&
      graphHistory::accept(utc, graphLastSample[inverter])) {
    const float power = Inv_Data[inverter].pw_total;
    if (isfinite(power) && power >= 0 && power < 10000.0f) {
      const uint32_t bucket = (utc % 86400) / graphRamHeader.step;
      graphRam[bucket * 9 + inverter] = {static_cast<uint16_t>(lroundf(power)), 1};
      graphRamHasSamples = true;
      graphLastSample[inverter] = utc;
    }
  }
  xSemaphoreGive(graphMutex);
}

void graphRequestSettings(AsyncWebServerRequest *request) {
  if (!graphMutex) { request->send(503, "text/plain", "History is starting."); return; }
  if (!request->hasParam("enabled", true)) { request->send(400, "text/plain", "Missing enabled setting"); return; }
  const String value = request->getParam("enabled", true)->value();
  if (value != "0" && value != "1") { request->send(400, "text/plain", "Invalid enabled setting"); return; }
  xSemaphoreTake(graphMutex, portMAX_DELAY);
  graphPending = value == "1" ? 1 : 0;
  xSemaphoreGive(graphMutex);
  request->send(202, "text/plain", "History setting queued.");
}

void graphSendStatus(AsyncWebServerRequest *request) {
  if (!graphMutex) { request->send(503, "text/plain", "History is starting."); return; }
  xSemaphoreTake(graphMutex, portMAX_DELAY);
  JsonDocument doc;
  doc["enabled"] = graphEnabled; doc["pending"] = graphPending != -1;
  doc["bytes"] = graphDiskBytes();
  doc["budget"] = graphHistory::budget;
  doc["ram_bytes"] = graphRam ? graphRamHeader.count * 9 * sizeof(graphHistory::Cell) : 0;
  doc["poll_seconds"] = pollIntervalSeconds;
  doc["utc"] = static_cast<uint32_t>(ecuUtcNow());
  doc["skipped_busy"] = graphSkippedBusy;
  JsonArray names = doc["inverters"].to<JsonArray>();
  for (uint8_t i = 0; i < inverterCount; ++i) names.add(String("Inverter ") + String(i + 1));
  JsonArray days = doc["days"].to<JsonArray>();
  const uint32_t today = static_cast<uint32_t>(ecuUtcNow()) / 86400;
  if (graphEnabled) for (uint8_t age = 0; age <= 30 && today >= age; ++age) {
    const uint32_t day = today - age;
    graphHistory::Header h = {};
    if (graphRam && graphRamHeader.day == day) h = graphRamHeader;
    else {
      File file = SPIFFS.open(graphPath(day), "r");
      if (!file && !SPIFFS.exists(graphPath(day))) continue;
      if (!graphReadHeader(file, h)) {
        const char *reason; uint32_t crc;
        if (!graphInspect(file, h, reason, crc)) {
          graphRememberFailure(graphPath(day), file, h, reason, crc); continue;
        }
      }
    }
    if (h.day != day || h.identity != graphIdentity()) continue;
    JsonObject entry = days.add<JsonObject>(); entry["day"] = day; entry["step"] = h.step;
  }
  doc["problem"] = graphProblem;
  if (graphValidationFailure.count) {
    JsonObject failure = doc["validation_failure"].to<JsonObject>();
    failure["file"] = graphValidationFailure.file; failure["reason"] = graphValidationFailure.reason;
    failure["utc"] = graphValidationFailure.utc; failure["bytes"] = graphValidationFailure.bytes;
    failure["day"] = graphValidationFailure.day; failure["step"] = graphValidationFailure.step;
    failure["expected_crc"] = graphValidationFailure.expectedCrc;
    failure["actual_crc"] = graphValidationFailure.actualCrc; failure["count"] = graphValidationFailure.count;
  }
  String body; serializeJson(doc, body);
  xSemaphoreGive(graphMutex);
  request->send(200, "application/json", body);
}

void graphSendDay(AsyncWebServerRequest *request) {
  if (!graphMutex) { request->send(503, "text/plain", "History is starting."); return; }
  const uint32_t day = request->hasParam("day") ? request->getParam("day")->value().toInt() : 0;
  const int inverter = request->hasParam("inv") ? request->getParam("inv")->value().toInt() : -1;
  xSemaphoreTake(graphMutex, portMAX_DELAY);
  const uint32_t today = static_cast<uint32_t>(ecuUtcNow()) / 86400;
  if (!graphEnabled || day > today || today - day > 30 || inverter < -1 || inverter >= inverterCount) {
    xSemaphoreGive(graphMutex); request->send(404, "text/plain", "History unavailable"); return;
  }
  const bool ram = graphRam && graphRamHeader.day == day;
  graphHistory::Header h = graphRamHeader;
  File file;
  if (!ram) file = SPIFFS.open(graphPath(day), "r");
  if (!ram) {
    const char *reason; uint32_t crc;
    if (!graphInspect(file, h, reason, crc)) {
      if (file || SPIFFS.exists(graphPath(day))) graphRememberFailure(graphPath(day), file, h, reason, crc);
      xSemaphoreGive(graphMutex); request->send(404, "text/plain", "History day missing or unreadable"); return;
    }
  }
  if (h.day != day || h.identity != graphIdentity()) {
    xSemaphoreGive(graphMutex); request->send(404, "text/plain", "History day missing or damaged"); return;
  }
  // At most 1440 values for one source. No giant all-inverter JSON document.
  AsyncResponseStream *response = request->beginResponseStream("application/json");
  response->printf("{\"day\":%lu,\"step\":%lu,\"values\":[", (unsigned long)day, (unsigned long)h.step);
  for (uint32_t n = 0; n < h.count; ++n) {
    graphHistory::Cell row[9] = {};
    bool valid = true;
    if (ram) memcpy(row, graphRam + n * 9, sizeof(row));
    else valid = graphReadRow(file, h.step, row);
    uint32_t watts = 0;
    const uint8_t begin = inverter < 0 ? 0 : inverter, end = inverter < 0 ? inverterCount : inverter + 1;
    for (uint8_t i = begin; i < end; ++i) {
      if (!row[i].weight) valid = false;
      else watts += row[i].watts;
    }
    if (n) response->print(',');
    if (valid && end > begin) response->print(watts); else response->print("null");
  }
  response->print("]}"); response->addHeader("Cache-Control", "no-store");
  xSemaphoreGive(graphMutex);
  request->send(response);
}

void graphSendFile(AsyncWebServerRequest *request) {
  if (!graphMutex) { request->send(503, "text/plain", "History is starting."); return; }
  const String slot = request->hasParam("slot") ? request->getParam("slot")->value() : String("");
  const char *text = slot.c_str();
  if (!slot.length() || slot.length() > 2 || text[0] < '0' || text[0] > '9' ||
      (slot.length() == 2 && (text[1] < '0' || text[1] > '9')) || slot.toInt() > 30) {
    request->send(400, "text/plain", "Expected history slot 0 through 30"); return;
  }
  // Export the original bytes, including a damaged header or CRC, for diagnosis.
  // The fixed slot map cannot expose settings or other filesystem paths.
  xSemaphoreTake(graphMutex, portMAX_DELAY);
  const String path = graphPath(static_cast<uint32_t>(slot.toInt()));
  if (!SPIFFS.exists(path)) {
    xSemaphoreGive(graphMutex); request->send(404, "text/plain", "History file missing"); return;
  }
  AsyncWebServerResponse *response = request->beginResponse(SPIFFS, path, "application/octet-stream", true);
  if (!response) {
    xSemaphoreGive(graphMutex); request->send(500, "text/plain", "Unable to open history file"); return;
  }
  response->addHeader("Cache-Control", "no-store");
  xSemaphoreGive(graphMutex);
  request->send(response);
}
