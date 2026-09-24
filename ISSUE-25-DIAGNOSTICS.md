# DS3-H pairing investigation (issue #25)

Candidate: `ESP32C6-ECU_v1_4_16-pair-diag1`, based on main after PR #26.

## What the evidence establishes

The reporter's DS3-H sent direct APS 0x0101 replies containing `FF0E`, its
serial, and five additional status bytes. The old parser required an eight-byte
payload; this observed thirteen-byte variant was rejected. The candidate
recognizes that exact envelope while retaining serial, source, endpoint,
profile, framing and operating-PAN verification. Status bytes are recorded as
opaque values: their meaning has not been established.

The captured direct replies were on the discovery PAN (FFFF). There was no
direct DS3-H response proving migration to the operating PAN. Thus this parser
correction is not proof that pairing or encrypted telemetry will work. The
reverse-serial announcement remains diagnostic evidence only; its unexplained
fields cannot establish pairing. A cached radio peer does not prove pairing.

## Collect the next capture

1. Install the matching-layout application image using the usual firmware
   update page, keeping settings and energy backups as usual.
2. Check **System information** for `ESP32C6-ECU_v1_4_16-pair-diag1`.
3. Enable **persistent flight recorder** under **Polling and access** and save.
4. Try pairing the DS3-H once. Wait for the result.
5. Immediately open **Diagnostic snapshot** and download **pairing log**,
   **report**, **poll log**, and **flight recorder**, before restarting or
   trying another pairing. The report retains the existing raw pairing trace.
6. If pairing succeeds, let it poll in daylight and download a second report
   and poll log. Note the time and whether DS3-H readings appear.
7. Turn the flight recorder off after capturing the issue.

The pairing log and report now include **PAIRING DETAIL**: relative receive
time, pairing phase and requested PAN, observed PAN/source, MAC source,
network header, cluster, lengths, prefix, five extended status bytes,
serial orientation, parser decision, RSSI and LQI. Per-phase counters cover
all received frames, including irrelevant traffic and rejection reasons.
Only target-related samples are retained, with first two and latest two per
phase; explicit omitted counts show when additional samples existed. Samples
are listed by buffer slot, so use their timestamps when ordering the last two.

Detailed pairing capture is bounded RAM storage, enabled only when the flight
recorder was on at pairing start. It survives completion until the next pairing
attempt or reboot, and adds no flash writes. The existing persistent pairing
audit remains available. It contains no new raw packets, full serial numbers,
keys or decrypted telemetry. Unknown announcements are classified rather than
interpreted. Existing raw trace storage is unchanged.

## Validation boundary

Host replay covers the captured extended envelope with an anonymized serial,
legacy replies, malformed lengths and headers, source/serial mismatch,
discovery-only failure, operating-PAN verification, conflicting sources,
storage failures, disabled capture and per-phase sample bounds. The actual
pairing orchestration and diagnostic rendering are exercised as well.
DS3-H pairing migration and AES telemetry still require the reporter's hardware.
