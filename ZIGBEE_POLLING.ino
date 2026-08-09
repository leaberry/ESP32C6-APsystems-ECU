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
  sendZB(pollCommand);

  errorCode = decodePollAnswer(which);
  if (errorCode == 0) {
    polled[which] = true;
    yield();
    mqttPoll(which);
    yield();
  } else {
    consoleOut("polling failed with errorcode " + String(errorCode));
  }
}
