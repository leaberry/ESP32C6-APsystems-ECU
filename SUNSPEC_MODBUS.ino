/*
 * Read-only SunSpec Modbus/TCP server.
 *
 * Unit 1 is the aggregate installation. Units 2..10 expose inverter 0..8.
 * FC03 and FC04 are supported. The register chain is SunS, Common Model 1,
 * single-phase Inverter Model 101, and the standard end marker.
 */

static const uint16_t SUNSPEC_BASE = 40000;
static const uint16_t SUNSPEC_REG_COUNT = 124;
static const uint16_t SUNSPEC_PORT = 502;
static const uint8_t SUNSPEC_MAX_CLIENTS = 10;
static WiFiServer sunspecTcpServer(SUNSPEC_PORT);
static WiFiClient sunspecClients[SUNSPEC_MAX_CLIENTS];
static uint8_t sunspecRequests[SUNSPEC_MAX_CLIENTS][260];
static size_t sunspecRequestUsed[SUNSPEC_MAX_CLIENTS] = {};
static int8_t sunspecAddressBias[SUNSPEC_MAX_CLIENTS] = {};
static bool sunspecAddressKnown[SUNSPEC_MAX_CLIENTS] = {};

static void ssPut16(uint16_t *bank, size_t &pos, uint16_t value) { bank[pos++] = value; }

static void ssPut32(uint16_t *bank, size_t &pos, uint32_t value) {
  bank[pos++] = value >> 16;
  bank[pos++] = value & 0xFFFF;
}

static void ssPutString(uint16_t *bank, size_t &pos, const char *text, size_t regs) {
  size_t length = text ? strlen(text) : 0;
  for (size_t i = 0; i < regs; ++i) {
    size_t b = i * 2;
    uint8_t hi = b < length ? (uint8_t)text[b] : 0;
    uint8_t lo = b + 1 < length ? (uint8_t)text[b + 1] : 0;
    bank[pos++] = ((uint16_t)hi << 8) | lo;
  }
}

static uint16_t ssU16(float value, float multiplier, uint16_t notImplemented = 0xFFFF) {
  if (!isfinite(value)) return notImplemented;
  long v = lroundf(value * multiplier);
  if (v < 0) v = 0;
  if (v > 65534) v = 65534;
  return (uint16_t)v;
}

static uint16_t ssS16(float value, float multiplier, uint16_t notImplemented = 0x8000) {
  if (!isfinite(value)) return notImplemented;
  long v = lroundf(value * multiplier);
  if (v < -32767) v = -32767;
  if (v > 32767) v = 32767;
  return (uint16_t)(int16_t)v;
}

struct SunSpecValues {
  float watts;
  float voltage;
  float frequency;
  float temperature;
  float dcCurrent;
  float dcVoltage;
  float dcWatts;
  uint64_t energyWh;
  bool online;
  const char *serial;
  const char *firmware;
};

static bool ssValuesForUnit(uint8_t unit, SunSpecValues &v) {
  memset(&v, 0, sizeof(v));
  v.serial = ECU_ID;
  v.firmware = VERSION;
  v.temperature = NAN;
  if (unit == 1) {
    float volts = 0, hz = 0, temp = -1000, dcVolts = 0;
    int online = 0, dcVoltageSamples = 0;
    for (int i = 0; i < inverterCount; ++i) {
      v.watts += Inv_Data[i].pw_total;
      v.energyWh += energyLifetimeWhFor(i);
      if (polled[i]) {
        ++online;
        volts += Inv_Data[i].acv;
        hz += Inv_Data[i].freq;
        if (Inv_Data[i].heath > temp) temp = Inv_Data[i].heath;
      }
      for (int ch = 0; ch < 4; ++ch) if (Inv_Prop[i].conPanels[ch]) {
        v.dcCurrent += Inv_Data[i].dcc[ch];
        v.dcWatts += Inv_Data[i].power[ch];
        if (Inv_Data[i].dcv[ch] > 0) { dcVolts += Inv_Data[i].dcv[ch]; ++dcVoltageSamples; }
      }
    }
    v.online = online > 0;
    if (online) { v.voltage = volts / online; v.frequency = hz / online; v.temperature = temp; }
    if (dcVoltageSamples) v.dcVoltage = dcVolts / dcVoltageSamples;
    return true;
  }
  int i = (int)unit - 2;
  if (i < 0 || i >= inverterCount) return false;
  v.serial = Inv_Prop[i].invSerial;
  v.firmware = Inv_Data[i].firmwareVersion;
  v.watts = Inv_Data[i].pw_total;
  v.energyWh = energyLifetimeWhFor(i);
  v.voltage = Inv_Data[i].acv;
  v.frequency = Inv_Data[i].freq;
  v.temperature = Inv_Data[i].heath;
  v.online = polled[i];
  int dcVoltageSamples = 0;
  for (int ch = 0; ch < 4; ++ch) if (Inv_Prop[i].conPanels[ch]) {
    v.dcCurrent += Inv_Data[i].dcc[ch];
    v.dcWatts += Inv_Data[i].power[ch];
    if (Inv_Data[i].dcv[ch] > 0) { v.dcVoltage += Inv_Data[i].dcv[ch]; ++dcVoltageSamples; }
  }
  if (dcVoltageSamples) v.dcVoltage /= dcVoltageSamples;
  return true;
}

