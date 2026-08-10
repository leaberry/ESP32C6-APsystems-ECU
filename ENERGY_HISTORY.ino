/*
 * Low-wear energy accounting.
 *
 * Poll deltas update RAM only. One finalized record is appended to SPIFFS per
 * local day, at the local calendar rollover. The current day's 24
 * hourly buckets deliberately remain volatile to avoid frequent flash writes.
 */

static const char ENERGY_HISTORY_FILE[] = "/energy-days.bin";
static const uint32_t ENERGY_RECORD_MAGIC = 0x41505345UL; // "APSE"

struct __attribute__((packed)) EnergyDayRecord {
  uint32_t magic;
  uint32_t dateKey; // YYYYMMDD
  uint32_t wh[YC600_MAX_NUMBER_OF_INVERTERS];
  uint32_t crc;
};

static uint64_t energyPersistedWh[YC600_MAX_NUMBER_OF_INVERTERS] = {};
static uint32_t energyTodayWh[YC600_MAX_NUMBER_OF_INVERTERS] = {};
static uint32_t energyHourlyWh[YC600_MAX_NUMBER_OF_INVERTERS][24] = {};
static float energyFractionWh[YC600_MAX_NUMBER_OF_INVERTERS] = {};
static uint32_t energyDateKey = 0;
static uint32_t energyLastPersistedDateKey = 0;

static uint32_t energyCrc32(const uint8_t *data, size_t length) {
  uint32_t crc = 0xFFFFFFFFUL;
  while (length--) {
    crc ^= *data++;
    for (uint8_t bit = 0; bit < 8; ++bit)
      crc = (crc >> 1) ^ (0xEDB88320UL & (uint32_t)-(int32_t)(crc & 1));
  }
  return ~crc;
}

static uint32_t energyLocalDateKey() {
  if (!timeRetrieved || year() < 2020) return 0;
  return (uint32_t)year() * 10000UL + (uint32_t)month() * 100UL + day();
}

static bool energyValidRecord(const EnergyDayRecord &record) {
  if (record.magic != ENERGY_RECORD_MAGIC || record.dateKey < 20200101UL ||
      record.dateKey > 20991231UL) return false;
  return record.crc == energyCrc32((const uint8_t *)&record,
                                   offsetof(EnergyDayRecord, crc));
}

void energyHistoryBegin() {
  memset(energyPersistedWh, 0, sizeof(energyPersistedWh));
  File history = SPIFFS.open(ENERGY_HISTORY_FILE, "r");
  if (history) {
    EnergyDayRecord record = {};
    while (history.read((uint8_t *)&record, sizeof(record)) == sizeof(record)) {
      if (!energyValidRecord(record)) {
        consoleOut(F("energy history: ignoring invalid/truncated record"));
        break;
      }
      for (uint8_t i = 0; i < YC600_MAX_NUMBER_OF_INVERTERS; ++i)
        energyPersistedWh[i] += record.wh[i];
      energyLastPersistedDateKey = record.dateKey;
    }
    history.close();
  }
  energyDateKey = energyLocalDateKey();
}

void energyRecordDelta(uint8_t which, float deltaWh) {
  if (which >= YC600_MAX_NUMBER_OF_INVERTERS || !isfinite(deltaWh) || deltaWh <= 0)
    return;

  // Reject an impossible single-sample jump. This protects lifetime history
  // from corrupt telemetry while remaining well above any supported unit.
  if (deltaWh > 10000.0f) {
    consoleOut("energy history: rejected implausible delta " + String(deltaWh));
    return;
  }

  uint32_t today = energyLocalDateKey();
  if (today && !energyDateKey) energyDateKey = today;
  energyFractionWh[which] += deltaWh;
  uint32_t wholeWh = (uint32_t)floorf(energyFractionWh[which]);
  if (!wholeWh) return;
  energyFractionWh[which] -= wholeWh;
  energyTodayWh[which] += wholeWh;
  if (timeRetrieved && hour() < 24) energyHourlyWh[which][hour()] += wholeWh;
}

