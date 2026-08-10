# Deferred work and field-test notes

This file records issues intentionally deferred during ESP32-C6 hardware
bring-up. Items here are observations, not completed changes.

## Pairing workflow

- The current workflow requires **Save**, followed by **Pair**. Saving first is
  required by the current implementation because the pairing frames use the
  configured inverter serial number and the returned short ID is written back
  to that inverter's file.
- Consider a single **Save and pair** action that performs those two steps in
  order and presents one progress/result page.

## Inverter discovery

- Consider a read-only **Scan for inverters** action that lists inverter serial
  numbers detected nearby and lets the operator select one instead of typing
  its serial number manually.
- Keep this separate from pairing: scanning should not automatically modify an
  inverter, assign a short address, or migrate it to the ECU's operational PAN.
- Investigate a fast scan on the current channel first. A complete scan may
  need to park temporarily on the APsystems rendezvous PAN `0xFFFF`, listen for
  inverter identity announcements, and optionally sweep Zigbee channels 11-26.
- Restore the operational PAN and channel on every success, failure, timeout,
  or cancellation path so normal polling cannot be stranded by a scan.
- Pairing and polling are now proven on the three field-test DS3 units, but keep
  discovery as a separate, carefully bounded feature.

## Remote diagnostics

- The existing journal at `/LOGPAGE` retains coarse pairing success/failure
  events in RAM.
- The existing `/CONSOLE` page streams `consoleOut()` messages over WebSocket
  when it is open and `diagNose == 1`; open it before starting a field pairing
  attempt to capture all four commands and receive/decode results.
- A bounded in-memory diagnostic ring buffer and authenticated read-only
  `/DIAGNOSTICS` endpoint were added during basement pairing tests. Retain and
  refine these while modernizing the web interface.
- `/CONSOLE` and its `/ws` command channel now require administrator Basic
  authentication. Consider replacing the command-capable console with separate
  read-only diagnostics and narrowly scoped actions in a future rewrite.
- Avoid a raw unauthenticated Telnet server. A secured WebSocket/log endpoint
  can provide TCP-based remote debugging without exposing an interactive shell.
