/*
 * Low-wear energy accounting.
 *
 * Poll deltas update RAM only. One finalized record is appended to SPIFFS per
 * local day, normally at the day-to-night transition. The current day's 24
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
    energyFinalizeDay();
    memset(energyTodayWh, 0, sizeof(energyTodayWh));
    memset(energyHourlyWh, 0, sizeof(energyHourlyWh));
    memset(energyFractionWh, 0, sizeof(energyFractionWh));
    energyDateKey = today;
  }
}

uint64_t energyLifetimeWhFor(uint8_t which) {
  if (which >= YC600_MAX_NUMBER_OF_INVERTERS) return 0;
  return energyPersistedWh[which] + energyTodayWh[which];
}

uint32_t energyTodayWhFor(uint8_t which) {
  return which < YC600_MAX_NUMBER_OF_INVERTERS ? energyTodayWh[which] : 0;
}

uint32_t energyHourWhFor(uint8_t which, uint8_t hourIndex) {
  if (which >= YC600_MAX_NUMBER_OF_INVERTERS || hourIndex >= 24) return 0;
  return energyHourlyWh[which][hourIndex];
}
