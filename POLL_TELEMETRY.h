#pragma once
#include <stddef.h>
#include <stdint.h>

// UID | FB FB | length (command + body) | command | body | sum16 | FE FE.
// Check every field before the legacy decoder can update energy baselines.
inline bool pollTelemetryValid(const uint8_t *data, size_t size, int model) {
  if (!data || model < 0 || model > 2 || size < 14 ||
      data[6] != 0xFB || data[7] != 0xFB || size != (size_t)data[8] + 13 ||
      data[9] != (model == 2 ? 0xBB : 0xB1) ||
      data[size - 2] != 0xFE || data[size - 1] != 0xFE) return false;
  // Highest energy field read for four inputs ends at byte 66 (DS3) or 55.
  if (size < (model == 2 ? 70U : 59U)) return false;
  if (model != 2 && !(data[12] || data[13] || data[14])) return false;
  uint16_t sum = 0;
  for (size_t i = 8; i < size - 4; ++i) sum += data[i];
  return sum == (uint16_t)((data[size - 4] << 8) | data[size - 3]);
}
