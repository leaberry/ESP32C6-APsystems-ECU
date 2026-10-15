#pragma once
#include <cstdint>
#include <cstdio>
#include <string>

// Real DS3 page-B capture published by OpenAPS, not the issue #30 reporter.
// https://github.com/bolkedebruin/openaps/blob/e70a8c77a30f5a166fe79c2dd1aa09ce22810516/codec/protection_read_test.go
// The captured 01 00 metadata and 0x2068 cap must pass without modification.
static const std::string ds3LimitCapture =
    "4481FBFB5CDDDE0100206801139C1450139C003D1993003C0003DD03A403200003E8"
    "0000000000640003DD03A503350304012C060D036403B10E320A0DF8048903520013"
    "74136F125C0014032007D023A6031401BF03D9FFFFFFFF049AFFFFFFFF1B6BFEFE";

static void ds3LimitChecksum(std::string &reply) {
  unsigned sum = 0;
  // Skip the fake AF_INCOMING_MSG marker (4481) and L2 start (FBFB).
  for (size_t i = 8; i < reply.size() - 8; i += 2)
    sum += std::stoul(reply.substr(i, 2), nullptr, 16);
  char hex[5];
  snprintf(hex, sizeof(hex), "%04X", sum);
  reply.replace(reply.size() - 8, 4, hex);
}

static std::string ds3LimitReply(int watts, int calibration = 0, uint8_t variant = 4) {
  std::string reply = ds3LimitCapture;
  char hex[5];
  snprintf(hex, sizeof(hex), "%02X", variant);
  reply.replace(16, 2, hex);
  snprintf(hex, sizeof(hex), "%04X", static_cast<unsigned>((watts + calibration) * 16.59));
  reply.replace(18, 4, hex);
  ds3LimitChecksum(reply);
  return reply;
}
