// A fleet round accepts each inverter once, even when its reply arrives during
// another inverter's broadcast. Manual single-inverter polls stay isolated.
static uint16_t pollRoundAccepted = 0;
static uint32_t pollRoundReceiveSince = 0;

void pollingRoundBegin() {
  pollRoundAccepted = 0;
  empty_serial2();
  pollRoundReceiveSince = millis();
}

void pollPublishTelemetry(int which) {
  polled[which] = true;
  if (timeRetrieved) inverterLastPollSuccess[which] = ecuNow();
  energyRecordTelemetry(which);
  haTelemetry(which);
  yield();
  mqttPoll(which);
  yield();
}

bool pollingRoundSucceeded() {
  bool any = false;
  bool all = true;
  for (int i = 0; i < inverterCount; ++i) {
    if (!strcmp(Inv_Prop[i].invID, "0000")) continue;
    any = true;
    polled[i] = (pollRoundAccepted & (1U << i)) != 0;
    all = all && polled[i];
  }
  return any && all;
}

void pollingCollect(int which, bool fleet) {
  if (which < 0 || which >= inverterCount) return;
  if (fleet && (pollRoundAccepted & (1U << which))) return;
  polled[which] = false;
  if (zigbeeUp != 1) {
    consoleOut(F("skipping poll, native 802.15.4 transport down"));
    return;
  }
  uint16_t wanted = 1U << which;
  uint16_t pan = 0;
  if (fleet && apsRadioLoadPeer(Inv_Prop[which].invSerial, &pan, nullptr)) {
    for (int i = 0; i < inverterCount; ++i) {
      uint16_t peerPan = 0;
      if (strcmp(Inv_Prop[i].invID, "0000") &&
          apsRadioLoadPeer(Inv_Prop[i].invSerial, &peerPan, nullptr) && peerPan == pan)
        wanted |= 1U << i;
    }
  }
  uint16_t accepted = fleet ? pollRoundAccepted : 0;
  if (!fleet) empty_serial2();
  const uint32_t since = fleet ? pollRoundReceiveSince : millis();
  char pollCommand[65] = {};
  char ecuIdReverse[13];
  ECU_REVERSE().toCharArray(ecuIdReverse, sizeof(ecuIdReverse));
  snprintf(pollCommand, sizeof(pollCommand),
           "2401%s1414060001000F13%sFBFB06BB000000000000C1FEFE",
           Inv_Prop[which].invID, ecuIdReverse);
  for (uint8_t attempt = 0; attempt < 2; ++attempt) {
    if (attempt) {
      uint16_t backoffMs = 300 + (esp_random() % 301);
      diagnosticsAppend("poll retry inverter=" + String(which) +
                        " after=" + String(backoffMs) + "ms");
      delay(backoffMs);
    }
    if (sendZB(pollCommand)) accepted = readPollReplies(wanted, accepted, since);
    if (accepted & (1U << which)) break;
  }
  if (fleet) pollRoundAccepted = accepted;
  errorCode = (accepted & (1U << which)) ? 0 : 50;
  if (errorCode) consoleOut("polling failed with errorcode " + String(errorCode));
}

void polling(int which) { pollingCollect(which, false); }
void pollingForRound(int which) { pollingCollect(which, true); }
