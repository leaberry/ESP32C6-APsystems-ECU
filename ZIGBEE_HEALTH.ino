void healthCheck() {
  if (!timeRetrieved) getTijd();
  if (!zbStarted) coordinator(true);
  zigbeeUp = zbStarted ? 1 : 0;
  if (!zigbeeUp) errorCode = 3000;
  else if (errorCode == 3000) errorCode = 0;
}

int checkCoordinator() { return zbStarted ? 0 : 2; }
