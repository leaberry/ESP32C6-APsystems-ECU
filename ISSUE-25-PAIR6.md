# DS3-H pairing with automatic plaintext fallback

Version: `ESP32C6-ECU_v1_4_16-pair6`. This is the next test after probe5.

## Install and test

1. Download **ESP32C6-APsystems-ECU-8mb-ota** from the successful GitHub build.
   Unzip it and install **ESP32C6_ECU-8MB-OTA.bin** through **Firmware update**
   on your existing 8 MB ECU. Use the application file, not `.merged.bin`.
   Check **System information** for the version above.
2. Test in daylight with the other ECU off. Keep your saved inverter settings.
   Let the working inverters finish a normal polling round.
3. Enable **persistent flight recorder** under **Polling and access**, then save.
4. Open the DS3-H and click **Pair inverter** once. Allow five minutes. This
   time, use the normal Pair button. Do not run **Run pairing investigation**.
5. If it pairs, open its details page. Check that **Communication** says
   **Plain (not encrypted)** or **AES encrypted**, and note which one you see.
   Let all inverters run for at least three normal polling rounds. Check for
   power readings and confirm the other inverters still update. The first
   reading may show zero power while the ECU learns its starting counters.
6. Before restarting, download **encrypted test log**, **report**, **poll log**,
   and **flight recorder** from **Diagnostic snapshot**. Do this even if pairing
   fails. The encrypted test log now also records the normal pairing attempt.
7. If pairing and polling worked, restart only the ECU. Do not pair again.
   Check that the DS3-H still shows the same communication mode and resumes
   polling. After three more normal rounds, download another report, poll log,
   and flight recorder. Keep these separate from the first set.
8. ZIP the files and attach them to issue #25. Include the pairing result,
   communication mode, whether all inverters polled before and after the ECU
   restart, and the test time and time zone. If pairing fails, stop after the
   first attempt and send that set of logs.
9. Turn the recorder off and save after collecting the files. **Clear recorded
   logs** frees flash once the downloads are saved.

Pairing works with the recorder off too; enabling it helps diagnose this test.
An inverter power-cycle test can follow once ordinary polling and ECU restart
have been confirmed. This build does not require that extra test yet.

## What changed

Normal pairing for encrypted-default devices first looks for the target on the
operating network, preserving probe5's successful assignment when possible.
If needed, it tries prepare and repeated commit, followed by the directed
020D assignment that preceded the reporter's successful network move.
Conflicting discovery addresses or radio setup failures stop the attempt.

On the operating network, it sends two production-format AES power requests.
If those do not yield two valid encrypted replies, it tries two plaintext power
requests. A fresh serial-matched operating-network discovery and two complete,
matching, validated power replies are required before saving pairing and mode.
Firmware-info replies, discovery alone, missing fragments, and bad checksums
cannot mark this inverter paired. Other transmit failures abort; a missing MAC
ACK alone does not prevent a valid response from proving communication.

The per-inverter mode survives restart, settings edits, and new settings
backups/restores. Old records and old backups retain serial-based automatic
selection. Polling and control commands use the saved mode. A failed attempt
retains the previous local pairing. Other plaintext inverters keep their
existing pairing path. Existing diagnostic tools remain available.

## Evidence and limits

Probe5 received complete plaintext DS3-H power packets on the operating network
after the directed PAN command. Both pass the existing production DS3 validator
and decoder. AES requests had no application replies. Earlier prepare/commit
commands may have contributed, so this build retains that sequence when a new
assignment is needed. It does not establish that directed 020D alone is enough.

Host tests replay those packets and exercise encrypted-first selection,
plaintext fallback, migration, conflict, corrupt/stale replies, radio/storage
failures, and saved modes. Hardware validation of normal pairing, sustained
polling, and restart persistence is still needed. This is not an AES-disable
command or proof of interoperability with inverters that require AES.
