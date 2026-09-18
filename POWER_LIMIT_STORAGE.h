#pragma once
#include <Preferences.h>
#include <cstdio>
#include <cstdint>

// Shared by live commands, startup and settings backup/restore. Do not import
// old "my-data" keys: they have no serial identity and may refer to reused slots.
static constexpr const char *POWER_LIMIT_NAMESPACE = "my_data";

inline bool savePowerLimit(int inverter, int value) {
  if (inverter < 0 || inverter >= 9) return false;
  char key[16];
  snprintf(key, sizeof(key), "maxPwr%d", inverter);
  Preferences limits;
  if (!limits.begin(POWER_LIMIT_NAMESPACE, false)) return false;
  const bool saved = limits.putInt(key, value) == sizeof(int32_t) &&
                     limits.getInt(key, -2) == value;
  limits.end();
  return saved;
}
