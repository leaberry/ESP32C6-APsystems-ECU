# Polling diagnostics

Polling diagnostics are included in the firmware. They are off by default and
use the existing flight recorder switch. A saved choice to enable recording is
preserved across firmware updates.

## Collect logs

1. Open **Menu > Polling and access**.
2. Check **Enable persistent flight recorder** and save.
3. Keep your usual poll interval. Let the ECU run in daylight for 30–60 minutes,
   preferably until the problem happens. Write down when it happened.
4. Open **Menu > Diagnostic snapshot**. Download **Download poll log**,
   **Download report**, and **Download flight recorder**. If the ECU crashed,
   also download **Download crash dump** and keep the matching firmware ELF.
5. Send these files with the time of the problem. Then turn the recorder off
   under **Polling and access** and save.

No firmware replacement is needed just to turn recording on. When it is off,
new detailed polling records and counters stop, and there are no regular
recorder flash writes. Saved logs remain available to download, including after
a restart. The normal live diagnostic trace and Wi-Fi recovery remain active.

Completed polling attempts are kept in a 32-record RAM buffer and saved in
minute batches to a 256-record circular flash log (50 KiB). The health recorder
uses about 53 KiB for 720 records: roughly 12 hours, less if Wi-Fi events occur.
Neither file grows indefinitely. A sudden reset can lose the unsaved minute
and the unfinished attempt. Download before restarting when possible.

## Free the recorded-log space

Open **Menu > Diagnostic snapshot** and click **Clear recorded logs**. This
deletes the health log, the current polling log, older polling-log files, and
pending polling records in RAM. Download anything you need first.

The button keeps the recording setting as it was. When recording is off, the
files remain absent and their flash space stays free. When recording is on,
new files are created as recording continues. Settings, pairing records,
production history and crash dumps are kept. A failed deletion is shown on the
page so you can retry. Refresh to see the updated diagnostic snapshot.

## Read the poll log

The current format is **v2**. Each row is one completed broadcast attempt,
including retries. A single attempt may collect several inverters. Inverters
already collected in the fleet round are skipped, so they need not have their
own request row.

- `slot` is the zero-based inverter that prompted the request; `attempt` is 1
  for the first request or 2 for its retry.
- `tx_ok` records transmission success. `result=0` means the requested inverter
  has valid telemetry; `50` means it did not, including a failed transmission.
- `wanted_mask` identifies the configured group on this PAN. `reply_mask`
  identifies serials seen by the reader, even if their payload was rejected.
  `accepted_mask` identifies validated telemetry collected during this attempt;
  `round_mask` also includes successes from earlier attempts in the round.
  Masks are hexadecimal, with bit 0 representing inverter slot 0.
- `boot_id` identifies the boot/recording session. Clearing logs starts a new
  session. `local_epoch` is local wall time, not UTC. `storage=ram` means the
  record has not yet been saved. Only a serial suffix is recorded.

Counters cover all traffic in the attempt window, not just the requested
inverter. `target_rx` matches its learned PAN/source; `target_asdu` counts its
identified replies. `raw_drop` and `app_drop` identify queue overflow;
`fragment_miss`, `fragment_evict` and `ack_fail` describe reassembly problems.
`cca_busy`, `coexist` and `tx_fail` describe transmission trouble.

`stale` counts frames started before the poll round; `unexpected` counts other
PANs, endpoints, clusters or identities. `bad_length`, `bad_kind`,
`bad_checksum` and `bad_value` explain telemetry rejection. `duplicate` counts
already accepted inverters, and `accepted` counts newly decoded samples.
`decrypt_fail` counts undecodable payloads. `ignored` applies to the strict
control reader; it does not mean a valid fleet reply was discarded.

The enabled-since-boot totals also include traffic between attempts. Pending
RAM overwrites and storage errors are reported explicitly. Downloads never
cause a flash flush. Raw payloads, passwords and keys are not included in the
poll log. The regular report contains configured identities.

The v2 companion uses a separate file from the earlier issue #24 v1 format;
v1 records are not converted. Download old logs before upgrading from that
experimental build. **Clear recorded logs** also removes its old file.
Instrumentation and flash writes can affect timing. Logs cannot establish what
happened to radio frames that the ECU never received.

For encrypted DS3-H pairing investigations, see
[the targeted pairing capture guide](ISSUE-25-DIAGNOSTICS.md).
