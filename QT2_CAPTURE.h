#pragma once
#include <stddef.h>
#include <stdint.h>

// Fixed versioned disk records; no credentials or unrelated network traffic.
struct __attribute__((packed)) Qt2CaptureRecord {
  uint32_t magic;
  uint32_t sequence;
  uint32_t bootId;
  uint64_t uptimeMs;
  int64_t localEpoch;
  int16_t utcOffsetMinutes;
  uint16_t length;
  uint16_t source;
  uint16_t cluster;
  int8_t rssi;
  uint8_t lqi;
  uint8_t connectedMask;
  uint8_t event; // 0: RX (after L1 decryption), 1: poll timeout
  char serial[13];
  char ecuVersion[32];
  char inverterVersion[20];
  uint8_t data[300];
  uint32_t checksum;
};

static inline uint32_t qt2CaptureChecksum(const Qt2CaptureRecord &record) {
  const uint8_t *p = reinterpret_cast<const uint8_t *>(&record);
  uint32_t hash = 2166136261UL;
  for (size_t i = 0; i < offsetof(Qt2CaptureRecord, checksum); ++i)
    hash = (hash ^ p[i]) * 16777619UL;
  return hash;
}

static inline bool qt2CaptureValid(const Qt2CaptureRecord &r) {
  return r.magic == 0x51543231 && r.sequence && r.length <= sizeof(r.data) &&
         r.event <= 1 && r.serial[12] == 0 && r.ecuVersion[31] == 0 &&
         r.inverterVersion[19] == 0 && r.checksum == qt2CaptureChecksum(r);
}
