bool pairQs1(int which) {
  uint8_t uid[6], ecu[6]; char ecuText[13];
  if (which < 0 || which >= inverterCount || Inv_Prop[which].invType != 1 ||
      !pairSerialBytes(Inv_Prop[which].invSerial, uid)) return false;
  ECU_REVERSE().toCharArray(ecuText, sizeof(ecuText));
  if (!apsSerialToBcd(ecuText, ecu) || !zbOperationalPan || zbOperationalPan == 0xFFFF) return false;
  // A diagnostic run without a new capture would leave a misleading old file.
  if (!qs1CaptureBegin(Inv_Prop[which].invSerial, zbOperationalPan)) {
    consoleOut("QS1 capture unavailable; finish downloads or restart before retrying");
    return false;
  }
  bool ok = pairReceiveBegin(Inv_Prop[which].invSerial, zbOperationalPan);
  empty_serial2();
  ok = ok && radioTraceBegin() && rawQs1Reception(zbOperationalPan, false);
  pairAuditStep(PA_RADIO, ok, 1, zbOperationalPan);
  const uint16_t clusters[] = {0x020D, 0x020C, 0x010F, 0x0101};
  const char *stages[] = {"qs1-bootstrap", "qs1-discovery", "qs1-assign", "qs1-commit"};
  for (uint8_t step = 0; step < 4 && ok; ++step) {
    uint8_t data[17] = {}; size_t length = step == 0 || step == 2 ? 17 : 6;
    memcpy(data, step == 3 ? ecu : uid, 6);
    if (length == 17) {
      uint16_t pan = step == 0 ? 0xFFFF : zbOperationalPan;
      data[6] = pan >> 8; data[7] = pan; data[8] = 0x10;
      data[9] = data[10] = 0xFF; memcpy(data + 11, ecu, 6);
    }
    qs1CaptureStage(stages[step], zbOperationalPan);
    pairTraceStage(stages[step], zbOperationalPan, 0);
    uint32_t at = millis();
    ok = apsSendQs1Pairing(zbOperationalPan, 0xFFFF, clusters[step], data, length, true);
    pairAuditStep(PA_COMMAND, ok, step, zbOperationalPan);
    uint32_t elapsed = millis() - at;
    if (ok && elapsed < 2150) delay(2150 - elapsed);
  }
  char verifiedId[5] = {}; uint16_t pan = 0, source = 0;
  bool verified = false;
  if (ok) {
    qs1CaptureStage("qs1-settle", zbOperationalPan);
    delay(10000);
    pairReceiveVerify();
    qs1CaptureStage("qs1-verify", zbOperationalPan);
    pairTraceStage("qs1-verify", zbOperationalPan, 0);
    for (uint8_t attempt = 0; attempt < 3 && ok; ++attempt) {
      ok = apsSendQs1Pairing(zbOperationalPan, zbOperationalPan, 0x020C, uid, 6, false);
      pairAuditStep(PA_VERIFY_QUERY, ok, attempt, zbOperationalPan);
      if (ok) delay(3500);
    }
    verified = pairReceiveFinish(verifiedId, &pan, &source);
  }
  uint16_t previousPan = 0, previousSource = 0;
  if (ok && !verified && apsRadioLoadPeer(Inv_Prop[which].invSerial, &previousPan, &previousSource) &&
      previousPan && previousPan != 0xFFFF && previousPan != zbOperationalPan) {
    ok = rawQs1Reception(previousPan, false) && pairReceiveBegin(Inv_Prop[which].invSerial, previousPan);
    pairReceiveVerify();
    qs1CaptureStage("qs1-saved-network", previousPan);
    pairTraceStage("qs1-saved-network", previousPan, 0);
    for (uint8_t attempt = 0; attempt < 3 && ok; ++attempt) {
      ok = apsSendQs1Pairing(previousPan, previousPan, 0x020C, uid, 6, false);
      pairAuditStep(PA_SAVED_NETWORK, ok, attempt, previousPan);
      if (ok) delay(3500);
    }
    verified = pairReceiveFinish(verifiedId, &pan, &source);
  }
  if (ok && !verified) {
    // Observe unexpected PANs/headers after failure, without accepting a pairing.
    qs1CaptureSetProbe(true);
    pairReceiveStop();
    bool observing = rawQs1Reception(zbOperationalPan, true);
    const uint16_t destinations[] = {0xFFFF, zbOperationalPan};
    for (uint8_t i = 0; i < 2 && observing; ++i) {
      qs1CaptureStage(i ? "observe-operating" : "observe-broadcast", zbOperationalPan);
      observing = apsSendQs1Pairing(zbOperationalPan, destinations[i], 0x020C, uid, 6, false);
      if (observing) delay(3500);
    }
  }
  radioTraceEnd();
  bool restored = rawQs1Reception(zbOperationalPan, false);
  qs1CaptureSetProbe(false);
  pairAuditStep(PA_RESTORE, restored, 0, zbOperationalPan);
  bool saved = ok && verified && restored && saveVerifiedPairing(which, verifiedId, pan, source);
  if (ok && verified && restored) pairAuditStep(PA_STORAGE, saved, 0, pan);
  pairTraceResult(saved, apsStoredTransportMode(Inv_Prop[which].transportMode, Inv_Prop[which].transportTag), restored);
  pairReceiveStop(); empty_serial2();
  qs1CaptureEnd(saved, restored);
  consoleOut(saved ? "QS1 pairing verified and saved" : "QS1 pairing not verified; download QS1 pairing capture");
  if (saved) { sendNO(); checkCoordinator(); }
  return saved;
}
