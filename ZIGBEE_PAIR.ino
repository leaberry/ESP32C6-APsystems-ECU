// The HTTP handler only schedules work; loop() owns the radio transaction.
void handlePair(AsyncWebServerRequest *request) {
  uint8_t serial[6];
  if (iKeuze < 0 || iKeuze >= inverterCount ||
      !pairSerialBytes(Inv_Prop[iKeuze].invSerial, serial)) {
    request->send(400, "text/plain", "Save a valid 12-digit inverter serial first");
    return;
  }
  pendingPairInverter = iKeuze;
  lastPairInverter = iKeuze;
  lastPairSucceeded = false;
  // Keep the stored ID intact. Pending/result status is separate from routing.
  actionFlag = 60;
  String page = FPSTR(WAIT_PAIR);
  page.replace("{#}", String(iKeuze));
  request->send(200, "text/html", page);
}

void pairOnActionflag() {
  const int which = pendingPairInverter;
  bool success = false;
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
    // Reassert PAN for every step; never let another receive operation choose it.
    sequenceOk = apsUsePairingPan(true);
    if (!sequenceOk) { pairAuditStep(PA_COMMAND, false, y, 0xFFFF); break; }
    consoleOut("pair command " + String(y) + " = " + String(pairCmd));
    sequenceOk = sendZB(pairCmd);
    pairAuditStep(PA_COMMAND, sequenceOk, y, 0xFFFF);
    if (!sequenceOk) break;
    // The worker collects direct replies throughout this window, without readZB().
    delay(4700);
  }

  // A discovery/status reply on FFFF proves contact, not successful migration.
  // Query again on the operational PAN after the four-command handshake settles.
  bool restored = apsUsePairingPan(false);
  if (sequenceOk && restored) {
    consoleOut("pairing: settling before operating-PAN verification");
    delay(10000);
    pairReceiveVerify();
    for (int attempt = 0; attempt < 3 && sequenceOk; ++attempt) {
      snprintf(pairCmd, sizeof(pairCmd),
               "24020FFFFFFFFFFFFFFFFF14FFFF140C0201000F0600%s",
               Inv_Prop[which].invSerial);
      sequenceOk = apsUsePairingPan(false) && sendZB(pairCmd);
      pairAuditStep(PA_VERIFY_QUERY, sequenceOk, attempt, zbOperationalPan);
      if (sequenceOk) delay(4700);
    }
  }
  char verifiedId[5] = {};
  uint16_t pan = 0, source = 0;
  bool verified = pairReceiveFinish(verifiedId, &pan, &source);
  // Previously validated units can retain a different PAN. Require a fresh
  // serial-matched response there, rather than accepting the saved route blindly.
  uint16_t previousPan = 0, previousSource = 0;
  if (sequenceOk && restored && !verified &&
      apsRadioLoadPeer(Inv_Prop[which].invSerial, &previousPan, &previousSource) &&
      previousPan && previousPan != 0xFFFF && previousPan != zbOperationalPan) {
    pairReceiveBegin(Inv_Prop[which].invSerial, previousPan);
    pairReceiveVerify();
    for (int attempt = 0; attempt < 3 && sequenceOk; ++attempt) {
      sequenceOk = apsUseSpecificPan(previousPan, "saved pairing verification") && sendZB(pairCmd);
      pairAuditStep(PA_SAVED_NETWORK, sequenceOk, attempt, previousPan);
      if (sequenceOk) delay(4700);
    }
    verified = pairReceiveFinish(verifiedId, &pan, &source);
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
  auto updated = Inv_Prop[which];
  strlcpy(updated.invID, id, sizeof(updated.invID));
  String path = "/Inv_Prop" + String(which) + ".str";
  String temporary = path + ".pair";
  File file = SPIFFS.open(temporary, "w");
  if (!file) return false;
  bool written = file.write((const uint8_t *)&updated, sizeof(updated)) == sizeof(updated);
  file.flush();
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
