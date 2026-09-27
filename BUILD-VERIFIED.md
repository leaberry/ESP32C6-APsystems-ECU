# Build and hardware verification

## Issue #25: discovery startup correction (2026-09-27)

`ESP32C6-ECU_v1_4_16-probe5` tests a working inverter before target discovery,
checks both PANs independently, and captures normal pairing's initial 020D/FFFF
command if the target is absent on both. A target already on the operating
network bypasses assignments. The final working-inverter control is attempted
even after failure. Tests cover silence until bootstrap, permanent silence,
operating-only presence, bootstrap failure, conflict, and capture/restoration.
The capture is 49,120 bytes on the host, under a compile-time 50,000-byte bound.
Hardware validation is pending; see [current instructions](ISSUE-25-PROBE5.md).

## Issue #25: staged assignment/commit investigation (2026-09-27)

`ESP32C6-ECU_v1_4_16-probe4` retains plaintext telemetry controls and adds
staged prepare, commit, and directed-PAN candidates. Fresh operating-network
discovery stops writes and enables plaintext/native/A1 read tests. Host checks
cover each migration point, changed addresses, no-ACK continuation, conflicting
sources, lost contact, TX failures, paired-target rejection, restoration, and
bounded streamed capture. Hardware outcomes are not established by these tests.
See [reporter instructions and evidence](ISSUE-25-PROBE4.md).

## Issue #25: plaintext telemetry probes (2026-09-26)

`ESP32C6-ECU_v1_4_16-probe3` replaces the encryption matrix with known DC
controls around plaintext BB telemetry requests. Discovery has three bounded
windows. Tests assert exact production poll bytes, address/PAN selection,
receive timing, delayed discovery, no-ACK continuation, and streamed capture.
Both named layouts are checked by the PR workflow with ESP32 core 3.3.8.
Hardware behavior remains unverified; see [test instructions](ISSUE-25-PROBE3.md).

## Issue #25: read-only encryption and delivery probes (2026-09-25)

`ESP32C6-ECU_v1_4_16-probe2` provides **Run encrypted tests** separately from
pairing. Fresh discovery is required before testing six DC query envelopes on
two PANs, with native/A1 broadcast comparisons and a known plaintext control.
Each phase repeats twice. Diagnostic replies are captured by address, including
real relayed replies and fragmented ASDUs, without peer or telemetry writes.
The suite restores the operating network and never marks the inverter paired.

A dedicated authenticated download streams the bounded capture one line at a
time. Capture stays in RAM until the next test or reboot. The older pairing
export now includes three recent audits to reduce temporary allocation. The
on-flash audit format is unchanged. See [instructions and interpretation](ISSUE-25-PROBE2.md).

All Python/C++ CI checks pass, including actual probe orchestration, key and
envelope construction, fresh/conflicting discovery, missing ACKs, both delivery
modes, strict source/PAN filtering, relayed replies, diagnostic APS reassembly,
no route/telemetry writes, normal/fragmented ACK format, restoration, and
small-chunk immutable exports. Pairing-status and recorded-log UI checks pass.
The host crypto primitive is a test double; hardware AES interoperability is
not established by these tests. Production AES and its self-test are unchanged.

Both named layouts compile with ESP32 core 3.3.8: 1,634,606 program bytes and
158,720 global RAM bytes. Packet buffers account for increased RAM use; the
new download does not construct a full report String. No hardware deployment,
successful encrypted reply or DS3-H pairing is claimed.

## Issue #25: bounded pairing experiments (2026-09-25)

`ESP32C6-ECU_v1_4_16-pair-exp1` adds three sequential, recorder-gated
trials for serials whose second digit is 2. It retains strict fresh-reply
verification and stops on verified results or radio failures. Per-trial
bounded RAM samples include raw target-related frames. No new flash log is
created. See [test instructions](ISSUE-25-DIAGNOSTICS.md).

All Python/C++ checks listed in CI pass, including the expanded actual pairing
orchestration replay. Pairing-status and recorded-log UI checks pass. Replay
checks exact payloads, timing/PAN selection, disabled/nonencrypted gating,
early success, all-trial failure, TX failure, storage failure and sample bounds.
Both pinned ESP32 core 3.3.8 layouts compile: 1,623,154 program bytes and
126,048 global RAM bytes. The application fits both named partition layouts.
No hardware deployment or DS3-H success is claimed. Experiments 1/2 remain
unverified hypotheses about the original modem's radio behavior.

