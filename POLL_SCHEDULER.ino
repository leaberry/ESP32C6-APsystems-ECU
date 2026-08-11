/*
 * Cooperative APsystems poll scheduler.
 *
 * One inverter transaction is performed per pass. The Arduino loop gets
 * control back between inverters, allowing operator actions (pairing, power
 * control and grid-profile work) to pre-empt the next telemetry request.
 * Modbus/TCP runs in its own task and always reads the last complete snapshot.
 */

static bool pollRoundActive = false;
static bool pollRoundManual = false;
static bool pollRoundRequested = false;
static uint8_t pollNextInverter = 0;
static uint32_t pollLastRoundStartedMs = 0;
static uint32_t pollNextSendMs = 0;
static bool pollRoundAllSucceeded = false;
static time_t pollLastSuccessfulEpoch = 0;

uint32_t pollingMinimumSeconds() {
  uint8_t configured = 0;
  for (uint8_t i = 0; i < YC600_MAX_NUMBER_OF_INVERTERS; ++i) {
    if (strcmp(Inv_Prop[i].invID, "0000") != 0) ++configured;
  }
  // readZB() may wait 2.5 seconds. Three seconds per inverter prevents a
  // failed fleet round from immediately overlapping the next one.
  uint32_t fleetMinimum = (uint32_t)configured * 3U;
  return fleetMinimum > 5UL ? fleetMinimum : 5UL;
}

uint32_t pollingClampSeconds(uint32_t requested) {
  uint32_t minimum = pollingMinimumSeconds();
  if (requested < minimum) return minimum;
  // One day is a useful upper limit and avoids millis conversion overflow.
  return requested < 86400UL ? requested : 86400UL;
}

void pollSchedulerBegin() {
  pollIntervalSeconds = pollingClampSeconds(pollIntervalSeconds);
  pollLastRoundStartedMs = millis();
}

void pollSchedulerRequest(bool manual) {
  pollRoundRequested = true;
  pollRoundManual = pollRoundManual || manual;
}

bool pollingAllowedNow() {
  // Time/location failures must never disable telemetry. Daylight-aware
  // scheduling only applies after a valid local clock and solar window exist.
  return !daylightPolling || !timeRetrieved || !locationConfigured || dayTime;
}

bool pollingNightModeActive() {
  return Polling && daylightPolling && timeRetrieved &&
         locationConfigured && !dayTime;
}

time_t pollingNextResumeEpoch() {
  if (!pollingNightModeActive()) return 0;
  sunMoon solar;
  if (!solar.init(currentUtcOffsetMinutes, lati, longi)) return 0;
  time_t candidate = solar.sunRise(now()) + (time_t)pollOffset * 60;
  if (candidate <= now())
    candidate = solar.sunRise(now() + 86400UL) + (time_t)pollOffset * 60;
  return candidate > now() ? candidate : 0;
}

bool pollingRoundInProgress() { return pollRoundActive; }

uint32_t pollingSecondsUntilNextRound() {
  if (!Polling || pollRoundActive || !pollingAllowedNow()) return 0;
  uint32_t intervalMs = pollIntervalSeconds * 1000UL;
  uint32_t elapsed = millis() - pollLastRoundStartedMs;
  return elapsed >= intervalMs ? 0 : (intervalMs - elapsed + 999UL) / 1000UL;
}

time_t pollingLastSuccessfulEpoch() { return pollLastSuccessfulEpoch; }

time_t pollingNextEpoch() {
  if (!Polling || !pollingAllowedNow() || !timeRetrieved) return 0;
  uint32_t intervalMs = pollIntervalSeconds * 1000UL;
  uint32_t elapsed = millis() - pollLastRoundStartedMs;
  uint32_t remaining = elapsed >= intervalMs ? 0 : (intervalMs - elapsed + 999UL) / 1000UL;
  return now() + remaining;
}

static void pollSchedulerStartRound(bool manual) {
  // Re-evaluate the floor in case the inverter list changed since setup.
  pollIntervalSeconds = pollingClampSeconds(pollIntervalSeconds);
  pollRoundActive = true;
  pollRoundManual = manual;
  pollRoundRequested = false;
  pollNextInverter = 0;
  pollNextSendMs = millis();
  pollLastRoundStartedMs = millis();
  pollRoundAllSucceeded = inverterCount > 0;
  consoleOut("starting inverter poll round (interval " + String(pollIntervalSeconds) + " s)");
}

void pollSchedulerLoop() {
  uint32_t nowMs = millis();

  if (!pollRoundActive) {
    bool automaticDue = Polling && pollingAllowedNow() &&
      (uint32_t)(nowMs - pollLastRoundStartedMs) >= pollIntervalSeconds * 1000UL;
    if (pollRoundRequested || automaticDue) {
      bool manual = pollRoundManual;
      pollRoundManual = false;
      if ((manual || (Polling && pollingAllowedNow())) && zigbeeUp == 1) {
        pollSchedulerStartRound(manual);
      } else {
        pollRoundRequested = false;
      }
    }
    return;
  }

  // actionFlag represents an operator request. Yield before starting another
  // inverter transaction; test_actionFlag() runs before us in loop().
  if (actionFlag != 0 || (int32_t)(nowMs - pollNextSendMs) < 0) return;

  while (pollNextInverter < YC600_MAX_NUMBER_OF_INVERTERS &&
         strcmp(Inv_Prop[pollNextInverter].invID, "0000") == 0) {
    ++pollNextInverter;
  }

  if (pollNextInverter >= YC600_MAX_NUMBER_OF_INVERTERS) {
    pollRoundActive = false;
    pollRoundManual = false;
    if (pollRoundAllSucceeded && timeRetrieved) pollLastSuccessfulEpoch = now();
    eventSend(2);
    consoleOut(F("inverter poll round complete"));
    return;
  }

  uint8_t which = pollNextInverter++;
  empty_serial2();
  polling(which); // bounded by the 2.5-second APS receive timeout
  if (!polled[which]) pollRoundAllSucceeded = false;
  if (polled[which]) {
    inverterInfoMaybeQuery(which);
  }
  empty_serial2();
  pollNextSendMs = millis() + 250UL;
}
