# QT2 experimental monitoring and capture

QT2 monitoring is rebased on `main` at `17d8a79` (v1.4.19), including its
fleet polling, Home Assistant, settings restore and power-limit fixes. The
implementation uses the evidence in [TobiasTTM's PR #1](https://github.com/leaberry/ESP32C6-APsystems-ECU/pull/1). ESP32-C5 support remains outside this PR.

TobiasTTM supplied the QT2 idea, initial field mapping and scale
factors, eleven annotated captures, subsequent reference measurements, and reported
testing two QT2 units on an ESP32-C5. The original capture text is preserved in
[`tools/fixtures/qt2.json`](tools/fixtures/qt2.json), from PR commit
`53b3c8f6a5d0ee4e06a343917d392c6de619bf6a`. These are contributor observations;
this integration has not yet been run against a physical QT2.

## What the dump establishes

Nine records are telemetry and two are configuration replies. Every record is
109 bytes in the old dump: 105 bytes of serial plus L2 data, followed by four
legacy ZNP source/radio/FCS bytes. The native transport supplies the 105-byte
ASDU without that trailer. The telemetry signature is `FB FB 5C BB BB 30` at
offset 6; configuration replies instead start `FB FB 5C DD DE 01`.

All eleven records pass the same checksum: the big-endian value at offsets
101–102 equals the sum of bytes 8–100. `FE FE` ends L2 at offsets 103–104.
The decoder checks exact ASDU length, hexadecimal syntax, configured serial,
telemetry signature, terminator and checksum before updating electrical telemetry or
energy state. Same-sized configuration replies, incomplete frames, other
serials and other models are rejected. Unrecognized variants are retained in
the diagnostic capture for later analysis.

Offsets below are **bytes from the start of the serial**, not hex characters.
All multi-byte values are big-endian.

| Offset | Field | Current interpretation | Evidence and remaining uncertainty |
| --- | --- | --- | --- |
| 20–25 | Status | Preserved raw only | Changes during the contributor's mains-off/startup tests; individual bits are not established. |
| 26, 28 | Two DC voltages | Divide by 26.3; shared by inputs 1/2 and 3/4 | The October 25 reference measurements agree within 0.1 V over 20–50 V. Sharing/order follows the contributor's mapping; paired raw frames are still needed. |
| 30, 32, 34, 36 | Four DC currents | Divide by 89 A, provisional | Loaded capture decodes to 8.90 A/0.15 A/5.81 A/0.20 A; annotation says roughly 10 A and 6 A. October reference readings are about 6–7.5% low at 4–10 A. Raw frames and input ordering still need confirmation. |
| 38 | Inverter clock | Unsigned 16-bit seconds, provisional | First two captures advance 585 to 645. Actual host timing was not supplied. New logs include a separate monotonic clock for comparison. |
| 40, 42, 44 | Three AC voltages | Divide by 10 V; AC readings 1/2/3 with unverified reference and order | First capture: 234.0/235.6/236.7 V. October pair-injection tests suggest line-to-line voltage divided by sqrt(3), not established phase-to-neutral measurements. See below. |
| 48 | Temperature | Divide by 100 degrees C | Produces plausible values, e.g. 19.51 degrees C; negative-temperature encoding is untested. |
| 50 | Frequency | Divide by 100 Hz | First capture is 50.03 Hz; both mains-off captures report zero. 60 Hz operation is untested. |
| 70, 74, 78, 82 | Four energy counters | Unsigned 32-bit; divide by 31,600 Wh, provisional | Scale is from the PR. The dump alone cannot establish Wh calibration. Production uses counter differences divided by inverter elapsed time. |

The initial poll establishes a baseline without adding historic production.
Duplicate/backwards clocks (including reboot/wrap) rebaseline; backwards
energy counters never subtract energy or create negative power. As with the
existing decoders, intervals crossing those discontinuities are not credited.
The two mains-off captures have zero frequency but their counters still rise
by a combined 2,710 units over 120 inverter seconds (about 2.6 W with the
provisional scale). Their exact physical meaning therefore needs checking.
Zero-frequency intervals and the first interval after grid recovery establish
a baseline without crediting AC production; raw counters are still captured.
The existing float energy representation also limits precision at high counter
values. Raw unsigned counters are preserved exactly in the diagnostic export.

## October 25 reference measurements

[TobiasTTM's comment](https://github.com/leaberry/ESP32C6-APsystems-ECU/pull/7#issuecomment-6398461882)
reports these lab-supply and multimeter comparisons:

| Reference | QT2 DC voltage | Reference | QT2 DC current |
| --- | --- | --- | --- |
| 20.0 V | 20.0 V | 2.0 A | 2.0 A |
| 30.0 V | 29.9 V | 4.0 A | 3.7 A |
| 40.0 V | 39.9 V | 6.0 A | 5.6 A |
| 50.0 V | 50.0 V | 8.0 A | 7.5 A |
| | | 10.0 A | 9.3 A |

These results support the existing DC voltage divisor. Current reads low at
higher loads, but changing its divisor from these rounded readings alone would
be premature. The comment does not include matching raw frames or the exact
firmware/scales used. Keep `/89` provisional until those are available.

The reporter also applied 235 VAC between each pair of conductors, with these
results under the previous L1/L2/L3 labels:

| Energized pair | AC reading 1 | AC reading 2 | AC reading 3 |
| --- | --- | --- | --- |
| L1–L2 | 66.8 V | 66.7 V | 135.7 V |
| L1–L3 | 134.4 V | 68.6 V | 68.1 V |
| L2–L3 | 68.4 V | 135.2 V | 67.7 V |

235 / sqrt(3) is 135.68 V; half is 67.84 V. These results suggest that the
three fields measure line-to-line voltage divided by sqrt(3), with field order
L1–L3, L2–L3, L1–L2. This is an inference, not a confirmed register definition.
The third conductor's electrical state and matching raw captures are missing.
The [APsystems cable page](https://latam.apsystems.com/en/portfolio-item/3-phase-bus-cable-4mm%C2%B2-h07-5-5m/)
identifies brown=L1, black=L2 and grey=L3, but does not define telemetry order.
The reporter subsequently clarified that he used the connector pin labels;
inside his older units the wires are red, black and green. Cable color alone
is therefore not a reliable phase identifier for these tests.

Agreement within 2 V against a house energy meter on a balanced supply does
not distinguish phase-to-neutral voltage from line-to-line voltage divided
by sqrt(3). The old phase-to-neutral labels are therefore removed. Reported
values and scales remain unchanged; no inferred 400 V values are published.

## October 25 diagnostic follow-up

[The later comment](https://github.com/leaberry/ESP32C6-APsystems-ECU/pull/7#issuecomment-6403601776)
confirms that these older QT2 units have three AC conductors, L1/L2/L3, with
PE connected to the frame and no neutral. This supports removing the previous
phase-to-neutral claim, but does not establish whether the internal reference
is a computed or virtual neutral. The inferred line-pair mapping remains open.
The reporter also switched physical units behind the test configuration while
looking for intact fuses; future comparisons need the actual inverter serial.

The attached [capture](https://github.com/user-attachments/files/33938928/qt2-capture.json)
is JSONL despite its `.json` extension. It contains one export header and
512 consecutive timeout records, 128 per inverter, with no received ASDUs
or valid telemetry. Sequences 11843–12354 span 21:21:24–21:51:39 local time
(UTC+01:00), or 30 minutes 15 seconds. All records use one boot ID and firmware
`ESP32C6-ECU_v1_4_13-qt2-dev`. Storage reports ready and zero dropped records;
normal ring overwrites are not counted as drops. Earlier contents are unavailable.
This attachment supplies no raw voltage, current, frequency or energy values.

The [diagnostic report](https://github.com/user-attachments/files/33938971/aps-ecu-diagnostics.txt)
shows four configured QT2s, a 12-second requested polling interval, and repeated
successful transmissions followed by receive timeouts late in the evening.
That does not establish why the inverters were silent; sleep, connectivity and
configuration cannot be distinguished from these records. Reset reason 4 is
an ESP-IDF panic reset, and a 12,164-byte crash dump is stored. Diagnosing that
requires the downloaded crash dump and the ELF from the exact build that
crashed. The report alone does not identify a fault in the QT2 decoder.

This exposed a capture-retention problem: repeated timeouts can consume every
slot. Starting with `ESP32C6-ECU_v1_4_19-qt2-dev2`, each inverter records only
the first timeout in an uninterrupted silent run. A received frame, changed
serial or ECU restart allows another timeout record. Suppressed repeats are
counted in the export header, independently of storage/queue drops. Failed
storage attempts remain eligible for retry. Polling cadence and retries are
unchanged, and received frames still follow normal bounded-ring retention.
No voltage/current calibration changes follow from this attachment.

## Integration and compatibility

- Select QT2 (experimental monitoring) in inverter settings. All four PV
  inputs remain selectable after saving, reopening or restoring settings.
- The details page shows AC readings 1/2/3 and four input cards. Daily voltage
  statistics use reading 1. The voltage reference and phase order are unverified.
- HTTP keeps numeric `acv` and adds `acv0`, `acv1`, `acv2` in wire order.
  `acv` equals `acv0`. `phase_count: 3` describes the inverter, while
  `ac_voltage_reference: "unverified"` and `ac_phase_order: "unverified"`
  describe the readings. `throttle_supported` is false.
- MQTT formats 3 and 4 retain `acv`, add `acv0/1/2` in that same unverified
  order, and include all four input channels. Formats 1 and 2 retain their schema.
- Home Assistant identifies QT2, exposes all enabled inputs, and labels the
  three AC readings as unverified. Its power-limit entity is removed for QT2;
  incoming and queued commands are rejected before changing the target.
- Per-inverter SunSpec uses Model 103 for QT2, with unchanged register offsets.
  Both line-to-line and phase-to-neutral voltages, along with AC current, use
  not-implemented sentinels. Aggregate Model 101 also leaves AC voltage/current
  unavailable when the fleet includes QT2. Power, frequency, temperature and
  energy remain available, subject to the provisional calibration above.
- The fleet polling collector accepts validated QT2 telemetry alongside the
  existing models and captures rejected QT2 frames before discarding them.
- QT2 output limiting and grid-profile writes remain disabled in the UI,
  command handlers and underlying send/query functions. A captured configuration
  reply does not establish a working control command or its scaling.

## Automatic diagnostic capture

Capture starts automatically when a configured QT2 is polled. No console
tracing setting is needed. Normal polling is unchanged and no extra control
commands are sent. Download **Diagnostics → Download QT2 capture**, or use
the link on the QT2 details page. The admin-authenticated endpoint is
`/diagnostics/qt2`; its attachment is `qt2-capture.jsonl`.

The log contains post-decryption, reassembled ASDUs (including rejected
telemetry and information/configuration replies), the first timeout of each
silent run, serial,
firmware versions, source/cluster, RSSI/LQI, connected-input mask, boot ID,
64-bit uptime and local clock/UTC offset. A zero local epoch means the clock
was not synchronized. Valid polls also include `ac_readings_v` in wire order, unverified voltage
reference/order metadata, decoded DC voltage/current,
frequency, temperature, inverter clock and **exact raw energy counters**.
Raw unknown status/reserved fields remain in `asdu_hex`. It cannot retain
fragments that never reassemble or encrypted data that fails decryption;
poll timeouts and the existing radio diagnostics help identify those cases.

Storage is a checksummed circular file, written after synchronous inverter
transactions. It retains 512 records on the 8 MB layout (about 205 KiB), or
128 records on the 4 MB layout (about 51 KiB). At two QT2s polled every five
minutes, 512 records cover roughly 21 hours; retries, info replies and faster
polling shorten that window. Repeated timeouts from an uninterrupted silent
run do not consume additional slots. Oldest records are overwritten. Download after
each useful test session. Records survive restart, with a boot ID distinguishing
clock resets. No file is created on a fleet without QT2 traffic.

Allocation leaves at least 96 KiB free for settings/history. If space or writes
fail, telemetry continues and the export reports `storage_ready` and
`dropped_since_boot`. The header also reports `suppressed_timeouts_since_boot`
and `timeout_policy: "first_until_rx_or_serial_change"`. Timeout suppression
is RAM-only and starts fresh at ECU restart. The four-entry pending queue
bounds RAM use. An abrupt
power loss may lose queued/in-progress records; checksums reject damaged
records at restart/export. A slow download can encounter an overwritten slot;
that is reported as `missing_or_overwritten`, rather than returning mixed data.
The JSONL export streams one record at a time. Logs stay on the ECU until an
administrator downloads them; they contain serial numbers, but no account,
Wi-Fi or MQTT credentials.

## What to collect next

1. Pair/select QT2, enable normal polling, and let it record startup, steady
   production and naturally varying light. Record which physical PV input is
   enabled and the inverter firmware version.
2. At known times, note independent DC voltage/current per connected input,
   line-to-line voltages and inverter AC output power. Record any existing
   phase-to-neutral reference separately, including its measurement point;
   these older QT2 units have no neutral connection. Identify phases by connector
   pin, record any floating conductor, and match meter timestamps to raw captures.
   Record the reference
   instrument and its units; a reading on an MPPT pair must be identified as
   a pair total. These references cannot be inferred from the radio dump.
3. For energy calibration, record an independent energy meter at the start
   and end of a steady interval, with timestamps. Download the capture for
   the same interval. Include natural restart/overnight behavior when available.
4. Download while useful daytime readings are still retained. Before sharing,
   check for `event: "rx"`, nonempty `asdu_hex`, and `valid_telemetry: true`;
   an all-timeout export cannot calibrate measurements. Share the JSONL and
   timestamped reference readings for analysis.
   Polls provide both device and host timing, allowing validation of the clock
   unit independently of the energy conversion. No throttle test is required.

## Repeatable validation

```sh
python3 tools/test_qt2.py
python3 tools/test_qt2_capture.py
node tools/test_qt2_ui.js
python3 tools/test_znp_pairing.py
python3 tools/test_timezones.py
```

The QT2 suites compile the production C++ with AddressSanitizer and
UndefinedBehaviorSanitizer. They exercise captured frames, malformed/truncated
input, energy discontinuities, legacy telemetry/MQTT, SunSpec registers,
control refusal, queue overflow, fixed-size rotation, restart recovery,
every-byte record corruption, write failure and low storage. The filesystem
tests simulate storage; real SPIFFS power-loss behavior and QT2 hardware
accuracy still need field testing.
