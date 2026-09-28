// The HTTP handler only schedules work; loop() owns the radio transaction.
void handlePair(AsyncWebServerRequest *request) {
  uint8_t serial[6];
  if (iKeuze < 0 || iKeuze >= inverterCount ||
      !pairSerialBytes(Inv_Prop[iKeuze].invSerial, serial)) {
    request->send(400, "text/plain", "Save a valid 12-digit inverter serial first");
    return;
  }
  const bool probe=request->hasParam("probe");
  if(probe && (strcmp(Inv_Prop[iKeuze].invID,"0000") || !flightRecorderEnabled || !apsSerialDefaultsToEncrypted(Inv_Prop[iKeuze].invSerial))) {
    request->send(400,"text/plain","Select an unpaired encrypted inverter and enable the flight recorder first."); return;
  }
  if(encryptedProbeBusy()) {
    request->send(409,"text/plain","Wait for the previous test or log download to finish."); return;
  }
  pendingEncryptedProbe=lastEncryptedProbe=probe;
  pendingPairInverter = iKeuze;
  lastPairInverter = iKeuze;
  lastPairSucceeded = false;
  // Keep the stored ID intact. Pending/result status is separate from routing.
  actionFlag = 60;
  String page = FPSTR(WAIT_PAIR);
  page.replace("{#}", String(iKeuze));
  if(probe) {
    page.replace("Pairing inverter", "Testing inverter");
    page.replace("Listening for the inverter...", "Testing assignment, commit, and telemetry. Allow ten minutes...");
  }
  request->send(200, "text/html", page);
}

void pairOnActionflag() {
  const int which = pendingPairInverter;
  bool success = false;
  if(pendingEncryptedProbe) {
    success=which>=0 && which<inverterCount && coordinator(false) && encryptedProbeRun(which);
    lastPairSucceeded=success;
    consoleOut(success ? "diagnostic suite completed; no local pairing saved" : "diagnostic suite incomplete; download test log");
    pendingEncryptedProbe=false; pendingPairInverter=-1; return;
  }
  if (which >= 0 && which < inverterCount) {
    pairAuditBegin(which, Inv_Prop[which].invSerial);
    bool radioReady = coordinator(false);
    pairAuditStep(PA_RADIO, radioReady, 0, 0);
    if (radioReady) {
    consoleOut("trying pair inv " + String(which));
    success = pairing(which);
    }
    pairAuditStep(PA_DONE, success, 0, 0);
  }
  lastPairSucceeded = success;
  Update_Log(success ? 2 : 4, success ? "success" : "failed");
  consoleOut(success ? "pairing verified and saved" :
                      "pairing not verified; previous local pairing retained");
  pendingPairInverter = -1;
}

