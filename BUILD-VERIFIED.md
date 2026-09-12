# Build and hardware verification

## Home Assistant MQTT (2026-09-12)

Both 4 MB and 8 MB variants compile with ESP32 core 3.3.8 and ArduinoJson
7.4.2. Each application uses 1,595,784 bytes; static globals use 93,424 bytes.
The HA client additionally allocates an 8 KB MQTT buffer when configured,
plus transient JSON documents. No board was flashed.

`tools/test_home_assistant.py` compiles the actual HA MQTT modules against an
in-memory retained broker, with no filesystem or NVS API available. It passes
initialization, acknowledged counters, lost-echo reconnect, reboot recovery
with pending deltas, failed publication, removed-inverter totals, invalid data,
energy availability, Energy discovery metadata, stable serial identity,
component tombstones, discovery inventory acknowledgment and disable cleanup.
It also tests stale/unavailable commands and successful/failed controls.

`tools/test_power_limit.py` tests the actual query decoder for YC600/QS1/DS3,
calibrated limits, mismatches, truncated data and missing replies. New CI steps
run both tests. Existing Python host and JavaScript UI regressions pass.
The legacy MQTT implementation, format builders and command handler have no diff.

Counter/discovery durability depends on retained broker persistence. An MQTT
echo does not guarantee a broker disk flush. Broker persistence/restart behavior,
live Home Assistant discovery and Energy statistics, real inverter throttling,
replacement-board counter continuity and simultaneous MQTT client load remain
integration/hardware checks. HA limit commands are RAM-only; only explicit HA
configuration saves write the existing settings file. Existing history writes
are unchanged.

## Settings backup and restore (2026-09-12)

Both 4 MB and 8 MB variants compile with Arduino core 3.3.8. Each application
uses 1,560,468 bytes; static globals use 90,128 bytes. No board was flashed.

`tools/test_settings_backup.py` compiles the actual export, validation, upload,
restore and radio-identity functions with ArduinoJson and fake filesystem/NVS
stores. It passes round trips, malformed records, CRC/schema/value rejection,
incomplete exports, original/restored radio addresses, orphan-peer preservation,
inverter replacement, saved power limits, insufficient staging space,
authentication, incomplete/oversized uploads, preview without writes and all
four optional network/antenna restore combinations. Failure injection at each
of 30 apply mutations retains the manifest, blocks startup and completes on
retry. Production-history and current-day checkpoint sentinel files remain
unchanged in every restore scenario.

`tools/test_settings_ui.js` passes preview/confirmation, all restore selections,
error handling, the file-size limit and stale-file-selection tests. The page was
visually checked at 390 px width in Edge with the firmware stylesheet: no
horizontal overflow; network/antenna options are unchecked and Restore is
disabled before validation. Both new tests run in CI.

Existing ECU identity, hostname, NTP/antenna, pairing-path/storage, pairing
audit, ZNP parsing and pairing-status regressions also pass. Same-board restore,
replacement-board pairing continuity, real flash power-loss recovery and Wi-Fi
reconnection remain hardware checks.

## Wi-Fi hostname fixes (2026-09-12)

Both 4 MB and 8 MB variants compile with Arduino core 3.3.8, each using
1,522,212 bytes of application storage and 90,128 bytes of static globals.
`tools/test_wifi_hostname.py` compiles the actual hostname functions and both
HTTP save handlers with Wi-Fi/NVS stubs. It verifies startup ordering, active
versus global hostname reporting, startup failures, normalization and the
31-character boundary, NVS open/write/readback failures, and success/reboot
behavior for both setup and Network forms. The test also runs in CI.

No board was flashed. DHCP packet advertisement and router lease/DNS/cache
behavior still need hardware verification. No mDNS service was added.

## Guarded ECU identity generation (2026-09-12)

The 4 MB and 8 MB builds with identity initialization each use 1,520,126 bytes
of application storage and 90,128 bytes of static globals. Both compile with
Arduino core 3.3.8. `tools/test_ecu_identity.py` compiles the actual identity
and configuration serialization code against ArduinoJson with fake flash/NVS.
It passes generation/reboot reuse, custom-ID preservation, paired/gapped/orphan
record guards, invalid storage, unknown configuration-field preservation,
failed writes/renames, and interrupted-save recovery. The existing pairing-path
and seven pairing-storage scenarios also pass. No board was flashed; physical
pairing with a newly generated identity still needs a hardware check.

## NTP and antenna configuration (2026-09-12)

