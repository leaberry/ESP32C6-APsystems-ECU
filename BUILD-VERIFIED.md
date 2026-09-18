# Build and hardware verification

## Power-limit persistence follow-up (2026-09-18, local validation)

The production Web UI test after PR #19 confirmed both a 100 W/input DS3
command and its 500 W/input normal-maximum revert. Both returned HTTP 200,
redirected to inverter details, and received inverter success acknowledgements.
Low morning output did not permit a physical clipping test. During that test,
settings export continued reporting an unknown limit: live commands wrote to
`my-data`, but startup and settings backup/restore read `my_data`.

All four paths now share `POWER_LIMIT_NAMESPACE` (`my_data`). The live-command
writer uses its own Preferences handle, checks the write size and readback,
and logs `limit save failed` on failure instead of claiming a successful save.
Radio command success and flash persistence remain separate results. Home
Assistant commands remain RAM-only.

Old `my-data` values are intentionally not imported: they contain slot numbers
without serial identity and may be stale after inverter changes or a settings
restore. After installing the fix, explicitly save each desired persistent
limit again in **Inverter details > Output limit > Save limit**, then download
a new settings backup. Existing `my_data` values remain authoritative. Startup
loads the remembered value; this change does not add automatic radio commands
at boot or change the inverter's own persistence behavior.

Host tests execute the production queued action, save helper, startup load
block and backup/restore implementation. They cover every inverter slot,
Web UI/legacy MQTT value boundaries, unknown limits, failure to open/write/read
back NVS, and all 30 interrupted-restore mutation points. HA retained recovery
and control tests and the inverter reply-decoding tests also pass. No new
production firmware installation or power-loss persistence test was performed
for this follow-up. Both 4 MB USB-only and 8 MB dual-OTA builds passed
with ESP32 core 3.3.8; generated flash headers, partition layouts and image fit
were checked.

## Issue #18 power limits and Web UI redirects (2026-09-18)

