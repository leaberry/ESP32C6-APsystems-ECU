#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>

// Issue 39: separate source PAN for cross-PAN sends, no NWK source IEEE field.
inline size_t qs1PairingFrame(uint8_t *out, size_t cap, uint16_t sourcePan,
                             uint16_t destPan, uint16_t cluster, const uint8_t *data,
                             size_t length, uint8_t mac, uint8_t nwk, uint8_t aps,
                             uint8_t radius) {
  if (!out || !data || !length || length > 32 || !sourcePan ||
      (sourcePan == 0xFFFF && (destPan != 0xFFFF || cluster != 0x020C)) ||
      (destPan != sourcePan && destPan != 0xFFFF) ||
      (cluster != 0x020D && cluster != 0x020C && cluster != 0x010F && cluster != 0x0101)) return 0;
  bool crossPan = destPan != sourcePan;
  size_t need = (crossPan ? 27 : 25) + length;
  if (cap < need) return 0;
  size_t p = 0;
  auto le = [&](uint16_t n) { out[p++] = n; out[p++] = n >> 8; };
  le(crossPan ? 0x8801 : 0x8841); out[p++] = mac;
  le(destPan); le(0xFFFF);
  if (crossPan) le(sourcePan);
  le(0); // Coordinator short address.
  le(0x0008); le(0xFFFF); le(0); out[p++] = radius; out[p++] = nwk;
  out[p++] = 0x08; out[p++] = 0x14; le(cluster); le(0x0F05);
  out[p++] = 0x14; out[p++] = aps;
  memcpy(out + p, data, length);
  return p + length;
}

bool qs1CaptureObserve(const uint8_t *, size_t, uint32_t, uint8_t, int8_t, uint8_t);
