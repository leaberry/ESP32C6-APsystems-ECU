/*
 * Native ESP32-C6 802.15.4 coordinator for the proprietary APsystems link.
 *
 * This intentionally does not form or join a standards-managed Zigbee
 * network. APsystems inverters retain their existing PAN and use several
 * non-standard NWK commands during pairing. The transport switches PANs for
 * each transaction while the Wi-Fi/802.15.4 coexistence arbiter remains on.
 */

static volatile bool zbStarted = false;
static volatile bool zbStartFailed = false;
uint16_t zbOperationalPan = 0xFFFF;

bool rawRadioStart();
bool rawRadioSetPan(uint16_t pan);
bool rawRadioSetPromiscuous(bool enabled);

bool coordinator(bool normal) {
  (void)normal;
  if (zbStarted) {
    zigbeeUp = 1;
    return true;
  }
  if (zbStartFailed) {
    zigbeeUp = 0;
    return false;
  }

  // ECU begins D8A3, therefore the legacy operational PAN is 0xA3D8.
  zbOperationalPan = hexLe16(ECU_ID);
  if (!rawRadioStart()) {
    zbStartFailed = true;
    zigbeeUp = 0;
    consoleOut(F("native APsystems 802.15.4 transport failed to start"));
    return false;
  }

  zbStarted = true;
  zigbeeUp = 1;
  consoleOut(F("native APsystems 802.15.4 transport is ready"));
  return true;
}

void coordinator_init() { coordinator(false); }

bool apsUseSpecificPan(uint16_t requested, const char *reason) {
  if (!zbStarted && !coordinator(false)) return false;
  bool changed = rawRadioSetPan(requested);
  char trace[96];
  snprintf(trace, sizeof(trace),
           "APS %s PAN requested=0x%04X result=%s",
           reason ? reason : "radio", requested, changed ? "OK" : "FAILED");
  diagnosticsAppend(String(trace));
  return changed;
}

bool apsUsePairingPan(bool pairing) {
  return apsUseSpecificPan(pairing ? 0xFFFF : zbOperationalPan,
                           pairing ? "pairing" : "operational");
}

void sendNO() {
  char ecuRev[13], cmd[100];
  ECU_REVERSE().toCharArray(ecuRev, sizeof(ecuRev));
  snprintf(cmd, sizeof(cmd), "2401FFFF1414060001000F1E%sFBFB1100000D6030FBD3000000000000000004010281FEFE", ecuRev);
  sendZB(cmd);
}

void ZBhardReset() {
  consoleOut(F("native 802.15.4 radio is managed by the ESP32-C6"));
}