bool energyFinalizeDay() {
  if (!energyDateKey || energyLastPersistedDateKey == energyDateKey) return false;
  bool hasEnergy = false;
  for (uint8_t i = 0; i < YC600_MAX_NUMBER_OF_INVERTERS; ++i)
    hasEnergy = hasEnergy || energyTodayWh[i] > 0;
  if (!hasEnergy) return false;

  EnergyDayRecord record = {};
  record.magic = ENERGY_RECORD_MAGIC;
  record.dateKey = energyDateKey;
  memcpy(record.wh, energyTodayWh, sizeof(record.wh));
  record.crc = energyCrc32((const uint8_t *)&record, offsetof(EnergyDayRecord, crc));

  File history = SPIFFS.open(ENERGY_HISTORY_FILE, FILE_APPEND);
  if (!history) {
    consoleOut(F("energy history: cannot open daily journal"));
    return false;
  }
  bool written = history.write((const uint8_t *)&record, sizeof(record)) == sizeof(record);
  history.close();
  if (!written) {
    consoleOut(F("energy history: daily journal write failed"));
    return false;
  }
  for (uint8_t i = 0; i < YC600_MAX_NUMBER_OF_INVERTERS; ++i)
    energyPersistedWh[i] += energyTodayWh[i];
  energyLastPersistedDateKey = energyDateKey;
  consoleOut("energy history: stored day " + String(energyDateKey));
  return true;
}

void energyHistoryLoop() {
  uint32_t today = energyLocalDateKey();
  if (!today) return;
  if (!energyDateKey) {
    energyDateKey = today;
    return;
  }
  if (today != energyDateKey) {
    bool hasEnergy = false;
    for (uint8_t i = 0; i < YC600_MAX_NUMBER_OF_INVERTERS; ++i)
      hasEnergy = hasEnergy || energyTodayWh[i] > 0;
    // Never discard a day's counters when the flash journal cannot be written.
    // Leave the old date active and retry on the next loop iteration.
    if (hasEnergy && !energyFinalizeDay()) return;
    memset(energyTodayWh, 0, sizeof(energyTodayWh));
    memset(energyHourlyWh, 0, sizeof(energyHourlyWh));
    memset(energyFractionWh, 0, sizeof(energyFractionWh));
    energyDateKey = today;
  }
}

uint64_t energyLifetimeWhFor(uint8_t which) {
  if (which >= YC600_MAX_NUMBER_OF_INVERTERS) return 0;
  // If persistence succeeds immediately before a rollover, avoid counting the
  // still-resident RAM value a second time.
  return energyPersistedWh[which] +
      (energyLastPersistedDateKey == energyDateKey ? 0 : energyTodayWh[which]);
}

uint32_t energyTodayWhFor(uint8_t which) {
  return which < YC600_MAX_NUMBER_OF_INVERTERS ? energyTodayWh[which] : 0;
}

uint32_t energyHourWhFor(uint8_t which, uint8_t hourIndex) {
  if (which >= YC600_MAX_NUMBER_OF_INVERTERS || hourIndex >= 24) return 0;
  return energyHourlyWh[which][hourIndex];
}

void energySendHourlyJson(AsyncWebServerRequest *request, int inverter) {
  if (inverter >= inverterCount || inverter < -1) {
    request->send(404, "text/plain", "unknown inverter");
    return;
  }
  AsyncResponseStream *response = request->beginResponseStream("application/json");
  response->print(F("{\"inverter\":"));
  response->print(inverter);
  response->print(F(",\"date\":"));
  response->print(energyDateKey);
  response->print(F(",\"today_wh\":"));
  uint64_t todayTotal = 0;
  if (inverter < 0) {
    for (uint8_t i = 0; i < inverterCount; ++i) todayTotal += energyTodayWhFor(i);
  } else {
    todayTotal = energyTodayWhFor(inverter);
  }
  response->print((uint32_t)min<uint64_t>(todayTotal, UINT32_MAX));
  response->print(F(",\"hourly_wh\":["));
  for (uint8_t hourIndex = 0; hourIndex < 24; ++hourIndex) {
    if (hourIndex) response->print(',');
    uint64_t value = 0;
    if (inverter < 0) {
      for (uint8_t i = 0; i < inverterCount; ++i)
        value += energyHourWhFor(i, hourIndex);
    } else {
      value = energyHourWhFor(inverter, hourIndex);
    }
    response->print((uint32_t)min<uint64_t>(value, UINT32_MAX));
  }
  response->print(F("]}"));
  response->addHeader("Cache-Control", "no-store");
  request->send(response);
}

