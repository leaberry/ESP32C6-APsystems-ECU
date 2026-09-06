/*
 * Thread-safe local clock for the ESP32-C6 application.
 *
 * TimeLib's now() uses shared 32-bit millis bookkeeping without a lock. The
 * web, Modbus and scheduler tasks can call it concurrently, allowing a race
 * that looks exactly like a 49.7-day millis wrap. Keep synchronized UTC against
 * ESP-IDF's 64-bit monotonic timer and convert on reads, so DST changes do not
 * depend on NTP. The public clock remains a local epoch for existing consumers.
 */

static SemaphoreHandle_t ecuTimeMutex = nullptr;
static time_t ecuTimeBaseEpoch = 0;
static int64_t ecuTimeBaseMicros = 0;
static bool ecuClockFixedOffset = false;
static int ecuClockFixedMinutes = 0;
static bool ecuClockHasDst = false;
static bool ecuLocalCacheValid = false;
static time_t ecuCachedUtc = 0;
static time_t ecuCachedLocal = 0;
static bool ecuSolarUpdatePending = false;

void ecuTimeBegin() {
  if (ecuTimeMutex) return;
  ecuTimeMutex = xSemaphoreCreateMutex();
  ecuTimeBaseMicros = esp_timer_get_time();
}

// All helpers ending in Locked require ecuTimeMutex, including TZ changes.
static time_t ecuUtcNowLocked() {
  time_t value = ecuTimeBaseEpoch;
  if (value) {
    int64_t elapsed = esp_timer_get_time() - ecuTimeBaseMicros;
    if (elapsed > 0) value += (time_t)(elapsed / 1000000LL);
  }
  return value;
}

static time_t ecuLocalNowLocked() {
  const time_t utc = ecuUtcNowLocked();
  if (!utc) return 0;
  if (ecuLocalCacheValid && ecuCachedUtc == utc) return ecuCachedLocal;

  time_t localEpoch;
  int localDst = 0;
  if (ecuClockFixedOffset) {
    localEpoch = utc + (time_t)ecuClockFixedMinutes * 60;
  } else {
    struct tm local = {};
    if (!localtime_r(&utc, &local)) return 0;
    tmElements_t elements = {};
    elements.Second = local.tm_sec;
    elements.Minute = local.tm_min;
    elements.Hour = local.tm_hour;
    elements.Day = local.tm_mday;
    elements.Month = local.tm_mon + 1;
    elements.Year = CalendarYrToTm(local.tm_year + 1900);
    localEpoch = makeTime(elements);
    localDst = ecuClockHasDst ? (local.tm_isdst > 0 ? 1 : 2) : 0;
  }
  const int16_t offset = (int16_t)((localEpoch - utc) / 60);
  if (!ecuLocalCacheValid || offset != currentUtcOffsetMinutes) {
    // sunMoon uses TimeLib internally. Rebase it at sync/zone/offset changes.
    setTime(localEpoch);
    ecuSolarUpdatePending = true;
  }
  currentUtcOffsetMinutes = offset;
  dst = localDst;
  ecuCachedUtc = utc;
  ecuCachedLocal = localEpoch;
  ecuLocalCacheValid = true;
  return localEpoch;
}

time_t ecuNow() {
  if (!ecuTimeMutex) return 0;
  xSemaphoreTake(ecuTimeMutex, portMAX_DELAY);
  const time_t value = ecuLocalNowLocked();
  xSemaphoreGive(ecuTimeMutex);
  return value;
}

// A zero UTC value changes only the zone, preserving the UTC instant and
// subsecond monotonic base. A null rule selects a fixed offset without DST.
bool ecuConfigureTime(const char *rule, int fixedMinutes, time_t utc) {
  if (!ecuTimeMutex) ecuTimeBegin();
  xSemaphoreTake(ecuTimeMutex, portMAX_DELAY);
  if (rule && setenv("TZ", rule, 1) != 0) {
    xSemaphoreGive(ecuTimeMutex);
    return false;
  }
  if (rule) tzset();
  ecuClockFixedOffset = !rule;
  ecuClockFixedMinutes = fixedMinutes;
  ecuClockHasDst = rule && strchr(rule, ',');
  if (utc) {
    ecuTimeBaseEpoch = utc;
    ecuTimeBaseMicros = esp_timer_get_time();
  }
  ecuLocalCacheValid = false;
  const bool ok = !ecuTimeBaseEpoch || ecuLocalNowLocked() != 0;
  xSemaphoreGive(ecuTimeMutex);
  return ok;
}

void ecuTimeLoop() {
  if (!ecuTimeMutex) return;
  xSemaphoreTake(ecuTimeMutex, portMAX_DELAY);
  ecuLocalNowLocked();
  const bool recalculate = ecuSolarUpdatePending;
  ecuSolarUpdatePending = false;
  xSemaphoreGive(ecuTimeMutex);
  // Solar helpers acquire the same mutex; run them outside the critical section.
  if (recalculate) sun_setrise();
}

static tmElements_t ecuTimeElements(time_t value) {
  tmElements_t elements = {};
  breakTime(value, elements);
  return elements;
}

int ecuSecond(time_t value) { return ecuTimeElements(value).Second; }
int ecuMinute(time_t value) { return ecuTimeElements(value).Minute; }
int ecuHour(time_t value) { return ecuTimeElements(value).Hour; }
int ecuDay(time_t value) { return ecuTimeElements(value).Day; }
int ecuMonth(time_t value) { return ecuTimeElements(value).Month; }
int ecuYear(time_t value) { return tmYearToCalendar(ecuTimeElements(value).Year); }

time_t ecuSunRise(sunMoon &solar, time_t value) {
  if (!ecuTimeMutex) return 0;
  xSemaphoreTake(ecuTimeMutex, portMAX_DELAY);
  time_t result = solar.sunRise(value);
  xSemaphoreGive(ecuTimeMutex);
  return result;
}

time_t ecuSunSet(sunMoon &solar, time_t value) {
  if (!ecuTimeMutex) return 0;
  xSemaphoreTake(ecuTimeMutex, portMAX_DELAY);
  time_t result = solar.sunSet(value);
  xSemaphoreGive(ecuTimeMutex);
  return result;
}
