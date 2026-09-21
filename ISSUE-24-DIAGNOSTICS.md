# YC600 polling diagnostic firmware

Version: `ESP32C6-ECU_v1_4_16-poll-diag1`.

This build collects evidence for [issue #24](https://github.com/leaberry/ESP32C6-APsystems-ECU/issues/24).
It preserves v1.4.16 polling, retry, decoding, power control and Home Assistant
availability behavior. It is not a verified fix for lost YC600 replies.

## Reporter instructions

1. Before updating, open **Menu > Diagnostic snapshot** and download the existing
   report, flight recorder and crash dump, if present. Keep the original
   v1.4.16 ELF with that crash dump; the diagnostic build's ELF cannot decode
   an older firmware crash reliably. Save settings and production-history
   backups as described in the [upgrade instructions](README.md#upgrade-without-losing-settings-or-data).
2. On the reported 8 MB board with the existing 8 MB OTA layout, use
   **Menu > Firmware update** to install
   `ESP32C6_ECU-8MB-OTA-poll-diag1.bin`. Leave **Save current day totals from ram**
   checked and select **Install firmware**, then **Restart ECU** when offered.
   Use the application image, not a merged image or partition image.
   For a 4 MB board, follow the existing matching-layout USB upgrade procedure
   using `ESP32C6_ECU-4MB-noOTA-poll-diag1.bin`; OTA is unavailable there.
3. Verify the diagnostic version in **System information**. Open
   **Polling and access**, select **Enable persistent flight recorder**, and
   save. Keep the five-minute poll interval and normal inverter configuration
   so the results are comparable with the original report.
4. Let it run in daylight for 30-60 minutes, preferably until an HA gap occurs.
   Open **Menu > Diagnostic snapshot** and select **Download poll log**, **Download report**,
   and **Download flight recorder**. Note the wall-clock time of the HA gap.
   Include **Download crash dump** if a new crash occurs.
5. Download before rebooting if possible. Completed attempts remain in RAM
   until the next successful poll-log batch, normally within a minute;
   the download includes both saved and unsaved records. An abrupt reset can
   lose that pending minute and the in-progress attempt. Disable the recorder
   after the capture and save to stop the additional persistent writes.

The poll log retains the newest 256 attempt sequence positions in flash and
32 in RAM. At five inverters every five minutes, continuous enabled recording
covers roughly 2-4 hours, depending on retries. Downloads remain available
when recording is disabled. Attempts made while disabled are RAM-only and
are not saved retroactively. Older flash records may fall outside the download
window as newer RAM-only sequence positions accumulate. No raw payloads,
keys or passwords are added to this log. It identifies a device by slot,
serial suffix and learned radio peer; the regular report contains its full
configured identity.

## Reading the poll log

Each row represents one completed attempt, including successful initial
requests and recovered retries. `attempt=1` is the original request and `2`
is its retry. `result=0` means decoding succeeded; `50` is a receive timeout.
Other results retain the legacy decoder error code. `tx_ok` independently
records the send result. `boot_id` separates uptime clocks across restarts;
`local_epoch` is local wall time, not UTC. `storage=ram` means that record is
not yet confirmed saved. The firmware version is recorded with each row.

Counters in a row cover traffic processed during that attempt, including
other inverters on the PAN. They are not all specific to the requested unit:

| Fields | What they establish |
|---|---|
| `rx`, `target_rx` | Frames processed by the radio worker; target matches the learned PAN/NWK source after basic MAC/NWK checks |
| `raw_drop`, `app_drop` | Raw ISR queue overflow, or complete-ASDU queue overflow |
| `parse_reject` | Frames filtered or rejected by the receive parser; includes unsupported/control traffic, not necessarily malformed target replies |
| `fragment_miss`, `fragment_evict` | Continuation without a session, or replacement of an active reassembly session |
| `ack_fail` | Failed outgoing fragment acknowledgements |
| `cca_busy`, `coexist`, `tx_fail` | Explicit CCA/coexistence rejections, and failed transmissions (requests or ACKs) |
| `delivered`, `ignored`, `target_asdu` | ASDUs queued, other-inverter replies discarded by the reader, and target-identified ASDUs reaching the reader |
| `reply_mask` | Hex bit mask of inverter identities reaching the reader, including ignored units; bit 0 is slot 0 |
| `decrypt_fail` | Encrypted-looking ASDUs rejected by the reader |
| `queue_clear` | Queue depth observed just before explicit clearing; concurrent arrivals can make this an approximation |
| `unfragmented_ack_requested` | Unfragmented incoming frames with APS ACK requested; metadata only, does not change acknowledgement behavior |

The since-boot counters also cover periods outside attempt windows, such as
between-inverter queue clears. `target_rx` and `target_asdu` only increment
while an attempt is active. Pending-buffer overwrite and storage failures
are explicitly reported. The original health recorder format/file is unchanged;
the companion uses a separate checksummed, versioned, fixed-size file.
Flash writes happen in the main loop at the existing recorder cadence, never
per received frame or from the radio task. A download does not trigger a flush.

A timeout with target frames but no target ASDU points toward parsing,
fragmentation or queue processing. A timeout with no target frames, alongside
CCA/coexistence activity and other replies, supports radio contention/loss.
Neither proves what happened to frames the ESP32 never received. There is no
raw sniffer capture in this build. Instrumentation and opt-in flash writes can
affect timing; hardware testing is still required.