void energySendDailyHistoryJson(AsyncWebServerRequest *request, uint16_t limit) {
  limit = constrain(limit, 1, 365);
  AsyncResponseStream *response = request->beginResponseStream("application/json");
  response->print(F("{\"inverter_count\":"));
  response->print(inverterCount);
  response->print(F(",\"days\":["));

  File history = SPIFFS.open(ENERGY_HISTORY_FILE, "r");
  bool first = true;
  if (history) {
    size_t recordCount = history.size() / sizeof(EnergyDayRecord);
    size_t firstRecord = recordCount > limit ? recordCount - limit : 0;
    history.seek(firstRecord * sizeof(EnergyDayRecord), SeekSet);
    EnergyDayRecord record = {};
    while (history.read((uint8_t *)&record, sizeof(record)) == sizeof(record)) {
      if (!energyValidRecord(record)) break;
      if (!first) response->print(',');
      first = false;
      response->print(F("{\"date\":"));
      response->print(record.dateKey);
      response->print(F(",\"wh\":["));
      uint64_t total = 0;
      for (uint8_t i = 0; i < inverterCount; ++i) {
        if (i) response->print(',');
        response->print(record.wh[i]);
        total += record.wh[i];
      }
      response->print(F("],\"total_wh\":"));
      response->print((uint32_t)min<uint64_t>(total, UINT32_MAX));
      response->print('}');
    }
    history.close();
  }

  response->print(F("],\"today\":{\"date\":"));
  response->print(energyDateKey);
  response->print(F(",\"wh\":["));
  uint64_t todayTotal = 0;
  for (uint8_t i = 0; i < inverterCount; ++i) {
    if (i) response->print(',');
    uint32_t wh = energyTodayWhFor(i);
    response->print(wh);
    todayTotal += wh;
  }
  response->print(F("],\"total_wh\":"));
  response->print((uint32_t)min<uint64_t>(todayTotal, UINT32_MAX));
  response->print(F("}}"));
  response->addHeader("Cache-Control", "no-store");
  request->send(response);
}

void energySendHistoryCsv(AsyncWebServerRequest *request) {
  File history = SPIFFS.open(ENERGY_HISTORY_FILE, "r");
  const uint8_t count = inverterCount;
  AsyncWebServerResponse *response = request->beginChunkedResponse(
      "text/csv",
      [history, pending = String(), stage = (uint8_t)0, count]
      (uint8_t *buffer, size_t maxLength, size_t) mutable -> size_t {
        size_t written = 0;
        while (written < maxLength) {
          if (pending.isEmpty()) {
            if (stage == 0) {
              pending = F("date,total_wh");
              for (uint8_t i = 0; i < count; ++i) {
                pending += F(",inverter_"); pending += String(i + 1);
                pending += F("_wh");
              }
              pending += '\n';
              stage = 1;
            } else if (stage == 1) {
              EnergyDayRecord record = {};
              if (history && history.read((uint8_t *)&record, sizeof(record)) == sizeof(record) &&
                  energyValidRecord(record)) {
                uint64_t total = 0;
                for (uint8_t i = 0; i < count; ++i) total += record.wh[i];
                pending = String(record.dateKey) + ',' +
                    String((uint32_t)(total > UINT32_MAX ? UINT32_MAX : total));
                for (uint8_t i = 0; i < count; ++i)
                  pending += ',' + String(record.wh[i]);
                pending += '\n';
              } else {
                if (history) history.close();
                stage = 2;
                continue;
              }
            } else if (stage == 2) {
              uint64_t total = 0;
              for (uint8_t i = 0; i < count; ++i) total += energyTodayWhFor(i);
              pending = String(energyDateKey) + ',' +
                  String((uint32_t)(total > UINT32_MAX ? UINT32_MAX : total));
              for (uint8_t i = 0; i < count; ++i)
                pending += ',' + String(energyTodayWhFor(i));
              pending += '\n';
              stage = 3;
            } else {
              return written;
            }
          }
          size_t available = min(maxLength - written, pending.length());
          memcpy(buffer + written, pending.c_str(), available);
          written += available;
          pending.remove(0, available);
        }
        return written;
      });
  response->addHeader("Content-Disposition", "attachment; filename=apsystems-energy-history.csv");
  request->send(response);
}
