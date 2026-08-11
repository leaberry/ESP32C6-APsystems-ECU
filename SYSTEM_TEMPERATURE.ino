/*
 * ESP32-C6 internal die-temperature monitoring.
 *
 * The -10..80 C hardware range has the best documented accuracy and covers
 * the expected operating range of this passively cooled controller. Values
 * describe the silicon die, not ambient air inside the enclosure.
 */
#include <driver/temperature_sensor.h>
#include <esp_timer.h>

namespace {
temperature_sensor_handle_t systemTemperatureHandle = nullptr;
bool systemTemperatureReady = false;
float systemTemperatureCurrent = NAN;
float systemTemperatureLow = NAN;
float systemTemperatureHigh = NAN;
int64_t systemTemperatureLowAtUs = 0;
int64_t systemTemperatureHighAtUs = 0;
uint32_t systemTemperatureLastSampleMs = 0;

void systemTemperatureSample() {
  if (!systemTemperatureReady) return;
  float value = NAN;
  if (temperature_sensor_get_celsius(systemTemperatureHandle, &value) != ESP_OK ||
      !isfinite(value)) return;
  int64_t sampledAtUs = esp_timer_get_time();
  systemTemperatureCurrent = value;
  if (!isfinite(systemTemperatureLow) || value < systemTemperatureLow) {
    systemTemperatureLow = value;
    systemTemperatureLowAtUs = sampledAtUs;
  }
  if (!isfinite(systemTemperatureHigh) || value > systemTemperatureHigh) {
    systemTemperatureHigh = value;
    systemTemperatureHighAtUs = sampledAtUs;
  }
}
}

void systemTemperatureBegin() {
  temperature_sensor_config_t config = TEMPERATURE_SENSOR_CONFIG_DEFAULT(-10, 80);
  esp_err_t result = temperature_sensor_install(&config, &systemTemperatureHandle);
  if (result == ESP_OK) result = temperature_sensor_enable(systemTemperatureHandle);
  systemTemperatureReady = result == ESP_OK;
  if (!systemTemperatureReady) {
    systemTemperatureHandle = nullptr;
    consoleOut("ESP32-C6 temperature sensor unavailable: " +
               String(esp_err_to_name(result)));
    return;
  }
  systemTemperatureSample();
  systemTemperatureLastSampleMs = millis();
}

void systemTemperatureLoop() {
  if (!systemTemperatureReady ||
      (uint32_t)(millis() - systemTemperatureLastSampleMs) < 10000UL) return;
  systemTemperatureLastSampleMs = millis();
  systemTemperatureSample();
}

bool systemTemperatureAvailable() {
  return systemTemperatureReady && isfinite(systemTemperatureCurrent);
}

float systemTemperatureCurrentC() { return systemTemperatureCurrent; }
float systemTemperatureLowC() { return systemTemperatureLow; }
float systemTemperatureHighC() { return systemTemperatureHigh; }

String systemTemperatureTimestamp(int64_t sampledAtUs) {
  if (!sampledAtUs) return F("Unavailable");
  int64_t ageSeconds = (esp_timer_get_time() - sampledAtUs) / 1000000LL;
  if (!timeRetrieved) {
    uint64_t uptimeSeconds = (uint64_t)sampledAtUs / 1000000ULL;
    char value[32];
    snprintf(value, sizeof(value), "uptime %llus",
             (unsigned long long)uptimeSeconds);
    return String(value);
  }
  time_t captured = now() - (time_t)ageSeconds;
  char value[32];
  snprintf(value, sizeof(value), "%04d-%02d-%02d %02d:%02d:%02d",
           year(captured), month(captured), day(captured),
           hour(captured), minute(captured), second(captured));
  return String(value);
}

String systemTemperatureLowTimestamp() {
  return systemTemperatureTimestamp(systemTemperatureLowAtUs);
}

String systemTemperatureHighTimestamp() {
  return systemTemperatureTimestamp(systemTemperatureHighAtUs);
}
