#pragma once
#include "PAIRING_PROTOCOL.h"

enum PairPowerCheck : uint8_t { PP_VALID, PP_MISSING, PP_IDENTITY, PP_MODE, PP_DECRYPT, PP_LENGTH, PP_KIND, PP_CHECKSUM, PP_VALUE };

enum PairAssignment : uint8_t { PAIR_BOOTSTRAP, PAIR_PREPARE, PAIR_COMMIT, PAIR_DIRECT_PAN };

// Functional receive state. It is deliberately independent of diagnostic storage.
struct PairPowerReply {
  uint32_t receivedAt = 0;
  uint16_t cluster = 0, length = 0;
  uint8_t data[300] = {};
};
struct PairTransportSession {
  bool active = false, discovery = false, waiting = false, conflict = false;
  uint8_t target[6] = {};
  uint16_t operatingPan = 0, pan = 0, source = 0, found = 0, operatingSource = 0;
  uint32_t since = 0;
  PairPowerReply reply;
  void begin(const uint8_t uid[6], uint16_t network) {
    *this = PairTransportSession{};
    memcpy(target, uid, 6); operatingPan = network; active = true;
  }
  void stage(uint16_t network, uint16_t address, bool discover, uint32_t now) {
    pan = network; source = address; discovery = discover; found = 0;
    since = now; waiting = false; reply = PairPowerReply{};
  }
  void observe(const uint8_t *b, size_t n, uint32_t at) {
    if (!active || !discovery || (int32_t)(at - since) < 0) return;
    PairReply r;
    if (!decodePairReply(b, n, target, r) || r.announcement || r.pan != pan) return;
    if ((found && found != r.source) ||
        (pan == operatingPan && operatingSource && operatingSource != r.source)) conflict = true;
    found = r.source;
    if (pan == operatingPan) operatingSource = r.source;
  }
  bool accepts(const uint8_t *b, size_t n, uint32_t at) const {
    // Use NWK source for genuine relayed replies; request echoes have source 0.
    return active && !discovery && source && b && n >= 18 && (int32_t)(at - since) >= 0 &&
           pairLe16(b + 4) == pan && pairValidAddress(pairLe16(b + 8)) &&
           pairLe16(b + 14) == source;
  }
  bool asdu(uint16_t network, uint16_t address, uint16_t cluster,
            const uint8_t *data, size_t n, uint32_t at) {
    if (!active || discovery || !source || network != pan || address != source ||
        (int32_t)(at - since) < 0) return false;
    // Consume owned traffic even when invalid; never publish or learn peers here.
    if (waiting && cluster == 0x0106 && data && n >= 6 && n <= sizeof(reply.data) &&
        !memcmp(data, target, 6)) {
      reply.receivedAt = at; reply.cluster = cluster; reply.length = n;
      memcpy(reply.data, data, n);
    }
    return true;
  }
};
static_assert(sizeof(PairTransportSession) < 400, "Pairing must not depend on a large log buffer");