static bool ssBuildBank(uint8_t unit, uint16_t *bank) {
  SunSpecValues v;
  if (!ssValuesForUnit(unit, v)) return false;
  memset(bank, 0, SUNSPEC_REG_COUNT * sizeof(uint16_t));
  size_t p = 0;
  ssPut16(bank, p, 0x5375); ssPut16(bank, p, 0x6E53); // "SunS"

  ssPut16(bank, p, 1); ssPut16(bank, p, 66);          // Common Model 1
  ssPutString(bank, p, "APsystems", 16);
  ssPutString(bank, p, unit == 1 ? "ESP32-C6 ECU" : "Microinverter", 16);
  ssPutString(bank, p, "Native 802.15.4", 8);
  // Unit 1 identifies the ESP32 bridge. Per-inverter unit IDs expose the
  // inverter's software version returned by APsystems command 0xDC.
  ssPutString(bank, p, v.firmware, 8);
  ssPutString(bank, p, v.serial, 16);
  ssPut16(bank, p, unit); ssPut16(bank, p, 0);

  ssPut16(bank, p, 101); ssPut16(bank, p, 50);        // Inverter Model 101
  float amps = v.voltage > 1 ? v.watts / v.voltage : 0;
  ssPut16(bank, p, ssU16(amps, 10));                  // A
  ssPut16(bank, p, ssU16(amps, 10));                  // AphA
  ssPut16(bank, p, 0xFFFF); ssPut16(bank, p, 0xFFFF);// AphB/C
  ssPut16(bank, p, (uint16_t)(int16_t)-1);            // A_SF
  ssPut16(bank, p, 0xFFFF); ssPut16(bank, p, 0xFFFF); ssPut16(bank, p, 0xFFFF); // line-line V
  ssPut16(bank, p, ssU16(v.voltage, 1));              // PhVphA
  ssPut16(bank, p, 0xFFFF); ssPut16(bank, p, 0xFFFF);// PhVphB/C
  ssPut16(bank, p, 0);                                // V_SF
  ssPut16(bank, p, ssS16(v.watts, 1)); ssPut16(bank, p, 0); // W, W_SF
  ssPut16(bank, p, ssU16(v.frequency, 100)); ssPut16(bank, p, (uint16_t)(int16_t)-2);
  ssPut16(bank, p, 0x8000); ssPut16(bank, p, 0);      // VA
  ssPut16(bank, p, 0x8000); ssPut16(bank, p, 0);      // VAr
  ssPut16(bank, p, 0x8000); ssPut16(bank, p, 0);      // PF
  // Persistent finalized days plus the current RAM-only day form a monotonic
  // lifetime counter for SunSpec clients.
  ssPut32(bank, p, v.energyWh > UINT32_MAX ? UINT32_MAX : (uint32_t)v.energyWh);
  ssPut16(bank, p, 0);                                // WH_SF
  ssPut16(bank, p, ssU16(v.dcCurrent, 10)); ssPut16(bank, p, (uint16_t)(int16_t)-1);
  ssPut16(bank, p, ssU16(v.dcVoltage, 10)); ssPut16(bank, p, (uint16_t)(int16_t)-1);
  ssPut16(bank, p, ssS16(v.dcWatts, 1)); ssPut16(bank, p, 0);
  ssPut16(bank, p, ssS16(v.temperature, 1));
  ssPut16(bank, p, 0x8000); ssPut16(bank, p, 0x8000); ssPut16(bank, p, 0x8000);
  ssPut16(bank, p, 0);                                // Tmp_SF
  ssPut16(bank, p, v.online ? (v.watts > 0 ? 4 : 8) : 2); // St
  ssPut16(bank, p, 0);                                // StVnd
  ssPut32(bank, p, 0); ssPut32(bank, p, 0xFFFFFFFF); // Evt1, Evt2
  ssPut32(bank, p, 0); ssPut32(bank, p, 0); ssPut32(bank, p, 0); ssPut32(bank, p, 0);

  ssPut16(bank, p, 0xFFFF); ssPut16(bank, p, 0);      // end model
  return p == SUNSPEC_REG_COUNT;
}

