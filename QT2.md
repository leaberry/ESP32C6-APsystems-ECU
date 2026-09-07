# QT2 experimental monitoring and capture

QT2 monitoring is implemented on top of main at `9e05b25` (v1.4.12), including
the recent pairing, inverter-management and timezone fixes. This is a fresh
integration of the evidence in [TobiasTTM's PR #1](https://github.com/leaberry/ESP32C6-APsystems-ECU/pull/1),
not a merge of that branch's Wi-Fi or ESP32-C5 changes.

**Credit:** TobiasTTM supplied the QT2 idea, initial field mapping and scale
factors, eleven annotated captures, and reported testing two QT2 units on an
ESP32-C5. The original capture text is preserved in
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
| 26, 28 | Two DC voltages | Divide by 26.3; shared by inputs 1/2 and 3/4 | The loaded capture gives about 43.2 V and 39.6 V, consistent with its rough 43 V/39 V annotation. Sharing/order follows the contributor's mapping. |
| 30, 32, 34, 36 | Four DC currents | Divide by 89 A, provisional | Loaded capture decodes to 8.90 A/0.15 A/5.81 A/0.20 A; annotation says roughly 10 A and 6 A. Independent calibration and input ordering still need confirmation. |
| 38 | Inverter clock | Unsigned 16-bit seconds, provisional | First two captures advance 585 to 645. Actual host timing was not supplied. New logs include a separate monotonic clock for comparison. |
| 40, 42, 44 | Three AC voltages | Divide by 10 V, interpreted as phase-to-neutral | First capture: 234.0/235.6/236.7 V. Mains-off captures: about 1.7–1.8 V. Consistent with the 400 V three-phase annotation; phase order needs checking. |
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

## Integration and compatibility

- Select **QT2 (experimental monitoring)** in inverter settings. All four PV
  inputs remain selectable after saving and reopening the form.
- The details page shows L1, L2 and L3 voltage and four input cards.
- HTTP keeps the existing numeric `acv` property, meaning L1 for QT2. QT2 adds
  `acv0` (L1), `acv1` (L2), `acv2` (L3), `phase_count: 3`, and
  `throttle_supported: false`. Other models keep their existing fields.
- MQTT formats 3 and 4 include those three phase fields and all four input
  channels; existing `acv` remains. Formats 1 and 2 retain their schema. Buffers
  accommodate a complete four-input message including large energy totals.
- Per-inverter SunSpec units use
  [Model 103](https://github.com/sunspec/models/blob/master/json/model_103.json)
  for QT2 with three phase voltages. The register layout and scaling stay
  compatible with the existing map. AC current and line-to-line voltage are
  unavailable: the captures do not establish those measurements. The aggregate
  ECU unit remains Model 101; when it contains QT2, AC current is unavailable
  and its representative voltage still averages L1 from online inverters.
- Daily voltage statistics use L1. Power, daily energy and lifetime energy
  use all enabled inputs, with the provisional energy scale above.
- QT2 output limiting and grid-profile writes are disabled. The HTTP handler,
  console, serial interface and underlying send/query functions refuse QT2
  throttle requests; hiding the form is not the only guard. A configuration
  reply in the dump does not prove a working control command or its scaling.

## Automatic diagnostic capture

Capture starts automatically when a configured QT2 is polled. No console
tracing setting is needed. Normal polling is unchanged and no extra control
commands are sent. Download **Diagnostics → Download QT2 capture**, or use
the link on the QT2 details page. The admin-authenticated endpoint is
`/diagnostics/qt2`; its attachment is `qt2-capture.jsonl`.

The log contains post-decryption, reassembled ASDUs (including rejected
telemetry and information/configuration replies), poll timeouts, serial,
firmware versions, source/cluster, RSSI/LQI, connected-input mask, boot ID,
64-bit uptime and local clock/UTC offset. A zero local epoch means the clock
was not synchronized. Valid polls also include decoded voltage/current,
frequency, temperature, inverter clock and **exact raw energy counters**.
Raw unknown status/reserved fields remain in `asdu_hex`. It cannot retain
fragments that never reassemble or encrypted data that fails decryption;
poll timeouts and the existing radio diagnostics help identify those cases.

Storage is a checksummed circular file, written after synchronous inverter
transactions. It retains 512 records on the 8 MB layout (about 205 KiB), or
128 records on the 4 MB layout (about 51 KiB). At two QT2s polled every five
minutes, 512 records cover roughly 21 hours; retries, info replies and faster
polling shorten that window. Oldest records are overwritten. Download after
each useful test session. Records survive restart, with a boot ID distinguishing
clock resets. No file is created on a fleet without QT2 traffic.

Allocation leaves at least 96 KiB free for settings/history. If space or writes
fail, telemetry continues and the export reports `storage_ready` and
`dropped_since_boot`. The four-entry pending queue bounds RAM use. An abrupt
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
   L1/L2/L3 voltage, and inverter AC output power. Record the reference
   instrument and its units; a reading on an MPPT pair must be identified as
   a pair total. These references cannot be inferred from the radio dump.
3. For energy calibration, record an independent energy meter at the start
   and end of a steady interval, with timestamps. Download the capture for
   the same interval. Include natural restart/overnight behavior when available.
4. Share the downloaded JSONL and those reference readings for analysis.
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