bool pairing(int which) {
  if (which < 0 || which >= inverterCount) return false;
  if (apsSerialDefaultsToEncrypted(Inv_Prop[which].invSerial) || apsInverterUsesEncryption(which))
    return pairWithTransportFallback(which);
  char pairCmd[254] = {};
  char ecu_id_reverse[13];
  ECU_REVERSE().toCharArray(ecu_id_reverse, sizeof(ecu_id_reverse));
  char ecu_short[5];
  snprintf(ecu_short, sizeof(ecu_short), "%02X%02X",
           zbOperationalPan >> 8, zbOperationalPan & 0xFF);
  if (!pairReceiveBegin(Inv_Prop[which].invSerial, zbOperationalPan)) return false;
  empty_serial2();
  bool sequenceOk = radioTraceBegin();
  pairAuditStep(PA_RADIO, sequenceOk, 1, 0xFFFF);
  // Diagnostic trials are explicitly gated by the recorder and encrypted serial.
  const uint8_t trials = flightRecorderEnabled && Inv_Prop[which].invSerial[1] == '2' ? 3 : 1;
  char verifiedId[5] = {};
  uint16_t pan = 0, source = 0;
  bool verified = false, restored = false;
  for (uint8_t trial = 0; trial < trials && sequenceOk; ++trial) {
    pairDiagnosticsExperiment(trial);
    if (!pairReceiveBegin(Inv_Prop[which].invSerial, zbOperationalPan)) {
      sequenceOk = false;
      break;
    }
    for (int y = 0; y < 4 && sequenceOk; ++y) {
      switch (y) {
          case 0:// command 0
              // build command 0 this is "24020FFFFFFFFFFFFFFFFF14FFFF14" + "0D0200000F1100" + String(invSerial) + "FFFF10FFFF" + ecu_id_reverse
              snprintf(pairCmd, sizeof(pairCmd), "24020FFFFFFFFFFFFFFFFF14FFFF140D0200000F1100%sFFFF10FFFF%s", Inv_Prop[which].invSerial , ecu_id_reverse);
              break;
          case 1:
              // build command 1 this is "24020FFFFFFFFFFFFFFFFF14FFFF14" + "0C0201000F0600"  + inv serial,
              snprintf(pairCmd, sizeof(pairCmd), "24020FFFFFFFFFFFFFFFFF14FFFF140C0201000F0600%s", Inv_Prop[which].invSerial );
              break;
          case 2:
              // build command 2 this is "24020FFFFFFFFFFFFFFFFF14FFFF14" + "0F0102000F1100"  + invSerial + short ecu_id_reverse, + 10FFF + ecu_id_reverse

              snprintf(pairCmd, sizeof(pairCmd), "24020FFFFFFFFFFFFFFFFF14FFFF140F0102000F1100%s%s10FFFF%s", Inv_Prop[which].invSerial, ecu_short, ecu_id_reverse);
              break;
          case 3:
              // now build command 3 this is "24020FFFFFFFFFFFFFFFFF14FFFF14"  + "010103000F0600" + ecu_id_reverse,
              snprintf(pairCmd, sizeof(pairCmd), "24020FFFFFFFFFFFFFFFFF14FFFF14010103000F0600%s", ecu_id_reverse);
         }
      // Trial 2 tests a count suffix suggested by the host prepare command.
      // Its raw APS mapping is unknown: this is a labeled experiment only.
      if (trial == 2 && y == 2)
        snprintf(pairCmd, sizeof(pairCmd), "24020FFFFFFFFFFFFFFFFF14FFFF140F0102000F1300%s%s10FFFF%s0001",
                 Inv_Prop[which].invSerial, ecu_short, ecu_id_reverse);
      // Reassert PAN for every step; never let another receive operation choose it.
      pairDiagnosticsStage(y, 0xFFFF);
      sequenceOk = apsUsePairingPan(true);
      if (!sequenceOk) { pairAuditStep(PA_COMMAND, false, y, 0xFFFF); break; }
      consoleOut("pair command " + String(y) + " = " + String(pairCmd));
      sequenceOk = sendZB(pairCmd);
      pairAuditStep(PA_COMMAND, sequenceOk, y, 0xFFFF);
      if (!sequenceOk) break;
      // The worker collects direct replies throughout this window, without readZB().
      if (trial > 0 && y == 3) {
        // Repeated final APS command is a hypothesis, not a proven opcode-22 map.
        delay(1000);
        for (uint8_t repeat = 0; repeat < 2 && sequenceOk; ++repeat) {
          pairDiagnosticsStage(12 + repeat, 0xFFFF);
          sequenceOk = apsUsePairingPan(true) && sendZB(pairCmd);
          pairAuditStep(PA_COMMAND, sequenceOk, 12 + repeat, 0xFFFF);
          if (sequenceOk) delay(1000);
        }
      } else delay(4700);
    }
    if (trial > 0 && sequenceOk) {
      pairDiagnosticsStage(14, 0xFFFF);
      delay(10000);
    }

    // A discovery/status reply on FFFF proves contact, not successful migration.
    // Query again on the operational PAN after the four-command handshake settles.
    pairDiagnosticsStage(4, zbOperationalPan);
    restored = apsUsePairingPan(false);
    if (sequenceOk && restored) {
      consoleOut("pairing: settling before operating-PAN verification");
      delay(10000);
      pairReceiveVerify();
      for (int attempt = 0; attempt < 3 && sequenceOk; ++attempt) {
        snprintf(pairCmd, sizeof(pairCmd),
                 "24020FFFFFFFFFFFFFFFFF14FFFF140C0201000F0600%s",
                 Inv_Prop[which].invSerial);
        pairDiagnosticsStage(5 + attempt, zbOperationalPan);
        sequenceOk = apsUsePairingPan(false) && sendZB(pairCmd);
        pairAuditStep(PA_VERIFY_QUERY, sequenceOk, attempt, zbOperationalPan);
        if (sequenceOk) delay(4700);
      }
    }
    verified = pairReceiveFinish(verifiedId, &pan, &source);
    // Previously validated units can retain a different PAN. Require a fresh
    // serial-matched response there, rather than accepting the saved route blindly.
    uint16_t previousPan = 0, previousSource = 0;
    if (sequenceOk && restored && !verified &&
        apsRadioLoadPeer(Inv_Prop[which].invSerial, &previousPan, &previousSource) &&
        previousPan && previousPan != 0xFFFF && previousPan != zbOperationalPan) {
      pairReceiveBegin(Inv_Prop[which].invSerial, previousPan);
      pairReceiveVerify();
      for (int attempt = 0; attempt < 3 && sequenceOk; ++attempt) {
        pairDiagnosticsStage(8 + attempt, previousPan);
        sequenceOk = apsUseSpecificPan(previousPan, "saved pairing verification") && sendZB(pairCmd);
        pairAuditStep(PA_SAVED_NETWORK, sequenceOk, attempt, previousPan);
        if (sequenceOk) delay(4700);
      }
      verified = pairReceiveFinish(verifiedId, &pan, &source);
    }
    pairDiagnosticsResult(trial, sequenceOk && restored, verified);
    // Never retry radio errors or a verified response (including storage errors).
    // Trials are sequential; later success does not establish independent causality.
    if (verified || !restored) break;
  }
  radioTraceEnd();
  restored = apsUsePairingPan(false) && restored;
  pairAuditStep(PA_RESTORE, restored, 0, zbOperationalPan);
  empty_serial2();
  bool saved = sequenceOk && restored && verified &&
      saveVerifiedPairing(which, verifiedId, pan, source);
  if (sequenceOk && restored && verified) pairAuditStep(PA_STORAGE, saved, 0, pan);
  pairReceiveStop();
  if (!saved) {
    consoleOut("pairing failed: transmit, restore, verification, or persistence; see journal");
    return false;
  }
  consoleOut("verified pairing ID " + String(verifiedId));
  sendNO();
  checkCoordinator();
  return true;
}

