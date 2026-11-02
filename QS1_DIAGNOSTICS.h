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
  int8_t rssi;
  uint8_t channel, lqi, length;
  uint8_t data[128];
};

struct Qs1Capture {
  static constexpr size_t capacity = 256, first = 64, tail = capacity - first;
  Qs1Packet packets[capacity];
  uint32_t total = 0, rx = 0, tx = 0, started = 0, ended = 0, drops = 0;
  char target[13] = {};
  uint16_t operatingPan = 0;
  bool saved = false, restored = false;
  void add(const Qs1Packet &p) {
    size_t index = total < capacity ? total : first + (total - first) % tail;
    packets[index] = p; ++total;
    if (p.direction == 'R') ++rx;
    if (p.direction == 'T') ++tx;
  }
  size_t count() const { return total < capacity ? total : capacity; }
  const Qs1Packet &at(size_t n) const {
    if (total <= capacity || n < first) return packets[n];
    return packets[first + (total - first + n - first) % tail];
  }
};
static_assert(sizeof(Qs1Capture) < 43000, "Bound QS1 diagnostic memory");