## Issue #25: DS3-H pairing format and targeted diagnostics (2026-09-24)

The `pair-diag1` candidate recognizes the observed 13-byte FF0E direct pairing
reply alongside the existing 8-byte format. It still requires a fresh
serial-matched operating-PAN reply and successful persistence; discovery-only
contact cannot overwrite an existing pairing. The unknown reversed-serial
announcement is not treated as confirmation.

Opt-in, bounded RAM diagnostics now keep per-phase frame/rejection counts and
first/latest target-related samples, with time, routing headers, reply length,
status bytes and match/reject reason. They are included in the authenticated
pairing-log and diagnostic-report downloads. No additional flash writes or
changes to the persistent pairing-audit layout are introduced. Download after
pairing and before restarting or another attempt. See
[issue #25 capture instructions](ISSUE-25-DIAGNOSTICS.md).

Captured-envelope and orchestration tests pass, including legacy pairing,
malformed and foreign frames, discovery-only failure, operating-PAN success,
conflicting sources, persistence failure, disabled recording and bounded
per-phase sampling. Hardware confirmation remains pending; this is not a claim
of working DS3-H network migration or AES telemetry.

All Python/C++ and JavaScript checks in CI pass. Both 8 MB OTA and 4 MB
USB layouts compile with ESP32 core 3.3.8: 1,621,664 bytes of compiled
program and 107,856 bytes of global RAM. Firmware identifies as
`ESP32C6-ECU_v1_4_16-pair-diag1`. No hardware deployment has been performed.

## Issue #24: YC600/QS1 checksum regression (2026-09-22)

The reporter's `poll-fix2` logs show four rounds accepting both DS3s and no
YC600s, with 73 checksum rejections. All three YC600s successfully paired.
The crash dump is byte-identical to the previous submission, not new crash
evidence. The new universal additive-checksum requirement was incorrect:
existing YC600 and QS1 samples in `test.ino` have zero trailer bytes despite
nonzero sums. The previous synthetic tests reproduced that same assumption.

`ESP32C6-ECU_v1_4_16-poll-fix3` applies the verified additive checksum only
to DS3 replies. YC600/QS1 retain identity, PAN, endpoint, opcode, envelope,
freshness and required-value validation. No undocumented checksum rule is
imposed on their trailer. Recorder diagnostics and the clear-logs action remain.

Host tests replay the unchanged historical YC600/QS1 payloads through the
production collector and decoder, check decoded frequency, and use their
payloads for mixed-model round tests. These tests failed before the fix and
pass afterward. Malformed legacy messages and corrupt/zero DS3 checksums
remain rejected. Polling reassembly, radio diagnostics, recorder storage and
clearing, event-log bounds and power-control radio regression tests also pass.

Both 8 MB OTA and 4 MB USB layouts passed local builds with ESP32 core
3.3.8 (1,618,438 bytes of compiled program and 105,872 bytes of global RAM).

The reporter subsequently confirmed [several hours without problems](https://github.com/leaberry/ESP32C6-APsystems-ECU/issues/24#issuecomment-5799050245)
and [successful YC600 testing the following day](https://github.com/leaberry/ESP32C6-APsystems-ECU/issues/24#issuecomment-5816870007).
This supplies field confirmation for `poll-fix3` on the affected installation.
The production DS3 observations below apply to the earlier integrated build;
QS1 coverage remains captured-message host testing, not native-radio hardware.

Final review against main retained the opt-in, bounded recorder and its
administrator-only clear action. All Python/C++ and JavaScript checks listed
in CI were rerun successfully. Both firmware layouts passed GitHub builds for
commit `127a9d2`; subsequent review changes only update verification documents.

## Integrated recorder and clear-logs action (2026-09-21)

The issue #24 diagnostic branch is merged with the polling and journal fixes.
The v2 poll log distinguishes seen identities from accepted telemetry, records
PAN-group and round masks, and counts stale, duplicate and invalid replies by
reason. It uses the existing persisted flight-recorder switch, off by default.
Disabled recording creates no new detailed poll records, counter updates or
periodic recorder writes; old logs remain downloadable after restart.

**Diagnostic snapshot > Clear recorded logs** removes the health log, both
poll-log versions and pending poll records. It preserves the enabled setting,
configuration, pairing history, production history and crash partition. Files
stay absent while disabled and are recreated lazily when enabled recording
continues. Health writes, setting changes, downloads and clearing use a
recursive storage mutex; the poll companion retains its own storage lock.
The action requires administrator authentication, allowed remote access and a
POST with the diagnostics action header. Failures are reported to the user.

Host tests cover the integrated collector, radio counters, storage bounds,
disabled operation, reboot downloads, deletion failures, active-attempt
cancellation, clearing both formats, preserved unrelated files, automatic
recording resumption, storage retry pacing and the button's error handling.
Both ESP32 core 3.3.8 layouts passed local builds. With user authorization,
combined commit `3afd51e` was deployed through matching-layout 8 MB OTA. The
1,352 Wh checkpoint (444 / 454 / 454 Wh) restored exactly. All three production
DS3s passed eight complete rounds in a 305-second observation, covering both
the shared-PAN pair and the separate-PAN inverter. The interval remained 45
seconds; maximum observed response gaps were 46 / 48 / 46 seconds.
There were no missed cycles or unexpected resets. Final output was
474.9 / 486.7 / 482.5 W, with daily totals 484 / 495 / 495 Wh. Free heap ended
at 152,380 bytes.

Every exported setting, ECU identity, inverter order and learned peer was
preserved. Finalized history (1,968 bytes) and the existing crash dump matched
the backups byte-for-byte. Web UI, settings validation, NTP/hostname and
SunSpec Modbus checks passed. The v2 log endpoint and clear button are available;
recording stayed disabled and the poll log remained empty. Production logs
were not cleared. Enabled recording and deletion are host-tested, not exercised
on production hardware. No pairing or power-limit command was sent. Previous
firmware and backups are retained for rollback. YC600 verification still needs
the reporter's hardware.

## Issue #24: polling replies and journal corruption (2026-09-21)

Built from main at `def929e`. The reporter's diagnostics showed YC600 replies
arriving during other inverters' poll transactions. Fleet polling now collects
validated telemetry from every configured responder on the current PAN, accepts
one sample per inverter per round, and skips already successful targets. A late
recovery counts toward the final round result; missing inverters remain failed.
Single-inverter manual polls and control-response matching stay isolated.

Telemetry acceptance checks the serial, PAN, cluster/endpoints, model reply
opcode, envelope length, DS3 checksum and required fields (see the correction
above for YC600/QS1). Receive timestamps
survive raw queues and fragment reassembly, preventing pre-round queued data
or partial responses from counting as fresh telemetry. Reassembly has room for
all nine supported inverters. Existing energy accounting and MQTT payloads are
preserved.

The crash dump's overwritten pointer matched the bytes from a long throttle
message written into the final journal slot. Journal messages now have bounded
copies and room for 63 characters. Rendering uses an escaped, dynamically sized
string instead of fixed row/page buffers.

Host regression tests exercise the production collector, energy decoder,
round handling, fragment parser, journal writer and renderer. Coverage includes
out-of-order and duplicate replies, malformed/control frames, stale fragments,
different PANs, late recovery, transmit failures, all nine simultaneous fragment
sessions, and the exact last-slot message that corrupted memory. Existing
power-control addressing and reply tests also pass. Both pinned ESP32 core
3.3.8 firmware layouts are built locally.

With user authorization, the 8 MB application from commit `e8b8c4e` was
subsequently installed on the production ECU using same-layout OTA. The saved
707 Wh daily checkpoint restored exactly. All three DS3s resumed polling at the
unchanged 45-second interval, including two sharing a PAN and one on another
PAN. A 301-second observation recorded seven successful complete fleet rounds,
with no missed inverter cycles or unexpected reset. Maximum observed response
gaps were 49 / 51 / 45 seconds. Final power was 354.1 / 363.7 / 361.3 W, and
current-day totals increased to 273 / 279 / 279 Wh. Free heap finished at
160,324 bytes.

ECU identity, inverter order, learned peers and every exported setting were
preserved. The 1,968-byte finalized history and previous crash dump were
byte-for-byte unchanged. Settings validation, Web UI and SunSpec Modbus reads
passed. No pairing or power-limit command was sent. The previous firmware and
pre-update backups were retained for rollback; hourly RAM statistics restart
with reboot as documented.

This verifies the DS3 polling regression on production hardware. YC600 radio
reliability still needs confirmation on the reporter's installation.

## Power-limit confirmation follow-up (2026-09-19)

The decoder now skips intermediate control acknowledgments and waits for the
model-specific limit readback, with bounded time and frame counts. It rejects
malformed hexadecimal fields, missing frame terminators and mismatched limits.
The command sequence clears old queued replies before transmission; standalone
queries also clear old replies and stop on transmit failure. Confirmed Web UI
and Domoticz limits then use the existing verified NVS persistence helper;
Home Assistant remains RAM-only. Poll timing and energy-derived power averaging
are unchanged.

Host tests replay the actual `06DE02...` acknowledgment followed by DS3 limit
readbacks for both 20 W and restoration to 500 W. They also cover ACK-only
streams, malformed/truncated replies and real limit mismatches. Radio addressing,
Web UI form, MQTT commands and persistence/interrupted-restore tests pass.
Both ESP32 core 3.3.8 builds (4 MB and 8 MB) passed. The 8 MB application was
deployed with user authorization, preserving settings, pairing, history and
the 720 Wh pre-reboot checkpoint. On production DS3 hardware, Web UI set/revert
commands both logged success. The 20 W/input request was confirmed in actual
radio readback, RAM and settings export; output settled at 42 W while the
controls remained at 441.1/438.1 W with unchanged physical limits. Restoration
to 500 W/input was confirmed in all three places; output recovered to 432.8 W
while the controls produced 445.3/441.7 W. All exported settings were
unchanged apart from replacing the target's unknown saved limit with its
confirmed normal maximum. MQTT remained disabled; its control paths were
host-tested. Persistence was checked through NVS-backed settings export and
host startup/restore tests, not a further production power cycle.


## Directed power-limit commands (2026-09-19, local validation)

Issue #18 reported that limiting one DS3 also limited another on the same PAN.
The native transport used broadcast MAC, NWK and APS delivery even when the
legacy command named one inverter. Power writes and their readback queries now
use the selected inverter's learned PAN/source, unicast delivery and MAC ACKs.
Missing, invalid or ambiguous peers fail without transmitting. Encryption and
transmission failures are returned to the shared power-control caller.
Web UI, Domoticz and Home Assistant all use that caller.

`tools/test_power_limit_radio.py` runs the actual command builder, ZNP parser and
raw frame builder with captured transmissions. It checks YC600/QS1/DS3 commands,
three peers on one PAN, all three frames in the write/readback sequence, learned
addresses, rejected peers, encryption and transmission failures, and unchanged
polling/pairing broadcasts. Radio regression, reply-decoder and pairing-parser
host tests pass, as do the Web UI form, HA/Domoticz command, pairing-path and
settings-backup suites. Both pinned-core 3.3.8 firmware layouts are built locally.

The local 8 MB build was subsequently deployed with user authorization on
2026-09-19. A Web UI test set one plaintext DS3 to 20 W/input: output settled
at 42.7 W total while the two controls produced 244.4 W and 239.4 W. Direct
radio queries confirmed only the target changed; the same-PAN control kept
its original 500 W/input limit and the separate-PAN control kept 475 W/input.
The target was restored to its original 500 W/input, verified by radio readback.
Both form submissions returned HTTP 200 and navigated back to inverter details.

The first test also exposed a confirmation bug: the power-limit decoder
consumed the intermediate `06DE02...` reply as a DS3 readback and reported zero,
so the caller marked the operation failed and saved an unknown limit (-1),
even though the inverter applied the requested setting. A later directed query
returned the correct setting. The confirmation follow-up above subsequently
fixed and verified confirmation/persistence for this sequence. YC600/QS1 and
live MQTT control were not exercised on hardware. The v1.4.15 low-output test had not established
isolation.

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