// Stage the file before touching NVS. SPIFFS cannot rename over an existing
// file, so retain a backup until both stores have been updated.
bool saveVerifiedPairing(int which, const char *id, uint16_t pan, uint16_t source) {
  if (which < 0 || which >= inverterCount) return false;
  return saveVerifiedPairingMode(which, id, pan, source,
      apsStoredTransportMode(Inv_Prop[which].transportMode, Inv_Prop[which].transportTag));
}

bool saveVerifiedPairingMode(int which, const char *id, uint16_t pan, uint16_t source, uint8_t mode) {
  if (which < 0 || which >= inverterCount || mode > APS_TRANSPORT_PLAIN) return false;
  auto updated = Inv_Prop[which];
  updated.transportMode = mode;
  updated.transportTag = APS_TRANSPORT_TAG;
  strlcpy(updated.invID, id, sizeof(updated.invID));
  String path = "/Inv_Prop" + String(which) + ".str";
  String temporary = path + ".pair";
  File file = SPIFFS.open(temporary, "w");
  if (!file) return false;
  bool written = file.write((const uint8_t *)&updated, sizeof(updated)) == sizeof(updated);
  file.flush();
  file.close();
  // Verify the mode and route bytes before modifying either durable store.
  decltype(updated) readback;
  file = SPIFFS.open(temporary, "r");
  written = written && file && file.size() == sizeof(readback) &&
      file.read((uint8_t *)&readback, sizeof(readback)) == sizeof(readback) &&
      !memcmp(&readback, &updated, sizeof(updated));
  file.close();
  if (!written) { SPIFFS.remove(temporary); return false; }
  String backup = path + ".pair-old";
  bool hadFile = SPIFFS.exists(path);
  if (hadFile && ((SPIFFS.exists(backup) && !SPIFFS.remove(backup)) ||
                  !SPIFFS.rename(path, backup))) {
    SPIFFS.remove(temporary);
    return false;
  }
  uint16_t oldPan = 0, oldSource = 0;
  bool hadPeer = apsRadioLoadPeer(updated.invSerial, &oldPan, &oldSource);
  if (!apsRadioRememberPeer(updated.invSerial, pan, source)) {
    if (hadFile && !SPIFFS.rename(backup, path))
      consoleOut("pairing: old inverter file retained as .pair-old; reboot to recover");
    SPIFFS.remove(temporary);
    return false;
  }
  if (!SPIFFS.rename(temporary, path)) {
    bool rolledBack = hadPeer ? apsRadioRememberPeer(updated.invSerial, oldPan, oldSource)
                             : apsRadioForgetPeer(updated.invSerial);
    if (!rolledBack) consoleOut("pairing: failed to restore previous radio peer after file error");
    if (hadFile && !SPIFFS.rename(backup, path))
      consoleOut("pairing: old inverter file retained as .pair-old; reboot to recover");
    SPIFFS.remove(temporary);
    return false;
  }
  if (hadFile) SPIFFS.remove(backup);
  Inv_Prop[which] = updated;
  return true;
}
