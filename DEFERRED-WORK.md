# Deferred work and field-test notes

These are deliberate follow-up items, not descriptions of currently completed
features. Confirm current behavior in the README and web UI before using this
list as a roadmap.

## Pairing workflow

- Pairing currently requires **Save inverter**, then **Pair inverter**. Saving
  first is functionally required because pairing frames use the configured
  serial number and the learned route is written to that inverter's file.
- Consider a single **Save and pair** action that performs both operations and
  presents one result page without hiding the underlying two-step transaction.

## Read-only inverter discovery

- Consider a bounded **Scan for inverters** action that lists observed serial
  numbers and lets the operator select one instead of typing it.
- Scanning must not pair, change a short address or migrate an inverter to an
  operational PAN.
- Investigate a current-channel scan first. A complete scan may need to enter
  the APsystems rendezvous PAN `0xFFFF` and optionally sweep channels 11-26.
- Restore operational PAN/channel on every success, failure, timeout and cancel
  path so normal polling cannot be stranded.

## Service and protocol testing

- Complete a local-broker MQTT publish/subscribe test from a broker reachable
  by the ECU VLAN. Route, settings and truthful failure reporting are tested;
  the field network intentionally blocked the temporary desktop broker.
- Exercise production-history rollover across local midnight and verify exactly
  one finalized record. Then test binary backup, same-file restore and guarded
  wipe on live hardware with a saved recovery copy.
- Obtain encrypted, YC600 and QS1 hardware for model-specific validation.

## Diagnostics hardening

The modern authenticated `/diagnostics`, `/diagnostics/download` and `/console`
routes now provide a bounded trace, downloadable report and live command
console. A future security-focused rewrite could split the command-capable
console into read-only streaming plus narrowly scoped authenticated actions.
Do not add an unauthenticated Telnet shell.
