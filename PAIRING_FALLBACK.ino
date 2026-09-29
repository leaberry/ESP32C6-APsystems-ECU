#include "PAIRING_SESSION.h"
#include "POLL_TELEMETRY.h"

extern uint16_t zbOperationalPan;
bool rawRadioSetPromiscuous(bool enabled);

static PairPowerCheck pairPowerCheck(const uint8_t uid[6], const PairPowerReply &reply,
                                     int model, bool encrypted) {
  const uint8_t *data = reply.data;
  size_t n = reply.length;
  if (!n) return PP_MISSING;
  if (reply.cluster != 0x0106 || n < 8 || n > sizeof(reply.data) || memcmp(data, uid, 6)) return PP_IDENTITY;
  uint8_t decoded[300]; size_t length = 0;
  if (data[6] == 0xFB && data[7] == 0xFB) {
    if (encrypted) return PP_MODE;
  } else {
    if (!encrypted) return PP_MODE;
    if (!apsDecryptTail(uid, data + 6, n - 6, decoded + 6, sizeof(decoded) - 6, &length) &&
        !(n > 7 && (data[6] == 0xA0 || data[6] == 0xA1) &&
          apsDecryptTail(uid, data + 7, n - 7, decoded + 6, sizeof(decoded) - 6, &length))) return PP_DECRYPT;
    memcpy(decoded, uid, 6); data = decoded; n = length + 6;
  }
  switch (pollTelemetryCheck(data, n, model)) {
    case POLL_VALID: return PP_VALID;
    case POLL_BAD_LENGTH: return PP_LENGTH;
    case POLL_BAD_KIND: return PP_KIND;
    case POLL_BAD_CHECKSUM: return PP_CHECKSUM;
    default: return PP_VALUE;
  }
}

static bool pairDiscover(int which, uint16_t pan, const char *stage) {
  pairTransportStage(pan, 0, true);
  pairTraceStage(stage, pan, 0);
  if (!apsUseSpecificPan(pan, "pairing discovery")) return false;
  char cmd[96];
  snprintf(cmd, sizeof(cmd), "24020FFFFFFFFFFFFFFFFF14FFFF140C0201000F0600%s", Inv_Prop[which].invSerial);
  for (uint8_t i = 0; i < 2; ++i) {
    if (!sendZB(cmd)) return false;
    delay(3500);
  }
  pairTraceDiscovery(pairTransportFound(false), pairTransportConflict());
  return !pairTransportConflict();
}

static bool pairAssign(int which, uint16_t source, PairAssignment command) {
  const bool commit = command == PAIR_COMMIT;
  uint8_t ecu[6], uid[6], payload[17]; char serial[13], hex[35] = {}, cmd[120];
  ECU_REVERSE().toCharArray(serial, sizeof(serial));
  if (!apsSerialToBcd(serial, ecu) || !pairSerialBytes(Inv_Prop[which].invSerial, uid)) return false;
  size_t n = commit ? 6 : 17;
  if (commit) memcpy(payload, ecu, 6);
  else {
    uint16_t requested = command == PAIR_BOOTSTRAP ? 0xFFFF : zbOperationalPan;
    memcpy(payload, uid, 6); payload[6] = requested >> 8; payload[7] = requested;
    payload[8] = 0x10; payload[9] = payload[10] = 0xFF; memcpy(payload + 11, ecu, 6);
  }
  for (size_t i = 0; i < n; ++i) snprintf(hex + 2*i, sizeof(hex) - 2*i, "%02X", payload[i]);
  const char *cluster = commit ? "0101" : command == PAIR_PREPARE ? "0F01" : "0D02";
  const unsigned sequence = commit ? 3 : command == PAIR_PREPARE ? 2 : 0;
  snprintf(cmd, sizeof(cmd), "24020FFFFFFFFFFFFFFFFF14FFFF14%s%02X000F%02X00%s", cluster, sequence, (unsigned)n, hex);
  pairTransportStage(0xFFFF, source, false);
  pairTraceStage(commit ? "commit" : command == PAIR_PREPARE ? "prepare" :
                 command == PAIR_BOOTSTRAP ? "bootstrap" : "directed-PAN", 0xFFFF, source);
  if (!apsUseSpecificPan(0xFFFF, "pairing assignment")) return false;
  // Preserve the field-tested command order, three commits, and settle window.
  for (uint8_t i = 0; i < (commit ? 3 : 1); ++i) {
    if (!sendZB(cmd)) return false;
    delay(commit ? 1000 : 5000);
  }
  if (commit) delay(10000);
  return true;
}

