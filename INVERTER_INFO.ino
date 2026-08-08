/* Query and decode APsystems inverter model/software version (L2 command DC).
 * Reply layouts are the three forms documented by OpenAPS' codec/info.go.
 */

static uint8_t inverterInfoAttempts[YC600_MAX_NUMBER_OF_INVERTERS] = {};

static bool infoHexByte(const char *p, uint8_t &out) {
  auto nibble = [](char c) -> int {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
  };
  int hi = nibble(p[0]), lo = nibble(p[1]);
  if (hi < 0 || lo < 0) return false;
  out = (uint8_t)((hi << 4) | lo);
  return true;
}

static bool decodeInverterInfoL2(const uint8_t *l2, size_t len, uint8_t &model,
                                 char *version, size_t versionSize) {
  if (!l2 || len < 6 || l2[0] != 0xFB || l2[1] != 0xFB ||
      l2[len - 2] != 0xFE || l2[len - 1] != 0xFE) return false;

  uint32_t hi = 0, mid = 0, lo = 0;
  if (len == 16 && l2[2] == 0x09 && l2[3] == 0xDC) {
    model = l2[4]; hi = ((uint32_t)l2[5] << 8) | l2[6];
    lo = ((uint32_t)l2[8] << 8) | l2[9];
    snprintf(version, versionSize, "%lu.%03lu", (unsigned long)hi, (unsigned long)lo);
    return true;
  }
  if (len == 17 && l2[2] == 0x0A && l2[3] == 0xDD && l2[4] == 0xDC) {
    model = l2[5]; hi = ((uint32_t)l2[6] << 8) | l2[7];
    lo = ((uint32_t)l2[9] << 8) | l2[10];
    snprintf(version, versionSize, "%lu.%03lu", (unsigned long)hi, (unsigned long)lo);
    return true;
  }
  if (len == 19 && l2[2] == 0x0C && l2[3] == 0xDD && l2[4] == 0xDC) {
    model = l2[5]; hi = ((uint32_t)l2[6] << 8) | l2[7];
    mid = ((uint32_t)l2[9] << 8) | l2[10];
    lo = ((uint32_t)l2[13] << 8) | l2[14];
    snprintf(version, versionSize, "%lu.%lu.%lu", (unsigned long)hi,
             (unsigned long)mid, (unsigned long)lo);
    return true;
  }
  return false;
}

bool queryInverterInfo(uint8_t which) {
  if (which >= YC600_MAX_NUMBER_OF_INVERTERS || zigbeeUp != 1 ||
      strcmp(Inv_Prop[which].invID, "0000") == 0) return false;

  if (inverterInfoAttempts[which] < 255) ++inverterInfoAttempts[which];
  char command[65] = {}, ecuReverse[13] = {}, received[CC2530_MAX_SERIAL_BUFFER_SIZE] = {};
  ECU_REVERSE().toCharArray(ecuReverse, sizeof(ecuReverse));
  // FB FB 06 DC + five zero bytes + checksum E2 + FE FE.
  snprintf(command, sizeof(command),
           "2401%s1414060001000F13%sFBFB06DC000000000000E2FEFE",
           Inv_Prop[which].invID, ecuReverse);
  empty_serial2();
  sendZB(command);
  readZB(received);
  if (!readCounter) return false;

  char *hex = strstr(received, "FBFB");
  if (!hex) return false;
  char *end = strstr(hex + 4, "FEFE");
  if (!end) return false;
  size_t bytes = (size_t)(end + 4 - hex) / 2;
  if (bytes > 32) return false;
  uint8_t l2[32] = {};
  for (size_t i = 0; i < bytes; ++i) {
    if (!infoHexByte(hex + i * 2, l2[i])) return false;
  }
  if (!decodeInverterInfoL2(l2, bytes, Inv_Data[which].modelCode,
                            Inv_Data[which].firmwareVersion,
                            sizeof(Inv_Data[which].firmwareVersion))) return false;
  consoleOut("inverter " + String(which) + " model 0x" +
             String(Inv_Data[which].modelCode, HEX) + " firmware " +
             String(Inv_Data[which].firmwareVersion));
  return true;
}

void inverterInfoMaybeQuery(uint8_t which) {
  if (which >= YC600_MAX_NUMBER_OF_INVERTERS ||
      strcmp(Inv_Data[which].firmwareVersion, "unknown") != 0 ||
      inverterInfoAttempts[which] >= 3) return;
  queryInverterInfo(which);
}
