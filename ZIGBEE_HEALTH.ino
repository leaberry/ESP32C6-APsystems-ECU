void healthCheck() {
  if (!timeRetrieved) getTijd();
  if (inverterCount < 1) { zigbeeUp = 0; return; }
  if (!zbStarted) coordinator(true);
  zigbeeUp = zbStarted ? 1 : 0;
  if (!zigbeeUp) errorCode = 3000;
}

int checkCoordinator() { return zbStarted ? 0 : 2; }
