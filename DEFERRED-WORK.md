# Deferred work and field-test notes

This file records issues intentionally deferred during ESP32-C6 hardware
bring-up. Items here are observations, not completed changes.

## Inverter add/edit screen

- Replace terse labels with plain-language descriptions and visible help that
  also works on touch devices.
- Explain that **power-limit correction** (`calib`) adjusts outbound throttle
  commands in percentage points, does not calibrate measured production, and
  should normally remain `0`.
- Explain that **Domoticz device ID** (`invIdx`) is used by the legacy Domoticz
  MQTT integration and is unrelated to SunSpec/Home Assistant. It should remain
  `0` when Domoticz is not used.
- Fix the calibration form field: the page currently submits `pMax`, while the
  save handler reads `cal`, so a nonzero correction cannot be saved.
- Default calibration to `0` instead of leaving `{cal}` unresolved/blank for a
  new inverter.
- Replace the unresolved `INVERTER {nr}` heading on the add screen with a clear
  `New inverter` heading or the correct new index.
- Clarify `Serial number`, `Inverter model`, `Display name`, `Pairing status`,
  and `Connected PV inputs`.

## Pairing workflow

- The current workflow requires **Save**, followed by **Pair**. Saving first is
  required by the current implementation because the pairing frames use the
  configured inverter serial number and the returned short ID is written back
  to that inverter's file.
- Consider a single **Save and pair** action that performs those two steps in
  order and presents one progress/result page.
- Initial basement test reached the pairing operation but did not receive a
  usable inverter response. Repeat close to the inverter before treating this
  as a protocol failure.

## Remote diagnostics

- The existing journal at `/LOGPAGE` retains coarse pairing success/failure
  events in RAM.
- The existing `/CONSOLE` page streams `consoleOut()` messages over WebSocket
  when it is open and `diagNose == 1`; open it before starting a field pairing
  attempt to capture all four commands and receive/decode results.
- Add authentication/authorization directly to `/CONSOLE` and `/ws`. The HTTP
  console route currently relies only on the legacy source-address check, and
  the WebSocket handler accepts powerful commands without its own login check.
- A bounded in-memory diagnostic ring buffer and authenticated read-only
  `/DIAGNOSTICS` endpoint were added during basement pairing tests. Retain and
  refine these while modernizing the web interface.
- Avoid a raw unauthenticated Telnet server. A secured WebSocket/log endpoint
  can provide TCP-based remote debugging without exposing an interactive shell.
