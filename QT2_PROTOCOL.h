#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

// Layout and scales from TobiasTTM's captures and DC injection tests.
// Offsets include the six-byte serial. See QT2.md for evidence and limits.
static constexpr float QT2_ENERGY_COUNTS_PER_WH = 32000.0f;
static inline bool qt2TelemetryVariantSupported(uint8_t value) {
  return value == 0x29 || value == 0x30;
}
struct Qt2Telemetry {
  float acv[3]; // Reported scale; reference and phase order remain unverified.
  float dcv[4];
  float dcc[4];
  float frequency;
  float temperature;
  uint16_t timestamp;
  uint32_t energy[4];
};

static inline int qt2Nibble(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

static inline bool decodeQt2(const char *hex, size_t chars,
                             const char *serial, Qt2Telemetry &output) {
  // 6 serial + 99 L2 bytes. The old ZNP source/RSSI/FCS trailer is NOT L2.
  if (!hex || !serial || chars != 210 || strlen(serial) != 12) return false;
  uint8_t bytes[105];
  for (size_t i = 0; i < sizeof(bytes); ++i) {
    int hi = qt2Nibble(hex[2*i]), lo = qt2Nibble(hex[2*i+1]);
    if (hi < 0 || lo < 0) return false;
    bytes[i] = (uint8_t)((hi << 4) | lo);
    if (i < 6 && (hi != qt2Nibble(serial[2*i]) ||
                  lo != qt2Nibble(serial[2*i+1]))) return false;
  }
  // Captured telemetry uses 0x29 or 0x30 here; both share the decoded layout.
  // A DD/DE configuration reply has the same length as telemetry.
  if (bytes[6] != 0xFB || bytes[7] != 0xFB || bytes[8] != 0x5C ||
      bytes[9] != 0xBB || bytes[10] != 0xBB ||
      !qt2TelemetryVariantSupported(bytes[11]) ||
      bytes[103] != 0xFE || bytes[104] != 0xFE) return false;
  auto u16 = [&](size_t off) -> uint16_t {
    return ((uint16_t)bytes[off] << 8) | bytes[off+1];
  };
  uint16_t sum = 0;
  for (size_t i = 8; i < 101; ++i) sum += bytes[i];
  if (sum != u16(101)) return false;

  Qt2Telemetry next = {};
  for (size_t phase = 0; phase < 3; ++phase)
    next.acv[phase] = u16(40 + 2*phase) / 10.0f;
  next.frequency = u16(50) / 100.0f;
  next.temperature = u16(48) / 100.0f;
  next.timestamp = u16(38);
  for (size_t panel = 0; panel < 4; ++panel) {
    next.dcv[panel] = u16(26 + 2*(panel/2)) / 26.3f;
    next.dcc[panel] = u16(30 + 2*panel) / 86.5f; // PV2 lab calibration; other channels await checks.
    const size_t off = 70 + 4*panel;
    next.energy[panel] = ((uint32_t)u16(off) << 16) | u16(off+2);
  }
  output = next; // Commit only after the complete frame passes validation.
  return true;
}