static bool pairVerifyPower(int which, uint16_t source, bool encrypted, bool &radioOk) {
  uint8_t uid[6], ecu[6]; char serial[13];
  ECU_REVERSE().toCharArray(serial, sizeof(serial));
  if (!apsSerialToBcd(serial, ecu) || !pairSerialBytes(Inv_Prop[which].invSerial, uid)) { radioOk = false; return false; }
  pairTransportStage(zbOperationalPan, source, false);
  pairTraceStage(encrypted ? "verify-AES" : "verify-plain", zbOperationalPan, source);
  radioOk = apsUseSpecificPan(zbOperationalPan, "pairing power verification");
  bool verified = radioOk;
  for (uint8_t i = 0; i < 2 && radioOk; ++i) {
    uint8_t plain[19] = {}, payload[32]; size_t length = sizeof(plain);
    static const uint8_t bb[] = {0xFB,0xFB,0x06,0xBB,0,0,0,0,0,0,0xC1,0xFE,0xFE};
    memcpy(plain, ecu, 6); memcpy(plain + 6, bb, sizeof(bb));
    if (encrypted) radioOk = apsEncryptOutgoing(which, plain, sizeof(plain), payload, sizeof(payload), &length, false);
    else memcpy(payload, plain, sizeof(plain));
    if (!radioOk) break;
    // Arm before TX so a reply received during the transmit call is not lost.
    pairTransportWaitForReply();
    int error = -1;
    bool sent = apsSendPairingQuery(zbOperationalPan, source, payload, length, &error);
    // No MAC ACK alone does not prove non-delivery. Other radio errors abort.
    radioOk = sent || error == 3;
    delay(6000);
    PairPowerReply reply;
    PairPowerCheck check = pairTransportTakeReply(reply) ? pairPowerCheck(uid, reply, Inv_Prop[which].invType, encrypted) : PP_MISSING;
    bool valid = check == PP_VALID;
    pairTraceValidation(i, check);
    verified = verified && valid;
  }
  return verified && radioOk && !pairTransportConflict();
}

bool pairWithTransportFallback(int which) {
  uint8_t uid[6];
  if (which < 0 || which >= inverterCount || Inv_Prop[which].invType != 2 || !pairSerialBytes(Inv_Prop[which].invSerial, uid) ||
      !pairReceiveBegin(Inv_Prop[which].invSerial, zbOperationalPan)) return false;
  pairTransportBegin(uid, zbOperationalPan);
  empty_serial2();
  bool ok = radioTraceBegin();
  if (ok) ok = pairDiscover(which, zbOperationalPan, "initial-operating");
  if (ok && !pairTransportFound(true)) ok = pairDiscover(which, 0xFFFF, "initial-discovery");
  uint16_t target = pairTransportFound(false);
  if (ok && !pairTransportFound(true) && !target) {
    ok = pairAssign(which, 0, PAIR_BOOTSTRAP);
    if (ok) ok = pairDiscover(which, 0xFFFF, "after-bootstrap");
    target = pairTransportFound(false);
    if (ok && !target) ok = pairDiscover(which, zbOperationalPan, "bootstrap-operating");
  }
  if (ok && !pairTransportFound(true)) {
    ok = pairValidAddress(target) && pairAssign(which, target, PAIR_PREPARE);
    if (ok) ok = pairDiscover(which, 0xFFFF, "after-prepare-discovery");
    target = pairTransportFound(false);
    if (ok) ok = pairDiscover(which, zbOperationalPan, "after-prepare-operating");
    if (ok && !pairTransportFound(true)) {
      ok = pairValidAddress(target) && pairAssign(which, target, PAIR_COMMIT);
      if (ok) ok = pairDiscover(which, zbOperationalPan, "after-commit");
    }
    if (ok && !pairTransportFound(true)) {
      ok = pairDiscover(which, 0xFFFF, "before-directed-PAN");
      target = pairTransportFound(false);
      if (ok) ok = pairValidAddress(target) && pairAssign(which, target, PAIR_DIRECT_PAN);
      if (ok) ok = pairDiscover(which, zbOperationalPan, "after-directed-PAN");
    }
  }
  uint16_t source = pairTransportFound(true);
  ok = ok && pairValidAddress(source) && !pairTransportConflict();
  uint8_t mode = APS_TRANSPORT_AUTO;
  if (ok && pairVerifyPower(which, source, true, ok)) mode = APS_TRANSPORT_AES;
  if (ok && mode == APS_TRANSPORT_AUTO && pairVerifyPower(which, source, false, ok)) mode = APS_TRANSPORT_PLAIN;
  pairTransportStop();
  radioTraceEnd();
  bool restored = apsUsePairingPan(false);
  restored = rawRadioSetPromiscuous(false) && restored;
  pairAuditStep(PA_RESTORE, restored, 0, zbOperationalPan);
  char id[5]; snprintf(id, sizeof(id), "%02X%02X", source & 0xFF, source >> 8);
  bool saved = ok && restored && mode != APS_TRANSPORT_AUTO &&
      saveVerifiedPairingMode(which, id, zbOperationalPan, source, mode);
  pairAuditStep(PA_STORAGE, saved, mode, zbOperationalPan);
  pairTraceResult(saved, mode, restored);
  // Keep routing/telemetry suppression through durable storage and restoration.
  pairReceiveStop(); empty_serial2();
  if (saved) { sendNO(); checkCoordinator(); }
  return saved;
}
