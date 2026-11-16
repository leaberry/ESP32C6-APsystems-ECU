#pragma once
#include "PAIRING_PROTOCOL.h"

struct Qs1RouteRequest {
  uint16_t pan = 0, sender = 0, source = 0;
  uint8_t request = 0, cost = 0, serial[6] = {};
};

// Decode the observed unsecured, short-address NWK route request. Never use
// the command bytes as a pairing address or treat discovery as confirmation.
inline bool qs1RouteRequest(const uint8_t *b, size_t n, Qs1RouteRequest &out) {
  if (!b || n != 34 || b[0] != 33 ||
      (pairLe16(b+1) & ~uint16_t(0x0030)) != 0x8841 ||
      pairLe16(b+6) != 0xFFFF || !pairValidAddress(pairLe16(b+8)) ||
      pairLe16(b+10) != 0x1009 || pairLe16(b+12) != 0xFFFD ||
      !pairValidAddress(pairLe16(b+14)) || !b[16] ||
      b[18] != 0xFF || b[19] != 0xFF ||
      b[26] != 1 || b[27] != 0 || pairLe16(b+29) != 0) return false;
  if (!pairLe16(b+4) || pairLe16(b+4) == 0xFFFF) return false;
  for (size_t i=20;i<26;++i) if ((b[i]>>4)>9 || (b[i]&15)>9) return false;
  out.pan=pairLe16(b+4); out.sender=pairLe16(b+8); out.source=pairLe16(b+14);
  out.request=b[28]; out.cost=b[31]; memcpy(out.serial,b+20,6);
  return true;
}

inline size_t qs1RouteReply(uint8_t *b, size_t cap, const Qs1RouteRequest &r,
                            uint16_t pan, const uint8_t ecu[6], uint8_t mac, uint8_t nwk) {
  if (!b || cap<57 || !ecu || !pan || pan==0xFFFF || !pairValidAddress(r.source)) return 0;
  size_t p=0;
  auto le=[&](uint16_t v){b[p++]=v; b[p++]=v>>8;};
  auto ieee=[&](const uint8_t *uid){b[p++]=0xFF;b[p++]=0xFF;memcpy(b+p,uid,6);p+=6;};
  le(0x8861); b[p++]=mac; le(pan);
  // Cross-PAN relay copies identify the originator, not a next hop on our PAN.
  le(r.pan==pan ? r.sender : r.source); le(0);
  le(0x1809); le(r.source); le(0); b[p++]=30; b[p++]=nwk;
  ieee(r.serial); ieee(ecu);
  b[p++]=2; b[p++]=0x30; b[p++]=r.request; le(r.source); le(0); b[p++]=r.cost;
  ieee(r.serial); ieee(ecu);
  return p;
}

struct Qs1RouteWindow {
  bool active=false, conflict=false;
  uint8_t serial[6]={}, ecu[6]={}, lastRequest=0;
  uint16_t pan=0, source=0;
  uint32_t since=0, lastSent=0;
  unsigned replies=0;
  void begin(const uint8_t *uid,const uint8_t *identity,uint16_t network,uint32_t now) {
    *this=Qs1RouteWindow{};active=true;memcpy(serial,uid,6);memcpy(ecu,identity,6);
    pan=network;since=now;
  }
  bool accept(const Qs1RouteRequest &r,uint32_t received,uint32_t now) {
    if (!active || int32_t(received-since)<0 || now-since>90000 ||
        memcmp(r.serial,serial,6)) return false;
    if (source && source!=r.source) conflict=true;
    source=r.source;
    if (conflict || replies>=24 || (replies && now-lastSent<500)) return false;
    ++replies;lastSent=now;lastRequest=r.request;return true;
  }
};
