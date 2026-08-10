void polling(int which) {
  polled[which] = false;
  if (zigbeeUp != 1) {
    consoleOut(F("skipping poll, native 802.15.4 transport down"));
    return;
  }

  char pollCommand[65] = {};
  char ecuIdReverse[13];
  ECU_REVERSE().toCharArray(ecuIdReverse, sizeof(ecuIdReverse));
  snprintf(pollCommand, sizeof(pollCommand),
           "2401%s1414060001000F13%sFBFB06BB000000000000C1FEFE",
           Inv_Prop[which].invID, ecuIdReverse);
  delayMicroseconds(250);
  consoleOut("pollCommand ex checksum:" + String(pollCommand));
  // Manual/API polls do not pass through pollSchedulerLoop(), which normally
  // clears late replies from another inverter on the same PAN. Always start a
  // transaction with a clean application queue so stale profile/info traffic
  // cannot be decoded as fresh telemetry.
  empty_serial2();
  sendZB(pollCommand);

  errorCode = decodePollAnswer(which);
  if (errorCode != 0) {
    // Same-PAN inverters answer the APsystems broadcast together. A collision
    // can occasionally interrupt one inverter between APS fragments; retry
    // once with jitter instead of waiting for the next configured poll cycle.
    uint16_t backoffMs = 300 + (esp_random() % 301);
    diagnosticsAppend("poll retry inverter=" + String(which) +
                      " after=" + String(backoffMs) + "ms");
    delay(backoffMs);
    empty_serial2();
    sendZB(pollCommand);
    errorCode = decodePollAnswer(which);
  }
  if (errorCode == 0) {
    polled[which] = true;
    yield();
    mqttPoll(which);
    yield();
  } else {
    consoleOut("polling failed with errorcode " + String(errorCode));
  }
}
