/*
 * APsystems proprietary L1 AES transport.
 *
 * This is deliberately separate from Zigbee NWK/APS security.  The legacy
 * CC25xx modem exposed frames as:
 *   FC FC | short/rssi/lqi | peer UID (6) | plaintext L2
 * or
 *   FC FC | short/rssi/lqi | peer UID (6) | nonce (6) | ciphertext
 *
 * With the ESP32-C6 raw APS API the metadata/FC FC wrapper is absent, so the
 * ASDU starts at peer UID.  The old modem's outbound A0/A1 gate is also absent.
 */

static const uint8_t APS_AES_KEY_TAIL[4] = {0x18, 0x28, 0x45, 0x90};

static uint8_t apsHexNibble(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  return 0;
}

bool apsSerialDefaultsToEncrypted(const char *serial) {
  return serial && strlen(serial) == 12 && serial[1] == '2';
}

static bool apsSerialToBcd(const char *serial, uint8_t out[6]) {
  if (!serial || strlen(serial) != 12) return false;
  for (uint8_t i = 0; i < 6; ++i) {
    char hi = serial[i * 2], lo = serial[i * 2 + 1];
    if (!isxdigit((unsigned char)hi) || !isxdigit((unsigned char)lo)) return false;
    out[i] = (apsHexNibble(hi) << 4) | apsHexNibble(lo);
  }
  return true;
}

static void apsFrameKey(const uint8_t nonce[6], const uint8_t uid[6], uint8_t key[16]) {
  memcpy(key, nonce, 6);
  memcpy(key + 6, uid, 6);
  memcpy(key + 12, APS_AES_KEY_TAIL, 4);
}

static bool apsEcb(bool encrypt, const uint8_t key[16], const uint8_t *input,
                   size_t length, uint8_t *output) {
  if (!length || (length & 15)) return false;
  mbedtls_aes_context ctx;
  mbedtls_aes_init(&ctx);
  int rc = encrypt ? mbedtls_aes_setkey_enc(&ctx, key, 128)
                   : mbedtls_aes_setkey_dec(&ctx, key, 128);
  for (size_t off = 0; rc == 0 && off < length; off += 16) {
    rc = mbedtls_aes_crypt_ecb(&ctx, encrypt ? MBEDTLS_AES_ENCRYPT : MBEDTLS_AES_DECRYPT,
                               input + off, output + off);
  }
  mbedtls_aes_free(&ctx);
  return rc == 0;
}

int apsFindInverter(uint16_t shortAddress, const uint8_t *peerUid) {
  for (int i = 0; i < inverterCount; ++i) {
    if (strlen(Inv_Prop[i].invID) == 4) {
      uint16_t saved = (uint16_t)((apsHexNibble(Inv_Prop[i].invID[0]) << 4) |
                                  apsHexNibble(Inv_Prop[i].invID[1]));
      saved |= (uint16_t)((apsHexNibble(Inv_Prop[i].invID[2]) << 4) |
                           apsHexNibble(Inv_Prop[i].invID[3])) << 8;
      if (saved == shortAddress) return i;
    }
  }
  if (peerUid) {
    uint8_t wanted[6];
    for (int i = 0; i < inverterCount; ++i) {
      if (apsSerialToBcd(Inv_Prop[i].invSerial, wanted) && !memcmp(wanted, peerUid, 6)) return i;
    }
  }
  return -1;
}

bool apsInverterUsesEncryption(int which) {
  return which >= 0 && which < inverterCount &&
         (Inv_Prop[which].encrypted || apsSerialDefaultsToEncrypted(Inv_Prop[which].invSerial));
}

static bool apsDecryptTail(const uint8_t uid[6], const uint8_t *tail, size_t tailLen,
                           uint8_t *plain, size_t plainCap, size_t *plainLen) {
  if (tailLen < 22 || ((tailLen - 6) & 15)) return false;
  uint8_t key[16], padded[288];
  size_t cipherLen = tailLen - 6;
  if (cipherLen > sizeof(padded)) return false;
  apsFrameKey(tail, uid, key);
  if (!apsEcb(false, key, tail + 6, cipherLen, padded)) return false;
  size_t bodyLen = padded[0];
  if (bodyLen < 4 || bodyLen + 1 > cipherLen || bodyLen > plainCap) return false;
  if (padded[1] != 0xFB || padded[2] != 0xFB) return false;
  memcpy(plain, padded + 1, bodyLen);
  *plainLen = bodyLen;
  return true;
}

