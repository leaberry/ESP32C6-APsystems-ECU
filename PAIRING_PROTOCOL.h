#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>

// Pure wire decoder, shared by the radio worker and host packet-replay tests.
// The first byte is PHY length; the last two bytes are FCS/radio metadata.
struct PairReply {
  uint16_t pan = 0;
  uint16_t source = 0;
  uint16_t id = 0;  // little-endian radio address, NOT an ASDU status prefix
  uint16_t prefix = 0;
  bool announcement = false;
};

inline uint16_t pairLe16(const uint8_t *p) {
  return uint16_t(p[0]) | (uint16_t(p[1]) << 8);
}

inline bool pairSerialBytes(const char *serial, uint8_t out[6]) {
  if (!serial || strlen(serial) != 12) return false;
  for (size_t i = 0; i < 6; ++i) {
    char hi = serial[2*i], lo = serial[2*i+1];
    if (hi < '0' || hi > '9' || lo < '0' || lo > '9') return false;
    out[i] = uint8_t((hi-'0')*16 + lo-'0');
  }
  return true;
}

inline bool pairValidAddress(uint16_t address) {
  // 1111 is reserved by the existing web UI as its pending sentinel.
  return address != 0 && address != 0x1111 && address < 0xFFF8;
}

inline bool decodePairReply(const uint8_t *b, size_t captured,
                            const uint8_t target[6], PairReply &reply) {
  if (!b || !target || captured < 30 || b[0] > 127 ||
      size_t(b[0]) + 1 != captured) return false;
  const size_t end = captured - 2;
  // Legacy short/short, PAN compression, no MAC security/IEs/sequence suppression.
  if ((pairLe16(b+1) & ~uint16_t(0x0030)) != 0x8841) return false;
  const uint16_t macDst = pairLe16(b+6), macSrc = pairLe16(b+8);
  const uint16_t nwk = pairLe16(b+10), nwkSrc = pairLe16(b+14);
  if (!pairValidAddress(nwkSrc)) return false;
  PairReply found;
  found.pan = pairLe16(b+4);
  found.source = nwkSrc;
  found.id = nwkSrc;

  if (nwk == 0x1009) {
    // Existing proprietary announcement: serial at 20, compatibility ID at 26.
    // A relay's MAC source is not the inverter's NWK source.
    if (macDst != 0xFFFF || pairLe16(b+12) != 0xFFFC ||
        b[18] != 0xFF || b[19] != 0xFF ||
        memcmp(b+20, target, 6) != 0) return false;
    found.id = pairLe16(b+26);
    if (!pairValidAddress(found.id)) return false;
    found.announcement = true;
  } else {
    // Issue #8: unfragmented plaintext APS 0x0101, endpoint 20/profile 0x0F05.
    // Accept only direct replies. Broadcast copies of our commands aren't evidence.
    if ((nwk != 0x0008 && nwk != 0x0048) || macDst != 0 ||
        pairLe16(b+12) != 0 || macSrc != nwkSrc || end != 34 ||
        b[18] != 0 || b[19] != 0x14 || pairLe16(b+20) != 0x0101 ||
        pairLe16(b+22) != 0x0F05 || b[24] != 0x14 ||
        memcmp(b+28, target, 6) != 0) return false;
    // FF1A and FFFF are observed prefixes of unresolved meaning. Neither is an ID.
    found.prefix = uint16_t(b[26]) << 8 | b[27];
  }
  reply = found;
  return true;
}

struct PairReplySession {
  uint8_t target[6] = {};
  bool active = false;
  bool verifying = false;
  bool heard = false;
  uint16_t replies = 0;
  bool confirmed = false;
  bool conflict = false;
  uint16_t operatingPan = 0;
  PairReply latest;
  PairReply verified;

  bool begin(const char *serial, uint16_t pan) {
    *this = PairReplySession{};
    if (!pairSerialBytes(serial, target) || !pan || pan == 0xFFFF) return false;
    active = true;
    operatingPan = pan;
    return true;
  }
  void verify() { verifying = true; confirmed = false; conflict = false; }
  bool observe(const uint8_t *b, size_t size) {
    PairReply reply;
    if (!active || !decodePairReply(b, size, target, reply)) return false;
    bool changed = !heard || reply.pan != latest.pan ||
        reply.source != latest.source || reply.prefix != latest.prefix ||
        reply.announcement != latest.announcement;
    if (replies != 0xFFFF) ++replies;
    heard = true;
    latest = reply;
    if (verifying && reply.pan == operatingPan) {
      if (confirmed && verified.source != reply.source) conflict = true;
      verified = reply;
      confirmed = true;
    }
    return changed;
  }
  bool success() const { return active && verifying && confirmed && !conflict; }
};