Both 4 MB USB-only and 8 MB OTA variants compile with Arduino core 3.3.8.
Each application uses 1,516,078 bytes; static globals use 90,128 bytes, leaving
237,552 bytes before runtime allocations. Decoded generated partition tables
confirm the 4 MB factory layout and the 8 MB dual-OTA layout.

`tools/test_device_settings.py` passes against the actual validation, antenna
startup and NTP functions with hardware stubs: default unmanaged operation,
invalid-settings fallback, XIAO levels, reversed polarity, no enable pin,
reserved/duplicate GPIO rejection, server validation, failed cold-boot sync,
retry throttling, server changes and preservation of the clock during outages.
`tools/test_antenna_ui.js` passes all 12 mode/board/enable combinations. The
existing timezone suite also passes, including autonomous DST transitions,
energy rollovers and concurrent clock reads/settings changes.

The antenna template was rendered in a 390-pixel-wide headless Edge viewport
with the firmware stylesheet. Unmanaged, XIAO and Advanced states were visually
checked; the form has no horizontal overflow and remains valid with the enable
pin disabled. XIAO wiring and its photo link were checked against Seeed's docs.
Physical antenna switching, reception quality, and synchronization against a
real private NTP server remain hardware checks. No device was flashed.

This document records evidence for the current source tree. It is not a claim
that every supported inverter model or control path has been field-tested.

## Reproducible build environment

Verified on 2026-08-15 with:

- Arduino CLI and Espressif Arduino core 3.3.8;
- target `esp32:esp32:esp32c6`;
- Zigbee mode `default` (ZBOSS disabled);
- the included 8 MB dual-OTA layout and 4 MB USB-only alternative;
- ArduinoJson 7.4.2, PubSubClient 2.8, NTPClient 3.2.1, Time 1.6.1 and
  PSACrypto 1.1.1; and
- ESP Async WebServer 3.12.0 and AsyncTCP 3.5.0.

Final v1.4.10 sizes for both layouts:

```text
8 MB application: 1,487,638 bytes
4 MB application: 1,487,638 bytes
Global variables: 89,008 bytes (238,672 bytes free)
8 MB OTA application slot: 3,145,728 bytes
8 MB OTA application-slot margin: 1,658,090 bytes
4 MB factory application slot: 3,538,944 bytes
4 MB application-slot margin: 2,051,306 bytes
```

GitHub Actions explicitly copies `partitions-8mb-ota.csv` or
`partitions-4mb-noota.csv` before compiling, so changing the repository default
to 8 MB does not make the 4 MB artifact ambiguous. Both variants were also
compiled in a clean Ubuntu environment after applying the documented
`sunMoon`/Time 1.6.1 header compatibility adjustment used by the workflow.

## Live hardware evidence

Testing used an 8 MB ESP32-C6 with Wi-Fi active and three plaintext DS3
inverters in range. It verified:

- raw channel-16 receive while serving the web UI;
- proprietary APsystems pairing reply parsing;
- safe unique-peer inference when an inverter omits its pairing-ID announcement;
- persistence of PAN and short radio address across reboot;
- two inverters on one PAN plus a third on another PAN;
- transient Wi-Fi/802.15.4 coexistence retry;
- one jittered retry when contention interrupts a fragmented reply;
- APS fragment acknowledgements, 105-byte reassembly and telemetry decode;
- firmware versions `5.456`, `5.307` and `5.456` from the three units;
- repeated 45-second fleet polling with the web and Modbus services active;
- first-post-reboot energy baselining without a false power spike or duplicate
  production; and
- v1.4.x OTA installation with NVS/SPIFFS configuration preserved;
- lossless download of a 96-byte, two-record history journal with attachment
  headers;
- rejection of malformed restore input and an incorrect wipe confirmation with
  HTTP 400 while preserving the journal; and
- successful restore of that native journal with identical before/after
  SHA-256 and unchanged daily totals;
- v1.4.4 administrator-password validation rejecting an incorrect current
  password, mismatched confirmation and shared role passwords without changing
  stored credentials; and
- the read-only `user` account receiving the dashboard while being rejected
  from the administrator menu;
- retrieval of a 12,196-byte ESP-IDF flash coredump from a field watchdog
  failure and identification of the stalled `async_tcp` task; and
- 656 authenticated API requests in 45 seconds while a deliberately
  non-reading WebSocket client applied backpressure, with all three inverters
  continuing to poll and at least 151 KB of free heap remaining.

The boot-time AES known-answer test passes the published reverse-engineered
vector. Hardware validation of an encrypted inverter remains open. A deliberate
successful wipe was not performed on the field device; its confirmation guard
and non-destructive rejection path were tested instead.
