#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>

struct PassivePacket {
  uint32_t at;
  uint8_t channel, lqi, length;
  int8_t rssi;
  uint8_t bytes[128];
};

struct PassiveCapture {
  static constexpr size_t capacity = 256;
  static constexpr uint32_t duration = 120000;
  PassivePacket packets[capacity];
  uint32_t started = 0, ended = 0;
  size_t count = 0;
  bool running = false;
  const char *reason = "not started";

  void begin(uint32_t now) {
    started = now; ended = 0; count = 0; running = true; reason = "recording";
  }
  void stop(uint32_t now, const char *why) {
    if (running) { running = false; ended = now; reason = why; }
  }
  void tick(uint32_t now) {
    if (running && uint32_t(now - started) >= duration) stop(now, "time limit");
  }
  void observe(const uint8_t *data, size_t n, uint32_t at,
               uint8_t channel, int8_t rssi, uint8_t lqi) {
    if (!running || int32_t(at - started) < 0) return;
    tick(at);
    if (!running || !data || n < 1 || n > 128) return;
    auto &p = packets[count++];
    p.at = at; p.channel = channel; p.rssi = rssi; p.lqi = lqi; p.length = n;
    memcpy(p.bytes, data, n);
    if (count == capacity) stop(at, "buffer full");
  }
};

bool passiveCaptureOwnsRadio();
bool passiveCaptureLoop();
bool passiveCaptureObserve(const uint8_t *, size_t, uint32_t, uint8_t, int8_t, uint8_t);