// Converts raw APS data to the legacy plaintext form: peer UID | FB FB ...
bool apsDecryptIncoming(uint16_t source, const uint8_t *input, size_t inputLen,
                        uint8_t *output, size_t outputCap, size_t *outputLen,
                        int *whichInverter) {
  if (inputLen < 8 || outputCap < inputLen) return false;
  int which = apsFindInverter(source, input);
  if (whichInverter) *whichInverter = which;

  // Plaintext is unambiguous and remains supported for mixed installations.
  if (input[6] == 0xFB && input[7] == 0xFB) {
    memcpy(output, input, inputLen);
    *outputLen = inputLen;
    return true;
  }

  size_t bodyLen = 0;
  // Native-C6 form: UID | nonce | ciphertext.
  if (apsDecryptTail(input, input + 6, inputLen - 6, output + 6, outputCap - 6, &bodyLen)) {
    memcpy(output, input, 6);
  // Defensive compatibility with traces that retain the modem A0/A1 gate.
  } else if (inputLen > 7 && (input[6] == 0xA0 || input[6] == 0xA1) &&
             apsDecryptTail(input, input + 7, inputLen - 7, output + 6, outputCap - 6, &bodyLen)) {
    memcpy(output, input, 6);
  } else {
    return false;
  }
  *outputLen = bodyLen + 6;
  if (which >= 0) Inv_Prop[which].encrypted = true;
  return true;
}

// Input/output include the six-byte clear sender UID used by the original ASDU.
bool apsEncryptOutgoing(int which, const uint8_t *input, size_t inputLen,
                        uint8_t *output, size_t outputCap, size_t *outputLen,
                        bool broadcast) {
  if (inputLen < 10 || input[6] != 0xFB || input[7] != 0xFB) return false;
  const uint8_t *body = input + 6;
  size_t bodyLen = inputLen - 6;
  size_t cipherLen = (bodyLen + 1 + 15) & ~((size_t)15);
  if (6 + 6 + cipherLen > outputCap || cipherLen > 288) return false;

  uint8_t uid[6] = {};
  if (!broadcast && (which < 0 || !apsSerialToBcd(Inv_Prop[which].invSerial, uid))) return false;
  uint8_t nonce[6], key[16], padded[288] = {};
  esp_fill_random(nonce, sizeof(nonce));
  padded[0] = (uint8_t)bodyLen;
  memcpy(padded + 1, body, bodyLen);
  apsFrameKey(nonce, uid, key);

  memcpy(output, input, 6);             // clear ECU UID prefix
  memcpy(output + 6, nonce, 6);
  if (!apsEcb(true, key, padded, cipherLen, output + 12)) return false;
  *outputLen = 12 + cipherLen;
  return true;
}

bool apsAllInvertersEncrypted() {
  if (inverterCount < 1) return false;
  for (int i = 0; i < inverterCount; ++i) if (!apsInverterUsesEncryption(i)) return false;
  return true;
}

bool apsCryptoSelfTest() {
  const uint8_t key[16] = {0x11,0x22,0x33,0x44,0x55,0x66,0x20,0x80,0x00,0x00,0x42,0x58,0x18,0x28,0x45,0x90};
  const uint8_t plain[16] = {0x0C,0xFB,0xFB,0x06,0xBB,0x00,0x01,0x02,0x03,0x1A,0x7C,0xFE,0xFE,0,0,0};
  const uint8_t expected[16] = {0x2C,0x20,0xFF,0x70,0xD7,0xF5,0xB6,0x04,0xFC,0xCC,0xD5,0x52,0xFE,0x2A,0xC7,0xC8};
  uint8_t actual[16];
  return apsEcb(true, key, plain, sizeof(plain), actual) && !memcmp(actual, expected, 16);
}
