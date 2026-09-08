# Pairing path review: issue #8

Issue and packet source: <https://github.com/leaberry/ESP32C6-APsystems-ECU/issues/8>.
The fixture contains only its 48 raw packet records, not the full device diagnostic dump.

## Findings

The issue #4 outgoing-length correction is present in 1.4.12. The relayed
commands in issue #8 contain the complete requested serial and ECU identity.
There are additional failures on reception and in the success checks:

* The trace enabled promiscuous reception throughout pairing. In the ESP-IDF
  driver, `esp_ieee802154_set_promiscuous(true)` disables automatic ACK receive,
  ACK transmit, and enhanced ACK transmit. Each direct reply appears four times
  with the same sequence number in the report, consistent with missing MAC ACKs.
  Source: [Espressif driver](https://github.com/espressif/esp-idf/blob/v5.5.2/components/ieee802154/esp_ieee802154.c#L98-L105).
* Frames 16/18/19/20 and 37/38/39/40 are direct, plaintext APS replies on
  cluster `0x0101`, profile `0x0F05`, endpoint `0x14`. Their source is `0x5AF2`.
  The ASDUs are `FF1A704000007719` and `FFFF704000007719`. The telemetry decryptor
  rejected them; the legacy pairing decoder also required a much longer message
  and an ID after the serial. Here the serial ends the ASDU.
* The meanings of `FF1A` and `FFFF` remain unverified. They must not be treated as
  assigned IDs. The direct reply identifies its source through the MAC/NWK headers.
* The old pairing matcher depended on the first 48 diagnostic packets. This log
  dropped 17 packets. Its fallback could identify a relay as the target by
  elimination, which is not positive identity evidence.
* Transmission errors were not returned to pairing. A discovery reply could be
  called success before the network change completed, and speculative peer writes
  could outlive a failed attempt.
* Restoring a previous ID after failure made the web page report success.

## Revised path

1. The authenticated HTTP handler validates the selected saved serial and queues
   one pairing operation. It keeps the previous ID; the status endpoint reports
   the attempt's state separately. Inverter edits/deletes are refused while busy.
2. The main loop starts the coordinator and a dedicated receive session. Normal
   radio filtering and hardware MAC acknowledgements stay enabled.
3. The existing four commands are sent on PAN `0xFFFF`, checking each TX result.
   The receive worker validates exact serial identity before the bounded trace
   buffer, independently of the telemetry queue/decryptor. During this session,
   telemetry processing cannot switch PANs or persist speculative peers.
4. After the handshake and a ten-second settling window, three serial queries run
   on the operating PAN. A direct target reply or a validated legacy `0x1009`
   announcement on that PAN is required. Factory-PAN replies alone cannot pass.
   A previously saved different PAN is also queried if operating-PAN verification
   fails; it requires a fresh serial-matched response, not just a stored address.
   Broadcast command relays never identify the target.
5. The ID and PAN/source tuple are saved only after verification and radio restore.
   The inverter file is staged; SPIFFS's no-overwrite rename behavior is handled
   with a backup. Normal file/NVS errors return failure and restore old data.
   Boot loading recovers the backup if a file replacement was interrupted.
   This is not a power-loss-atomic transaction across SPIFFS and NVS: a power cut
   between store updates can still leave mismatched records and require a retry.
6. Success enables normal operation. Failure preserves previous local pairing
   data and is shown as failure even when a previous ID exists. This cannot undo
   a network change that the physical inverter has already accepted.

`PAIRING_PROTOCOL.h` deliberately supports the two observed reply formats. It does
not guess at encrypted, routed, or otherwise unfamiliar pairing reply layouts.

## Verification and remaining hardware work

Run `python3 tools/test_znp_pairing.py` and `python3 tools/test_pairing_path.py`
with g++ available. The latter runs the actual wire decoder, trace collection,
pairing sequence, command parser, and storage commit helper with simulated radio,
time, and storage. It covers the exact issue packets, overflow before a reply,
legacy announcements, malformed/truncated frames, other serials/relays, factory-
PAN-only contact, identity conflicts, each TX failure, timeout, and storage errors.
Packet replay runs with AddressSanitizer and UndefinedBehaviorSanitizer.
Run `node tools/test_pairing_status.js` to check the real wait-page script,
including a failed retry with an existing ID. The 4 MB USB and 8 MB OTA layouts
both compile with Arduino ESP32 core 3.3.8.

Synthetic operating-PAN and previously saved PAN replies test the success paths; the reporter's log does
not contain evidence of a completed corrected handshake. Build success and replay
tests are not hardware validation. The next useful test is pairing a powered
non-production inverter, then confirming telemetry and persistence after reboot.
Retain the new persistent pairing log if that test fails.

## Historical success and a controlled retry

Commit `fc999eb` and `BUILD-VERIFIED.md` record a DS3 learned using the old
unique-peer fallback on PAN `0x01CE`, source `0xAAEC`, followed by successful
polling after reboot (firmware `5.307`). This is evidence that learning an existing
route worked. It does not validate every step of a fresh network migration, nor
show that every production inverter used the same path. Issue #8 had four
remaining fallback candidates, so that shortcut could not resolve its target.

The current Delete operation removes the local inverter file and compacts slots;
it sends no radio unpair command and does not erase the inverter's own network
settings. Deleting/re-adding therefore does not reproduce a factory-fresh unit.
A retry of Pair on the existing record is the preferable first controlled test.
Before an explicitly authorized production test, save/export energy history,
back up configuration and radio peer records, and retain the current working
firmware. Test one inverter while it has DC power, then verify telemetry from all
units, reboot persistence, and energy continuity. Firmware rollback alone cannot
undo an inverter-side network change. A controlled re-pair test is described below.

## Persistent pairing log

Every accepted attempt records metadata automatically, independently of the
optional general flight recorder. The bounded SPIFFS file retains the latest 24
attempts across reboot and normal OTA updates. Export older attempts before they
rotate out; a full flash erase removes the log. Snapshots are written at attempt
start and each stage, including completion. Pending records after reboot indicate
an interrupted attempt. Checksums reject torn/corrupt snapshots; a power cut
during a write can lose that snapshot. Flash failures are flagged in downloads.

Use **Diagnostics > Download pairing log** (`/diagnostics/pairing-log`, admin
authentication required). The normal diagnostic download includes the latest
three attempts. Recorded fields are firmware/build, local time and uptime,
inverter slot and last four serial digits, step/result, PAN/source addresses,
matched-reply count, trace overflow, RSSI/LQI, and identity-conflict status.
No raw packets, command payloads, ciphertext, nonces, keys, passwords, complete
serial numbers, or unresolved ASDU prefixes are stored in this persistent log.
Raw pairing commands and frame hex dumps remain available in the transient normal
diagnostic trace and its downloadable report, as requested. Download that report
promptly after an attempt; the normal trace is bounded and is lost on reboot.
The metadata-only log is the durable record and is suitable for routine sharing;
treat raw diagnostic captures separately because they contain device identities
and protocol payloads.

`python3 tools/test_pairing_audit.py` runs the actual logger against simulated
flash, checking reboot recovery, pending attempts, rotation, corruption, event
overflow, storage failures, and exclusion of complete serials/arbitrary input.

## A second ECU and production inverters

Do not assume simultaneous pairing to independent ECUs is supported. The existing
commands carry an ECU identity and target PAN; independently implemented
[OpenAPS pairing](https://github.com/bolkedebruin/openaps/blob/main/internal/pairing/statemachine.go)
also explicitly migrates an inverter from the rendezvous PAN to the new ECU's PAN.
That is evidence of a possible network change, not evidence that two ECUs can
pair without disrupting each other. A spare ESP32 alone does not remove that risk.
Use a spare inverter or a separately agreed maintenance test.

## Hardware validation and logging correction

On September 8, a controlled re-pair of one existing DS3 (inverter firmware
5.307) completed in about 44 seconds. Raw capture confirmed serial-matched
replies on the rendezvous PAN, followed by replies on the destination PAN.
This verified an actual network migration, not acceptance of a saved route.
All three site inverters resumed normal polling. Energy counters continued
increasing and finalized history matched the backup byte-for-byte.
Factory-fresh pairing and the reporter's exact inverter still need validation.

This test exposed a first-file creation failure in the permanent audit logger.
Arduino ESP32 3.3.8 VFS can return a truthy directory handle when a missing
read/update file falls back to opendir; SPIFFS ignores that directory name.
The logger now chooses creation using SPIFFS.exists(), which excludes directories,
and rejects directory handles. Existing log files are not truncated.
The regression reproduces the old failure and passes with the correction,
including recovery, rotation, corruption and masked-serial checks.
The logging correction has compiled but has not yet been tested on hardware.
Raw diagnostic logging remains available; the hardware test capture was saved
externally before its transient trace could rotate out.