#include "ENCRYPTED_PROBE.h"
#include "POLL_TELEMETRY.h"

// Uses exactly the envelopes supported by normal reception. No mode learning,
// telemetry publication, or peer writes while testing an uncommitted route.
static bool pairTelemetryValid(const uint8_t uid[6], const ProbeAsdu &answer,
                               int model, bool encrypted) {
  const uint8_t *data = answer.data;
  size_t n = answer.length;
  if (answer.cluster != 0x0106 || n < 8 || n > sizeof(answer.data) || memcmp(data, uid, 6)) return false;
  bool plain = data[6] == 0xFB && data[7] == 0xFB;
  if (plain) return !encrypted && pollTelemetryValid(data, n, model);
  if (!encrypted) return false;
  uint8_t decoded[300]; size_t length = 0;
  if (!apsDecryptTail(uid, data + 6, n - 6, decoded + 6, sizeof(decoded) - 6, &length) &&
      !(n > 7 && (data[6] == 0xA0 || data[6] == 0xA1) &&
        apsDecryptTail(uid, data + 7, n - 7, decoded + 6, sizeof(decoded) - 6, &length))) return false;
  memcpy(decoded, uid, 6);
  return pollTelemetryValid(decoded, length + 6, model);
}

static bool pairVerifyTelemetry(uint8_t phase, int which, uint16_t source,
                                bool encrypted, bool &radioOk) {
  radioOk = encryptedProbeInfoPhase(phase, which, zbOperationalPan, source,
                                    encrypted ? 11 : 7, false);
  ProbeAsdu answers[2]; uint32_t tx[2]; uint8_t uid[6];
  bool enough = false;
  portENTER_CRITICAL(&encryptedProbeMux);
  const auto &p = encryptedCapture.phases[phase];
  // Missing MAC ACKs do not disprove delivery; other transmit failures abort.
  for (uint8_t i = 0; i < p.sent; ++i)
    radioOk = radioOk && (p.txOk[i] || p.txError[i] == 3);
  enough = p.sent == 2 && p.asdus >= 2 && p.pan == zbOperationalPan &&
           p.source == source && !encryptedCapture.conflict;
  memcpy(answers, p.answers, sizeof(answers)); memcpy(tx, p.txMs, sizeof(tx));
  memcpy(uid, encryptedCapture.target, sizeof(uid));
  portEXIT_CRITICAL(&encryptedProbeMux);
  bool verified = radioOk && enough && answers[0].ms != answers[1].ms;
  for (uint8_t i = 0; i < 2 && verified; ++i)
    verified = (int32_t)(answers[i].ms - tx[i]) >= 0 &&
               pairTelemetryValid(uid, answers[i], Inv_Prop[which].invType, encrypted);
  consoleOut(encrypted ? (verified ? "pairing: AES power replies verified" : "pairing: AES power replies not verified") :
                         (verified ? "pairing: plaintext power replies verified" : "pairing: plaintext power replies not verified"));
  return verified;
}

