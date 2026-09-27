#pragma once
#include "PAIRING_PROTOCOL.h"
#include <stdint.h>
#include <stddef.h>
#include <string.h>

// A read-only firmware/telemetry query suite. This state never becomes a saved radio peer.
constexpr uint8_t PROBE_PHASES = 13;
struct ProbePacket {
  uint32_t ms;
  uint16_t pan, source;
  uint8_t length;
  int8_t rssi;
  uint8_t lqi;
  uint8_t data[128];
};
struct ProbeAsdu {
  uint32_t ms;
  uint16_t cluster, length;
  uint8_t data[300];
};
struct ProbePhase {
  bool entered, broadcast;
  uint16_t pan, source;
  uint8_t mode; // 0 plain; 1 UID/nonce; 2 UID/A1; 3 A1; 4 UID/A0; 5 A0; 6 discovery; 7 plaintext BB telemetry
  uint32_t started, rx, related, asdus;
  uint8_t sent, txLength[2], tx[2][64];
  bool txOk[2];
  int txError[2];
  uint32_t txMs[2];
  ProbePacket packets[6]; // first three and latest three, not sorted
  ProbeAsdu answers[2]; // first and latest
};
struct ProbeCapture {
  bool active = false, finished = false, restored = false;
  uint8_t phase = 0, target[6] = {};
  uint16_t discovered = 0;
  bool conflict = false;
  uint32_t started = 0, ended = 0;
  ProbePhase phases[PROBE_PHASES] = {};
  void begin(const uint8_t uid[6], uint32_t now) {
    memset(phases, 0, sizeof(phases));
    memcpy(target, uid, 6); phase = 0; discovered = 0; conflict = false;
    active = true; finished = restored = false; started = now; ended = 0;
  }
  void stage(uint8_t index, uint16_t pan, uint16_t source, uint8_t mode, uint32_t now) {
    if (!active || index >= PROBE_PHASES) return;
    phase = index; auto &p = phases[index]; p.entered = true;
    p.pan = pan; p.source = source; p.mode = mode; p.broadcast = mode == 6;
    p.started = now - started;
  }
  bool accepts(const uint8_t *b, size_t n) const {
    if (!active || n < 18 || !phases[phase].source) return false;
    const auto &p = phases[phase];
    // A genuine target reply may arrive through a relay. Our echoed requests
    // have NWK source zero and must never enter reply reassembly.
    return pairLe16(b+4) == p.pan && pairValidAddress(pairLe16(b+8)) &&
           pairLe16(b+14) == p.source;
  }
  void observe(const uint8_t *b, size_t n, uint32_t now, int8_t rssi, uint8_t lqi) {
    if (!active || !b || (int32_t)(now-started-phases[phase].started)<0) return;
    auto &p = phases[phase]; ++p.rx;
    PairReply reply;
    if (p.mode == 6 && decodePairReply(b,n,target,reply) &&
        !reply.announcement && reply.pan == 0xFFFF) {
      if (discovered && discovered != reply.source) conflict = true;
      discovered = reply.source;
    }
    bool related = n >= 18 && p.source &&
        (pairLe16(b+8) == p.source || pairLe16(b+14) == p.source);
    for (size_t i=1; !related && i+6+2<=n; ++i)
      related = memcmp(b+i,target,6) == 0;
    if (!related) return;
    uint32_t index = p.related++;
    auto &s = p.packets[index < 3 ? index : 3+index%3];
    memset(&s,0,sizeof(s)); s.ms=now-started; s.length=n<128?n:128;
    s.rssi=rssi; s.lqi=lqi;
    if (n>=18) { s.pan=pairLe16(b+4); s.source=pairLe16(b+14); }
    memcpy(s.data,b,s.length);
  }
  bool asdu(uint16_t pan, uint16_t source, uint16_t cluster,
            const uint8_t *data, size_t n, uint32_t now) {
    if (!active || !data || !n || n>300 || !phases[phase].source ||
        pan!=phases[phase].pan || source!=phases[phase].source ||
        (int32_t)(now-started-phases[phase].started)<0) return false;
    auto &p=phases[phase]; auto &a=p.answers[p.asdus++ ? 1 : 0];
    a.ms=now-started; a.cluster=cluster; a.length=n; memcpy(a.data,data,n);
    return true;
  }
};

// Only constructs the known read-only firmware-version query. No caller opcode.
inline bool probePlainInfo(const uint8_t ecu[6], uint8_t *out, size_t cap, size_t *len) {
  static const uint8_t dc[] = {0xFB,0xFB,0x06,0xDC,0,0,0,0,0,0,0xE2,0xFE,0xFE};
  if (!ecu || !out || !len || cap<6+sizeof(dc)) return false;
  memcpy(out,ecu,6); memcpy(out+6,dc,sizeof(dc)); *len=6+sizeof(dc); return true;
}
