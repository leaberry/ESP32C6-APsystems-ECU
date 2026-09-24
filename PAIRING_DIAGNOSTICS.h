#pragma once
#include "PAIRING_PROTOCOL.h"
#include <stdio.h>

// Opt-in metadata only; no packet buffers, keys, nonces or full serials.
// Four relevant samples per phase: first two and most recent two.
struct PairDiagnosticSample {
  uint32_t elapsed;
  uint16_t pan, source, macSource, nwk, cluster, prefix;
  uint8_t bytes, payloadBytes, reason, identity;
  int8_t rssi;
  uint8_t lqi;
  uint8_t status[5];
};
struct PairDiagnosticPhase {
  uint32_t frames, relevant, matched, rejected[9];
  uint16_t requestedPan;
  bool entered;
  PairDiagnosticSample samples[4];
};
struct PairDiagnosticCapture {
  bool enabled = false, active = false;
  uint32_t started = 0;
  uint8_t phase = 0;
  PairDiagnosticPhase phases[12] = {};
  void begin(bool on, uint32_t now) {
    *this = PairDiagnosticCapture{}; enabled = active = on; started = now;
  }
  void stage(uint8_t index, uint16_t pan) {
    if (!active || index >= 12) return;
    phase = index; phases[index].entered = true; phases[index].requestedPan = pan;
  }
  void observe(const uint8_t *b, size_t n, const uint8_t target[6],
               uint32_t now, int8_t rssi, uint8_t lqi) {
    if (!active) return;
    PairDiagnosticPhase &p = phases[phase]; ++p.frames;
    PairReply reply;
    PairReplyCheck reason = checkPairReply(b,n,target,reply);
    ++p.rejected[reason]; if (reason == PR_MATCH) ++p.matched;
    if (!b || n < 8) return;
    // Search excludes PHY/FCS; a target serial in a relayed command is
    // relevant diagnostic traffic but remains rejected by the matcher.
    uint8_t identity = 0;
    for (size_t i=1; i+6 <= n-2; ++i) {
      bool normal=true, reversed=true;
      for (size_t j=0;j<6;++j) { normal &= b[i+j]==target[j]; reversed &= b[i+j]==target[5-j]; }
      if (normal) identity |= 1;
      if (reversed) identity |= 2;
    }
    if (!identity) return;
    uint32_t index=p.relevant++;
    PairDiagnosticSample &s=p.samples[index<2 ? index : 2+(index%2)];
    s={}; s.elapsed=now-started; s.identity=identity; s.reason=reason;
    s.bytes=n; s.rssi=rssi; s.lqi=lqi;
    if (n>=18) {
      s.pan=pairLe16(b+4); s.macSource=pairLe16(b+8);
      s.nwk=pairLe16(b+10); s.source=pairLe16(b+14);
    }
    // Interpret fixed APS offsets only for the direct format we understand.
    if (n>=36 && (s.nwk==0x0008 || s.nwk==0x0048) && b[18]==0) {
      s.cluster=pairLe16(b+20); s.prefix=uint16_t(b[26])<<8|b[27];
      s.payloadBytes=n-28;
      if (reason==PR_MATCH && reply.payloadBytes==13)
        memcpy(s.status,b+34,5);
    }
  }
};
