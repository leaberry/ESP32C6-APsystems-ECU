#pragma once
#include <stdint.h>
#include <stddef.h>

enum PairAuditStage : uint8_t {
  PA_START, PA_RADIO, PA_COMMAND, PA_VERIFY_QUERY, PA_RX_SUMMARY,
  PA_RESTORE, PA_STORAGE, PA_DONE, PA_SAVED_NETWORK
};
enum PairAuditResult : uint8_t { PA_PENDING, PA_OK, PA_FAILED };

// Allowlisted metadata only: no packet buffers, command strings, ciphertext,
// plaintext ASDU, keys, nonces, passwords, or complete serial numbers.
struct PairAuditEvent {
  uint32_t elapsedMs;
  uint16_t pan;
  uint16_t source;
  uint16_t replies;
  uint16_t traceDropped;
  uint8_t stage;
  uint8_t result;
  uint8_t step;
  int8_t rssi;
  uint8_t lqi;
  uint8_t conflict;
};
struct PairAuditRecord {
  uint32_t magic;
  uint32_t sequence;
  uint32_t startedMs;
  int64_t localEpoch;
  char firmware[32];
  char build[24];
  char serialTail[5];
  uint8_t inverter;
  uint8_t count;
  uint8_t droppedEvents;
  uint8_t result;
  PairAuditEvent events[20];
  uint32_t checksum;
};
inline uint32_t pairAuditChecksum(const PairAuditRecord &r) {
  const uint8_t *bytes = reinterpret_cast<const uint8_t *>(&r);
  uint32_t hash = 2166136261UL;
  for (size_t i = 0; i < offsetof(PairAuditRecord, checksum); ++i)
    hash = (hash ^ bytes[i]) * 16777619UL;
  return hash;
}
inline bool pairAuditValid(const PairAuditRecord &r) {
  return r.magic == 0x50414931 && r.sequence && r.count <= 20 &&
      r.result <= PA_FAILED && r.checksum == pairAuditChecksum(r);
}
