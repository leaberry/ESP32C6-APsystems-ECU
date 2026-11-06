#pragma once
#include <cstdint>
#include <cstddef>
#include <cstdio>
#ifndef APS_USB_PASSIVE
#define APS_USB_PASSIVE 0
#endif

struct UsbPassivePacket {
  uint32_t sequence, at;
  uint8_t channel, lqi, length;
  int8_t rssi;
  uint8_t bytes[128];
};

inline size_t usbPassiveFormat(const UsbPassivePacket &p, char *out, size_t cap) {
  if (cap < 340 || p.length < 4 || p.length > 128 ||
      p.bytes[0] + 1U != p.length) return 0;
  int n = snprintf(out, cap, "%lu,%u,%d,%u,", (unsigned long)p.at,
                   p.channel, p.rssi, p.lqi);
  static const char hex[] = "0123456789ABCDEF";
  for (unsigned i = 0; i < p.length; ++i) {
    out[n++] = hex[p.bytes[i] >> 4];
    out[n++] = hex[p.bytes[i] & 15];
  }
  n += snprintf(out + n, cap - n, ",%lu\n", (unsigned long)p.sequence);
  return (size_t)n;
}

void usbPassiveSetup();
void usbPassiveLoop();
#if APS_USB_PASSIVE
void usbPassiveReceive(uint8_t *, uint8_t, int8_t, uint8_t);
void usbPassiveTransmitted();
#endif