static void ssException(uint8_t slot, const uint8_t *req, uint8_t code) {
  uint8_t response[9] = {req[0],req[1],0,0,0,3,req[6],(uint8_t)(req[7] | 0x80),code};
  sunspecClients[slot].write(response, sizeof(response));
}

static void ssHandleRequest(uint8_t slot, const uint8_t *req, size_t length) {
  if (length < 12 || req[2] || req[3]) return; // Modbus protocol ID must be zero
  uint8_t function = req[7];
  if (function != 3 && function != 4) { ssException(slot, req, 1); return; }
  uint16_t address = ((uint16_t)req[8] << 8) | req[9];
  uint16_t count = ((uint16_t)req[10] << 8) | req[11];
  if (!count || count > 125) { ssException(slot, req, 3); return; }

  if (!sunspecAddressKnown[slot] && address >= SUNSPEC_BASE) {
    sunspecAddressBias[slot] = address == SUNSPEC_BASE + 1 ? -1 : 0;
    sunspecAddressKnown[slot] = true;
  }
  int32_t index = address < SUNSPEC_REG_COUNT ? address
                 : (int32_t)address - SUNSPEC_BASE + sunspecAddressBias[slot];
  if (index < 0 || index + count > SUNSPEC_REG_COUNT) { ssException(slot, req, 2); return; }

  uint16_t bank[SUNSPEC_REG_COUNT];
  if (!ssBuildBank(req[6], bank)) { ssException(slot, req, 2); return; }
  uint8_t response[260];
  size_t total = 9 + count * 2;
  response[0]=req[0]; response[1]=req[1]; response[2]=0; response[3]=0;
  uint16_t mbLength = 3 + count * 2;
  response[4]=mbLength >> 8; response[5]=mbLength; response[6]=req[6];
  response[7]=function; response[8]=count * 2;
  for (uint16_t i = 0; i < count; ++i) {
    uint16_t value = bank[index + i];
    response[9 + i*2] = value >> 8;
    response[10 + i*2] = value;
  }
  sunspecClients[slot].write(response, total);
}

void sunspecLoop() {
  WiFiClient incoming = sunspecTcpServer.accept();
  if (incoming) {
    bool accepted = false;
    for (uint8_t slot = 0; slot < SUNSPEC_MAX_CLIENTS; ++slot) {
      if (!sunspecClients[slot] || !sunspecClients[slot].connected()) {
        if (sunspecClients[slot]) sunspecClients[slot].stop();
        sunspecClients[slot] = incoming;
        sunspecClients[slot].setNoDelay(true);
        sunspecRequestUsed[slot] = 0;
        sunspecAddressKnown[slot] = false;
        accepted = true;
        break;
      }
    }
    if (!accepted) incoming.stop();
  }

  for (uint8_t slot = 0; slot < SUNSPEC_MAX_CLIENTS; ++slot) {
    WiFiClient &client = sunspecClients[slot];
    if (!client || !client.connected()) {
      if (client) client.stop();
      sunspecRequestUsed[slot] = 0;
      continue;
    }
    while (client.available() && sunspecRequestUsed[slot] < sizeof(sunspecRequests[slot])) {
      sunspecRequests[slot][sunspecRequestUsed[slot]++] = client.read();
    }
    while (sunspecRequestUsed[slot] >= 7) {
      uint8_t *request = sunspecRequests[slot];
      size_t frameLength = 6 + (((size_t)request[4] << 8) | request[5]);
      if (frameLength < 8 || frameLength > sizeof(sunspecRequests[slot])) {
        client.stop(); sunspecRequestUsed[slot] = 0; break;
      }
      if (sunspecRequestUsed[slot] < frameLength) break;
      ssHandleRequest(slot, request, frameLength);
      sunspecRequestUsed[slot] -= frameLength;
      memmove(request, request + frameLength, sunspecRequestUsed[slot]);
    }
  }
}

static void sunspecTask(void *) {
  for (;;) {
    sunspecLoop();
    vTaskDelay(pdMS_TO_TICKS(2));
  }
}

void sunspecBegin() {
  sunspecTcpServer.begin();
  xTaskCreate(sunspecTask, "sunspec", 4096, nullptr, 2, nullptr);
  Serial.println(F("SunSpec Modbus/TCP listening on port 502 (unit 1 aggregate, 2-10 inverters)"));
}
