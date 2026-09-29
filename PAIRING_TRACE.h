#pragma once
#include "PAIRING_PROTOCOL.h"

constexpr uint8_t PAIR_TRACE_STAGES = 16;
struct PairTracePacket {
  uint32_t at = 0;
  uint16_t pan = 0, source = 0;
  int8_t rssi = 0;
  uint8_t lqi = 0, length = 0, reason = 0;
  uint8_t data[128] = {};
};
struct PairTraceTx {
  uint32_t at = 0;
  uint16_t cluster = 0;
  int error = 0;
  bool ok = false;
  uint8_t length = 0, data[32] = {};
};
struct PairTraceStage {
  const char *name = nullptr; // firmware string literals only
  uint32_t at = 0, related = 0;
  uint16_t pan = 0, source = 0, found = 0;
  bool conflict = false;
  uint8_t sent = 0, validated = 0, checked = 0, reason[2] = {};
  PairTraceTx tx[3];
  PairTracePacket raw[2]; // first and latest target-related frame
};
struct PairTraceAsdu {
  uint32_t at = 0;
  uint16_t pan = 0, source = 0, cluster = 0, length = 0;
  uint8_t stage = 0, data[300] = {};
};
struct PairTraceCapture {
  uint8_t target[6] = {}, count = 0, mode = 0;
  uint32_t started = 0, ended = 0, omittedStages = 0, asdus = 0;
  bool saved = false, restored = false;
  PairTraceStage stages[PAIR_TRACE_STAGES];
  PairTraceAsdu answers[4]; // first two and latest two complete replies
};
static_assert(sizeof(PairTraceCapture) < 10000, "Bound optional pairing trace RAM");
