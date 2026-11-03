#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>

struct Qs1Packet {
  const char *stage;
  uint32_t at;
  uint16_t listenPan;
  int16_t error;
  char direction;
  bool unfiltered;
  bool target;
  int8_t rssi;
  uint8_t channel, lqi, length;
  uint8_t data[128];
};

struct Qs1Capture {
  static constexpr size_t stages = 52, samples = 6;
  struct Stage {
    Qs1Packet packets[samples];
    Qs1Packet firstTarget;
    bool hasTarget = false;
    const char *name = "setup";
    uint32_t started = 0, total = 0, rx = 0, tx = 0;
    uint16_t pan = 0, source = 0, asduLength = 0;
    int result = -100; // Not completed or skipped; see stage name.
    uint8_t asdu[300] = {};
    size_t ringCount() const { return total < samples ? total : samples; }
    const Qs1Packet &ringAt(size_t n) const {
      return packets[total <= samples || n < 3 ? n : 3 + (total - 3 + n - 3) % 3];
    }
    bool extraTarget() const {
      if (!hasTarget) return false;
      for (size_t i=0;i<ringCount();++i) {
        const auto &p=ringAt(i);
        if (p.at==firstTarget.at && p.length==firstTarget.length &&
            !memcmp(p.data,firstTarget.data,p.length)) return false;
      }
      return true;
    }
    size_t count() const { return ringCount() + extraTarget(); }
    void add(const Qs1Packet &p) {
      size_t i = total < samples ? total : 3 + (total - 3) % 3;
      packets[i] = p; ++total;
      if (p.target && !hasTarget) { firstTarget=p; hasTarget=true; }
      if (p.direction == 'R') ++rx;
      if (p.direction == 'T') ++tx;
    }
    const Qs1Packet &at(size_t n) const {
      return n<ringCount()?ringAt(n):firstTarget;
    }
  } phase[stages];
  size_t used = 1;
  uint32_t omittedStages = 0, channelRx[16] = {};
  uint32_t total = 0, rx = 0, tx = 0, started = 0, ended = 0, drops = 0;
  char target[13] = {};
  uint16_t operatingPan = 0;
  bool saved = false, restored = false;
  bool stage(const char *name, uint16_t pan, uint32_t at) {
    if (used == stages) { ++omittedStages; return false; }
    auto &s = phase[used++]; s.name = name; s.pan = pan; s.started = at;
    return true;
  }
  void add(const Qs1Packet &p) {
    phase[used - 1].add(p); ++total;
    if (p.direction == 'R') { ++rx; if (p.channel >= 11 && p.channel <= 26) ++channelRx[p.channel - 11]; }
    if (p.direction == 'T') ++tx;
  }
  size_t count() const { size_t n = 0; for (size_t i = 0; i < used; ++i) n += phase[i].count(); return n; }
  const Qs1Packet &at(size_t n) const {
    for (size_t i = 0; i < used; ++i) { if (n < phase[i].count()) return phase[i].at(n); n -= phase[i].count(); }
    return phase[0].packets[0];
  }
};
static_assert(sizeof(Qs1Capture) < 78000, "Bound QS1 diagnostic memory");

void qs1CaptureTx(const uint8_t *, size_t, uint32_t, bool, int);