[Issue #18](https://github.com/leaberry/ESP32C6-APsystems-ECU/issues/18)
reports that a DS3 accepted a 100 W per-input limit (about 200 W total) and
returned to normal output at 500 W, despite an invalid-link page after saving.
This is reporter evidence for that DS3 through the Web UI, not an independent
radio test or an MQTT power-limit test.

The confirmation used `/details?inv=N`; the registered page is
`/inverter-details?inv=N`. The fix also preserves an edited target during live
refresh and allows the same whole-watt targets as Home Assistant.

The wider redirect audit checks server redirects, JavaScript navigation, meta
refresh destinations, saved return paths and same-method route ordering against
both application and captive-portal routes. Telemetry no longer overwrites the
return path; inverter selection retains its required query parameter; the old
journal template uses lowercase `/menu`. Saved return paths are restricted to
pages with valid inverter indices, falling back to the dashboard for obsolete,
incomplete or action URLs.

Legacy/Domoticz MQTT now respects the payload length, rejects non-integer
throttle fields and rejects the index equal to the inverter count. Its topic,
JSON command shape, 20–700 W range and existing persistence path are preserved.
Home Assistant retains its separate namespace, 20–500 W range and RAM-only
limits; queued commands now recheck MQTT connection/session and night mode
before execution, as well as the existing identity and telemetry checks.

Both 8 MB dual-OTA and 4 MB USB-only firmware builds pass with ESP32 core
3.3.8. The application fits the named partition in each generated image; flash
size headers and the one-/two-application partition layouts were verified.
The redirect audit checks 67 destinations against 48 application GET routes
and seven captive-portal GET/ANY routes.

Host regression coverage uses the production form/confirmation code, return-URL
validator, MQTT callbacks, HA control loop and power-limit reply decoder. Tests
cover form routing, edited-input preservation, malformed/truncated MQTT input,
index/value boundaries, reconnects, unavailable inverters and confirmed/failed
HA commands. These tests do not establish physical output, broker timing or
flash persistence on a device. No firmware was deployed during this review.

## Issue #17 reporter verification (2026-09-16, v1.4.14)

[The reporter's follow-up](https://github.com/leaberry/ESP32C6-APsystems-ECU/issues/17#issuecomment-5701304771)
confirms native Home Assistant MQTT operation in their test installation after
switching from the legacy format. Their comment spells the release `V1.14.14`;
the discussion concerns v1.4.14.

| Feature | Reported hardware result | Scope of verification |
|---|---|---|
| YC600 pairing and operation | Paired successfully and reported working well | One reporter's YC600; no detailed YC600 telemetry capture or control test supplied |
| Multiple DS3s in Home Assistant | Both DS3s appear directly as separate devices with readings | Native HA MQTT discovery and separate telemetry in this installation |
| DS3 sensor display | Screenshot shows solar power/energy, temperature, AC voltage, frequency and both panel powers for each DS3 | Display and separation confirmed; not a calibrated accuracy test or Energy dashboard statistics validation |
| HA power-limit entity | Present for both DS3s, with unknown values | Discovery of the control only; physical throttling was explicitly not tested because of cloudy weather |
| Existing Energy dashboard history migration | Planned by reporter | Preservation of their previous two years of history has not been confirmed |

The [attached screenshot](https://github.com/user-attachments/files/32299564/mqtt_issue_v1.14.14.pdf)
shows HA solar power of 16 W and 31 W alongside ECU readings of 15.6 W and
31.1 W. The per-panel readings also agree to HA's displayed rounding. This
supports correct separation of the two DS3s in native HA mode; it does not
establish a change or fix to legacy format 2.

These are attributed field results, not independent reproduction. Broker
restart/persistence recovery, simultaneous HA/Domoticz load, physical power
limits and long-term Energy statistics remain unverified by this report. The
earlier dated test records below retain their original scope.

## Issue #11 reporter verification (2026-09-13, v1.4.14)

[The reporter's test results](https://github.com/leaberry/ESP32C6-APsystems-ECU/issues/11#issuecomment-5653722474) confirm the following on their installation:

| Feature | Reported hardware result | Scope of verification |
|---|---|---|
| Custom NTP server | Verified: works | Their custom server; no claim about every server or failure mode |
| Home Assistant MQTT | Verified: works | Basic HA MQTT integration; individual sensors, Energy statistics, throttling and recovery were not described |
| Antenna switching | Verified: works | Reporter measured about +20 dB with the external antenna in their setup; advanced GPIO configurations and other boards remain unverified |
| Hostname change | Verified: works | Their network; this does not establish every DHCP/DNS/cache behavior |
| Settings backup/restore | Not tested by reporter | Explicitly excluded from their tests; same-board restore and replacement-board pairing continuity remain open |
| Energy history restore | Restore accepted, today's display empty | Consistent with the implemented behavior below; the comment does not verify restored completed-day records |

These are attributed field results, not independent reproduction. They update
the earlier dated hardware-test gaps below without extending them to features
the reporter did not describe.

### Current-day restore code review

- `energySendHistoryBackup()` sends only the finalized journal (or an empty
  file when none exists). It does not export today's RAM or the separate
  shutdown checkpoint.
- `energyRestoreUploadFinish()` validates and replaces the journal, removes
  the current-day checkpoint, then calls `energyHistoryBegin()`. That function
  resets today's totals, fractional energy, hourly buckets and daily statistics
  before loading the restored journal. A same-board saved checkpoint therefore
  cannot recover today after an explicit history restore.
- A zero-record backup passes the record-size and CRC validation loops. Restore
  can legitimately report success with no finished days and an empty today.
- The web success notice already says current-day RAM counters were reset.
  Success means the uploaded journal was accepted, not that today's data was
  present. This matches the reporter's symptom; it does not by itself prove
  anything about the contents of their backup.
- Settings restore is separate: it leaves history/checkpoint files alone but
  restarts the ECU. The settings JSON does not contain today's data. Normal
  reboot checkpoint recovery is different from importing a history backup.

No production restore was performed during this review. See the README's
[restore instructions](README.md#restore-production-history) for the user-facing
consequences and the earlier native-journal hardware verification below.

## Broker connection test and ECU_ID warning (2026-09-12)

The 4 MB and 8 MB builds compile locally. Host tests exercise the real connection
handlers and worker with success, broker refusals, authentication failures,
unreachable-broker results, request validation, overlapping tests, stale results
and worker-allocation failure. They verify a separate temporary client, saved
password fallback, typed credentials, disconnect cleanup and no settings writes.
UI tests cover success, refusal, busy responses, network errors and timeouts.
The connection test does not publish or subscribe, so it cannot establish topic
permissions. These additions have not been deployed to production hardware.

## Local menu, MQTT and OTA changes (2026-09-12)

Based on merged main. The 4 MB USB-only and 8 MB OTA layouts compile locally
with ESP32 core 3.3.8. All Python host regressions and JavaScript UI checks pass.
The new handler tests cover independent MQTT modes, preserved disabled settings,
validation and save failures, OTA checkpoint failure before any firmware write,
explicit checkpoint opt-out, zero production, authentication, unavailable OTA,
write failures, incomplete uploads and concurrent uploads. CI includes these tests.
Desktop and phone-width browser renders show the reordered menu, combined MQTT
form and OTA checkbox without horizontal overflow.

This change has not been flashed to hardware. Host tests do not establish
power-loss durability or live inverter operation during an OTA upload. The OTA
checkpoint saves daily totals only; hourly charts and production after the
checkpoint remain RAM-only. No periodic flash writes were introduced.

## Local CI fix and documentation review (2026-09-12)

PR12 run [34702769670](https://github.com/leaberry/ESP32C6-APsystems-ECU/actions/runs/34702769670)
and PR13 run [34705072334](https://github.com/leaberry/ESP32C6-APsystems-ECU/actions/runs/34705072334)
both stop in `tools/test_wifi_hostname.py`, before the firmware build. The host
C++ harness uses `uint32_t` but did not include `<cstdint>`. The resulting
`PORTAL_REBOOT_DELAY_MS` error is a consequence of the missing type. The local
GCC 11 standard-library headers had supplied the type indirectly; the GitHub
runner did not. The harness now includes its dependency explicitly. Matrix
fail-fast also cancels the other build after a failing job; cancellation is not
a separate firmware compiler error.

All Python host regressions and the three JavaScript UI checks pass locally.
Firmware source and partition maps are unchanged, so the previous two-variant
build evidence still applies; no new firmware build or board flashing was done
for this edit. At the initial review, these changes were local only. The header
fix was subsequently backported to PR12, and PR13 and the documentation PR
were rebased onto it for GitHub validation. PR12 then passed both firmware
builds and merged. PR13 exposed the same missing `<cstdint>` dependency in
`tools/test_power_limit.py` (`uint8_t`); that harness now also includes the
header explicitly.

README installation/upgrade filenames were checked against release packaging.
Application-only USB addresses and sector-aligned image spans were checked
against both partition CSVs and the built images. The partition comparison
reads 3,072 bytes, matching the generated partition binaries. Local Markdown
links were checked. These are static/documentation checks, not a physical USB
upgrade or power-loss test.

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
detailed Home Assistant discovery and Energy statistics, real inverter throttling,
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

No board was flashed during that initial review. The Issue #11 report above
now confirms the hostname change on the reporter's network. DHCP packet details
and other routers' lease/DNS/cache behavior remain unverified. No mDNS service
was added.

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
At that initial review, physical antenna switching, reception quality and a
real private NTP server had not been tested. The Issue #11 report above now
confirms those functions on the reporter's setup. Advanced GPIO/polarity choices
and other boards remain unverified. No device was flashed during this review.

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
