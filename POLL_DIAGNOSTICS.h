#pragma once
#include <stdint.h>
#include <stddef.h>

// Counters are attempt-window deltas, not exclusively target-inverter traffic.
enum PollDiagCounter : uint8_t {
  PD_RX, PD_TARGET_RX, PD_RAW_DROP, PD_APP_DROP, PD_PARSE_REJECT,
  PD_FRAGMENT_MISS, PD_FRAGMENT_EVICT, PD_ACK_FAIL, PD_CCA, PD_COEX,
  PD_TX_FAIL, PD_DELIVERED, PD_IGNORED, PD_DECRYPT_FAIL, PD_QUEUE_CLEAR,
  PD_TARGET_ASDU, PD_UNFRAGMENTED_ACK_REQUEST,
  PD_STALE, PD_UNEXPECTED, PD_BAD_LENGTH, PD_BAD_KIND, PD_BAD_CHECKSUM,
  PD_BAD_VALUE, PD_DUPLICATE, PD_ACCEPTED, PD_COUNT
};

struct PollDiagRecord {
  uint32_t magic;
  uint16_t version, size;
  uint32_t sequence, boot, startedMs, elapsedMs;
  int64_t localEpoch;
  char firmware[40], serialTail[5];
  uint8_t inverter, attempt, txOk, result;
  uint16_t pan, source, replyMask, wantedMask, acceptedMask, roundMask;
  uint32_t counters[PD_COUNT];
  uint32_t checksum;
};

inline uint32_t pollDiagChecksum(const PollDiagRecord &r) {
  const uint8_t *p = reinterpret_cast<const uint8_t *>(&r);
  uint32_t hash = 2166136261UL;
  for (size_t i = 0; i < offsetof(PollDiagRecord, checksum); ++i)
    hash = (hash ^ p[i]) * 16777619UL;
  return hash;
}
inline bool pollDiagValid(const PollDiagRecord &r) {
  return r.magic == 0x50443131 && r.version == 2 && r.size == sizeof(r) &&
         r.checksum == pollDiagChecksum(r);
}

bool pollDiagnosticsClear();
void pollDiagnosticsBegin();
void pollDiagnosticsSetEnabled(bool enabled);
void pollDiagnosticsAccepted(int which);
void pollDiagnosticsCount(PollDiagCounter counter, uint32_t count = 1);
void pollDiagnosticsRawDrop();
void pollDiagnosticsRaw(uint16_t pan, uint16_t source);
void pollDiagnosticsReply(int which);
void pollDiagnosticsStart(int which, uint8_t attempt, uint16_t wantedMask = 0);
void pollDiagnosticsFinish(bool txOk, int result, uint16_t roundMask = 0);
void pollDiagnosticsFlush();
String pollDiagnosticsReport(size_t limit);
