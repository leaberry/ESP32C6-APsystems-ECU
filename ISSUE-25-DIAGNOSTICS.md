# Issue #25 diagnostic experiments

Build: `ESP32C6-ECU_v1_4_16-pair-exp1`.

## Run the test

1. Install the application image matching the existing flash layout. For an
   existing 8 MB OTA installation, use the application `.bin` on the firmware
   update page. Do not use `.merged.bin` for an upgrade. The 4 MB layout needs
   an application-only USB update; see [README.md](README.md#upgrade-without-losing-settings-or-data).
2. Check **System information** for the version above.
3. Open **Menu > Polling and access**, enable **persistent flight recorder**,
   and save.
4. Open the DS3-H inverter and click **Pair** once. Allow up to five minutes.
   Polling pauses while pairing runs. Keep the ECU powered and wait for the
   result; do not start another pairing attempt.
5. Open **Menu > Diagnostic snapshot**. Download the **pairing log**, **report**,
   **poll log**, and **flight recorder**, whether pairing passed or failed.
   Download before restarting or pairing again: detailed experiment packets
   are kept in RAM and will otherwise be lost.
6. If pairing succeeds, let it poll in daylight for five minutes. Download
   another report and poll log and note whether DS3-H readings appeared.
7. Turn the flight recorder off and save. After downloading, use **Clear
   recorded logs** on **Diagnostic snapshot** to free recorder flash space.

## What this build tests

With the recorder enabled, serials with `2` as their second digit run up to
three sequential trials. Other serials, or recorder-off operation, use only
existing pairing.

| Experiment | Change |
| --- | --- |
| 0 | Existing four-command sequence and operating-network verification. |
| 1 | Repeat final APS command 0101 three times, one second apart, then wait ten seconds on FFFF before switching networks. |
| 2 | Same as experiment 1, with `0001` appended to the 010F payload and its length increased from 17 to 19 bytes. |

Experiments 1 and 2 are hypotheses suggested by the original ECU's host
prepare/commit commands. Its modem-to-radio mapping is unknown; these are not
proven equivalents. AES framing is unchanged. Trials share inverter state, so
later success does not by itself establish which change caused it.

The suite stops at the first verified result or radio error. A fresh matching
reply on the operating network (or separately verified saved network) is still
required. Discovery-only replies, cached peers, and unknown announcements do
not establish success. Storage failures cannot trigger another trial or report
success. Previous local pairing is retained on failure.

## Logs and limits

The pairing log and report label each experiment, outcome before storage, and
phase. The persistent pairing audit records final storage/result. Phases 0-3
are handshake, 4 is operating-network settling, 5-7 operating verification,
8-10 saved-network verification, 12-13 repeated final commands, and 14 the
extra FFFF wait.

Each phase keeps counts plus its first two and latest two target-related
samples: time, requested/observed PAN, source addresses, cluster, rejection
reason, signal quality, opaque status bytes, and up to 64 raw packet bytes.
Truncation and omitted samples are reported. Downloads can contain device
identifiers. No AES keys are added to the log.

The bounded capture uses RAM and adds no recorder flash files or writes. It
survives completion until another pairing or restart. Unrelated traffic cannot
fill target-sample slots. The separate existing raw trace can overflow without
stopping reply matching.

Host replay verifies exact trial payloads, gating, all-trial failure, early
success, transmit/storage failure, restoration, and retained samples. Actual
DS3-H pairing and encrypted polling still require hardware testing.
