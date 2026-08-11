/*
 * Thread-safe local clock for the ESP32-C6 application.
 *
 * TimeLib's now() uses shared 32-bit millis bookkeeping without a lock. The
 * web, Modbus and scheduler tasks can call it concurrently, allowing a race
 * that looks exactly like a 49.7-day millis wrap. Keep the synchronized local
 * epoch against ESP-IDF's 64-bit monotonic timer instead. TimeLib is still set
 * for library compatibility, but application code reads time through here.
 */

static SemaphoreHandle_t ecuTimeMutex = nullptr;
static time_t ecuTimeBaseEpoch = 0;
static int64_t ecuTimeBaseMicros = 0;

void ecuTimeBegin() {
  if (!ecuTimeMutex) ecuTimeMutex = xSemaphoreCreateMutex();
  ecuTimeBaseMicros = esp_timer_get_time();
}

time_t ecuNow() {
  if (!ecuTimeMutex) return 0;
  xSemaphoreTake(ecuTimeMutex, portMAX_DELAY);
  time_t value = ecuTimeBaseEpoch;
  if (value) {
    int64_t elapsed = esp_timer_get_time() - ecuTimeBaseMicros;
    if (elapsed > 0) value += (time_t)(elapsed / 1000000LL);
  }
  xSemaphoreGive(ecuTimeMutex);
  return value;
}

void ecuSetTime(time_t value) {
  if (!ecuTimeMutex) ecuTimeBegin();
  xSemaphoreTake(ecuTimeMutex, portMAX_DELAY);
  ecuTimeBaseEpoch = value;
  ecuTimeBaseMicros = esp_timer_get_time();
  setTime(value); // Retained for sunMoon/TimeLib compatibility.
  xSemaphoreGive(ecuTimeMutex);
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