// Common assignment commands are plaintext even for AES devices. First try
// prepare/commit, then the directed 020D PAN assignment observed in issue #25.
// Once on the operating PAN, try the production AES transport before plaintext.
// Flight recording controls extra logging, never whether pairing can succeed.
bool pairWithTransportFallback(int which) {
  uint8_t uid[6];
  if (which < 0 || which >= inverterCount || encryptedProbeBusy() ||
      !pairSerialBytes(Inv_Prop[which].invSerial, uid) ||
      !pairReceiveBegin(Inv_Prop[which].invSerial, zbOperationalPan)) return false;
  empty_serial2();
  if (!encryptedProbeBegin(uid, true)) { pairReceiveStop(); return false; }
  bool ok = radioTraceBegin();
  // Probe5 may already have moved the inverter. Do not reset it to FFFF.
  if (ok) ok = encryptedProbeDiscovery(9, which, zbOperationalPan);
  if (ok && !probeOperatingSeen()) ok = encryptedProbeDiscovery(0, which, 0xFFFF);
  uint16_t target = probeFound(0);
  if (ok && !probeOperatingSeen() && !target) {
    ok = probeAssignment(1, which, 0, 13, 1, false);
    if (ok) ok = encryptedProbeDiscovery(2, which, 0xFFFF);
    target = probeFound(2);
    if (ok && !target) ok = encryptedProbeDiscovery(37, which, zbOperationalPan);
  }
  if (ok && !probeOperatingSeen()) {
    ok = pairValidAddress(target);
    if (ok) ok = probeAssignment(10, which, target, 8, 1, false);
    if (ok) ok = encryptedProbeDiscovery(11, which, 0xFFFF);
    if (ok) ok = encryptedProbeDiscovery(12, which, zbOperationalPan);
    if (ok && !probeOperatingSeen()) {
      target = probeFound(11); ok = pairValidAddress(target);
      if (ok) ok = probeAssignment(14, which, target, 10, 1, false);
      if (ok) ok = probeAssignment(15, which, target, 10, 2, true);
      if (ok) ok = encryptedProbeDiscovery(16, which, zbOperationalPan);
    }
    if (ok && !probeOperatingSeen()) {
      ok = encryptedProbeDiscovery(17, which, 0xFFFF);
      target = probeFound(17); ok = ok && pairValidAddress(target);
      if (ok) {
        consoleOut("pairing: prepare/commit did not move inverter; trying directed PAN assignment");
        ok = probeAssignment(20, which, target, 9, 1, false);
      }
      if (ok) ok = encryptedProbeDiscovery(21, which, zbOperationalPan);
    }
  }
  portENTER_CRITICAL(&encryptedProbeMux);
  uint16_t source = encryptedCapture.operatingSource;
  ok = ok && !encryptedCapture.conflict && pairValidAddress(source);
  portEXIT_CRITICAL(&encryptedProbeMux);
  uint8_t mode = APS_TRANSPORT_AUTO;
  if (ok && pairVerifyTelemetry(35, which, source, true, ok)) mode = APS_TRANSPORT_AES;
  if (ok && mode == APS_TRANSPORT_AUTO) {
    consoleOut("pairing: trying plaintext power replies before saving");
    if (pairVerifyTelemetry(32, which, source, false, ok)) mode = APS_TRANSPORT_PLAIN;
  }
  radioTraceEnd();
  bool restored = apsUsePairingPan(false);
  bool filtered = rawRadioSetPromiscuous(false);
  restored = restored && filtered;
  pairAuditStep(PA_RESTORE, restored, 0, zbOperationalPan);
  // Keep reception owned until both stores commit, even while capture is frozen.
  portENTER_CRITICAL(&encryptedProbeMux);
  encryptedCapture.active = false;
  encryptedCapture.verifiedMode = mode;
  encryptedCapture.restored = restored;
  encryptedCapture.ended = millis();
  portEXIT_CRITICAL(&encryptedProbeMux);
  char id[5]; snprintf(id, sizeof(id), "%02X%02X", source & 0xFF, source >> 8);
  bool saved = ok && restored && mode != APS_TRANSPORT_AUTO &&
               saveVerifiedPairingMode(which, id, zbOperationalPan, source, mode);
  pairAuditStep(PA_STORAGE, saved, mode, zbOperationalPan);
  portENTER_CRITICAL(&encryptedProbeMux);
  encryptedCapture.pairingSaved = encryptedCapture.finished = saved;
  portEXIT_CRITICAL(&encryptedProbeMux);
  pairReceiveStop(); empty_serial2();
  if (saved) {
    consoleOut(mode == APS_TRANSPORT_AES ? "pairing saved: AES" : "pairing saved: plaintext");
    sendNO(); checkCoordinator();
  } else consoleOut("pairing not saved; download encrypted test log and diagnostic report");
  return saved;
}
