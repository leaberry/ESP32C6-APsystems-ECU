# DS3-H plaintext telemetry test

Version: `ESP32C6-ECU_v1_4_16-probe3`. Experimental; pairing is not fixed.

The previous test received two valid plaintext firmware replies (5.354) from
this DS3-H on FFFF. Two production plaintext DS3s also run 5.456, as reported
by the owner. Neither version nor serial alone establishes encryption mode.
This suite tests whether the known plaintext path also supports power readings.
It replaces the broad encryption matrix behind **Run encrypted tests**.

## Install and collect logs

1. From the successful GitHub Actions run, download artifact
   **ESP32C6-APsystems-ECU-8mb-ota**, unzip it, and install
   **ESP32C6_ECU-8MB-OTA.bin** through **Firmware update** on the existing
   8 MB OTA installation. Do not install the `.merged.bin` file.
2. Check **System information** for the version above. Keep the other ECU off,
   test in daylight, and wait for the working inverters to finish a poll round.
3. Enable **persistent flight recorder** under **Polling and access**, then save.
4. Open the saved DS3-H and click **Run encrypted tests** once. The button keeps
   its existing name, but this build runs plaintext tests. Do not click
   **Pair inverter** first: run the test in its current state.
5. Allow five minutes. Polling pauses during the test. Whether the result is
   **Tests finished** or **Tests incomplete**, click **Download encrypted test log**.
   Save it before restarting or running another test; this capture is held in RAM.
6. Wait for a normal polling round after the test. From **Diagnostic snapshot**,
   download the **report**, **poll log**, and **flight recorder**. Put these and
   the test log into a ZIP. Include the time/time zone, result message, and
   whether the working inverters resumed polling. The DS3-H remaining unpaired
   is expected; tests do not save pairing or publish production readings.
7. Turn the recorder off and save. After downloading everything, **Clear recorded
   logs** can free its flash storage.

If discovery fails, save that capture before trying once more. Save each run
separately, and mention any pairing attempt or restart between runs.

## Experiments and interpretation

Up to three discovery windows (two requests each) require a fresh matching
serial/address on FFFF. Conflicting sources abort. Each subsequent phase sends
two requests. Firmware queries wait 3.5 seconds each; telemetry waits 6 seconds
each to capture slower or fragmented replies. The suite takes about 92 seconds
with immediate discovery and a control inverter, or 106 seconds with all retries.

| Phase | Request | Network and delivery |
| --- | --- | --- |
| 0-2 | Fresh discovery, stop retrying once found | FFFF broadcast |
| 3 | Known working inverter firmware control | Its saved network, direct |
| 4 | Target firmware DC | FFFF direct |
| 5 | Target telemetry BB | FFFF direct |
| 6 | Target firmware DC | FFFF direct |
| 7 | Target telemetry BB | FFFF broadcast |
| 8 | Target firmware DC | FFFF direct |
| 9 | Target telemetry BB | Operating network, direct |
| 10 | Target firmware DC | FFFF direct |
| 11 | Target discovery recheck | FFFF broadcast |
| 12 | Known working inverter firmware control | Its saved network, direct |

Only the existing read-only DC and BB payloads are used. No network assignment,
commit, power limit, grid setting, or encryption-disable commands are sent.
No-ACK is recorded but never suppresses the receive window. Raw packets and
assembled replies are retained even when their layout is unknown; only target
network/source replies enter diagnostic reassembly. Broadcast request echoes
are not treated as target responses. The operating-network address remains a
hypothesis and silence there cannot establish that the inverter is absent.

A valid target telemetry reply would establish a candidate plaintext read path,
not successful pairing or permission to use FFFF as a permanent production
network. If DC works but BB remains silent, focus on setup/command requirements.
Do not change the global encryption default from a firmware-info response alone.
Late packets can cross phase boundaries; use packet contents and timestamps.
`info_decoded=0` is normal for telemetry and is not a decode-failure verdict.

The existing bounded, streamed download and fragment ACK handling are retained.
Host tests verify request bytes, phase order, PAN/address selection, delayed and
missing discovery, no-ACK receive windows, state isolation, and log export.
Real DS3-H telemetry and post-test fleet polling require the reporter's test.
