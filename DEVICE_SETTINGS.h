#pragma once
#include <stdint.h>
#include <string.h>

// Pure validation helpers shared by firmware and host regression tests.
inline bool validNtpServer(const char *host) {
  const size_t n = strlen(host);
  if (!n || n > 253) return false;
  bool numeric = true;
  for (size_t i = 0; i < n; ++i)
    if ((host[i] < '0' || host[i] > '9') && host[i] != '.') numeric = false;
  if (numeric) {
    unsigned parts = 0, value = 0, digits = 0;
    for (size_t i = 0; i <= n; ++i) {
      if (i == n || host[i] == '.') {
        if (!digits || value > 255 || ++parts > 4) return false;
        value = digits = 0;
      } else {
        value = value * 10 + host[i] - '0';
        if (++digits > 3) return false;
      }
    }
    return parts == 4;
  }
  size_t label = 0;
  for (size_t i = 0; i < n; ++i) {
    char c = host[i];
    if (c == '.') {
      if (!label || host[i-1] == '-') return false;
      label = 0;
    } else {
      if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '-')) return false;
      if ((!label && c == '-') || ++label > 63) return false;
    }
  }
  return host[n-1] != '-'; // A trailing dot is a valid fully qualified name.
}

struct AntennaSettings {
  int mode = 0; // 0 unmanaged, 1 internal, 2 external
  int board = 0; // 0 XIAO ESP32-C6, 1 Advanced
  int selectPin = 14;
  int enablePin = 3; // -1 means no enable pin
  bool externalHigh = true;
  bool enableHigh = false;
};

inline void antennaPreset(AntennaSettings &s) {
  if (s.board == 0) {
    s.selectPin = 14; s.enablePin = 3;
    s.externalHigh = true; s.enableHigh = false;
  }
}

inline bool antennaPinAllowed(int pin, int button, int led) {
  // C6 GPIO24-30 are flash; 12/13 are USB; 16/17 are the serial console.
  // Reserve strapping pins, the firmware's GPIO4 output, button and LED.
  return pin >= 0 && pin <= 23 && pin != 4 && pin != 8 && pin != 9 &&
      pin != 15 && pin != 12 && pin != 13 && pin != 16 && pin != 17 &&
      pin != button && pin != led;
}

inline bool validAntennaSettings(const AntennaSettings &s, int button, int led) {
  if (s.mode < 0 || s.mode > 2 || s.board < 0 || s.board > 1) return false;
  if (!s.mode) return true;
  return antennaPinAllowed(s.selectPin, button, led) &&
      (s.enablePin == -1 || (antennaPinAllowed(s.enablePin, button, led) &&
                            s.enablePin != s.selectPin));
}

inline bool antennaSelectLevel(const AntennaSettings &s) {
  return s.mode == 2 ? s.externalHigh : !s.externalHigh;
}
