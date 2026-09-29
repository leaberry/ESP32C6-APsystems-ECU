#pragma once
#include <stdint.h>

enum ApsTransportMode : uint8_t {
  APS_TRANSPORT_AUTO = 0, APS_TRANSPORT_AES = 1, APS_TRANSPORT_PLAIN = 2
};
// The tag uses the two remaining padding bytes of the existing 52-byte record.
// Old records have no tagged override and keep serial-based auto selection.
constexpr uint16_t APS_TRANSPORT_TAG = 0xEC25;
inline uint8_t apsStoredTransportMode(uint8_t mode, uint16_t tag) {
  return tag == APS_TRANSPORT_TAG && mode <= APS_TRANSPORT_PLAIN ? mode : uint8_t(APS_TRANSPORT_AUTO);
}
inline bool apsTransportEncrypted(uint8_t mode, bool serialDefault) {
  return mode == APS_TRANSPORT_AES || (mode == APS_TRANSPORT_AUTO && serialDefault);
}
