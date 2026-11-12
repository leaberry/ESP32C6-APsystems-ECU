#pragma once
#include <stdint.h>
#include <stddef.h>
class AsyncWebServerRequest;

// Independent of energy accounting and every reporting integration.
namespace graphHistory {
constexpr uint8_t channels = 9;
constexpr uint16_t missing = UINT16_MAX;
constexpr uint32_t daySeconds = 86400;
constexpr uint32_t magic = 0x31485047; // GPH1, little endian
constexpr size_t budget = 192 * 1024; // includes the temporary replacement file
struct __attribute__((packed)) Header {
  uint32_t magic, day, identity, step, count, crc;
};
struct __attribute__((packed)) Cell { uint16_t watts; uint8_t weight; };
inline uint32_t stepFor(uint32_t polling) {
  if (polling > daySeconds) polling = daySeconds;
  return polling > 60 ? ((polling + 59) / 60) * 60 : 60;
}
inline uint32_t countFor(uint32_t step) { return (daySeconds + step - 1) / step; }
inline size_t rowBytes(uint32_t step) { return channels * (step < 300 ? 2 : 3); }
inline size_t fileBytes(uint32_t step) { return sizeof(Header) + countFor(step) * rowBytes(step); }
inline uint32_t retentionStep(uint32_t age, uint32_t oldStep) {
  const uint32_t minimum = age <= 3 ? 60 : age <= 10 ? 300 : 3600;
  return oldStep > minimum ? oldStep : minimum;
}
inline bool validHeader(const Header &h, size_t bytes) {
  return h.magic == magic && h.day >= 18262 && h.day <= 47481 &&
      h.step >= 60 && h.step <= daySeconds && h.step % 60 == 0 &&
      h.count == countFor(h.step) && bytes == fileBytes(h.step);
}
inline uint32_t crcUpdate(uint32_t crc, const void *data, size_t length) {
  const auto *p = static_cast<const uint8_t *>(data);
  while (length--) {
    crc ^= *p++;
    for (uint8_t b = 0; b < 8; ++b) crc = (crc >> 1) ^ (0xEDB88320UL & (0U - (crc & 1)));
  }
  return crc;
}
inline Cell average(uint32_t sum, uint32_t weight) {
  return weight ? Cell{static_cast<uint16_t>((sum + weight / 2) / weight),
                       static_cast<uint8_t>(weight)} : Cell{missing, 0};
}
inline bool accept(uint32_t now, uint32_t previous, uint32_t polling, uint32_t step) {
  const uint32_t spacing = polling > step ? polling : step;
  return !previous || (now >= previous && now - previous >= spacing);
}
}

void graphBegin();
void graphLoop();
void graphRecord(uint8_t inverter);
void graphRequestSettings(AsyncWebServerRequest *request);
void graphSendStatus(AsyncWebServerRequest *request);
void graphSendDay(AsyncWebServerRequest *request);
