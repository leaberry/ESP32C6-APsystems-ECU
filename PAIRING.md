# Pairing and communication mode

Save the inverter's model and 12-digit serial number, then choose **Pair inverter**.
Keep the ECU powered and allow up to three minutes. Pair during daylight while
the inverter is powered. The result shows the saved network ID and communication
mode: **Plain (not encrypted)** or **AES encrypted**.

For a DS3-family inverter whose serial number defaults to encrypted communication, the ECU
first checks whether it is already on the operating network. If necessary it
attempts network assignment. It then tries encrypted power requests, followed by
plaintext requests if encrypted replies cannot be verified. It saves the pairing
only after two valid power replies and successful restoration of the ECU's radio.
The selected mode survives ECU restarts and settings backup/restore.

If pairing fails, the previous saved ECU pairing is retained. An inverter's
network may have changed during assignment even when the ECU cannot verify it.
Keep other ECUs off while pairing. Existing plaintext DS3 and YC600 units retain
their established pairing procedure.

## Collect useful logs

Detailed pairing recording is optional and off by default. It follows the
**Persistent flight recorder** switch under **Polling and access**. Pairing does
not require recording to be enabled.

1. Enable the flight recorder before choosing **Pair inverter**.
2. Wait for the result. Open **Diagnostic snapshot** and choose **Download pairing
   trace**, **Download pairing log**, and **Download report**.
3. For polling problems, also choose **Download poll log** and **Download flight
   recorder**.
4. Download the pairing trace before restarting or pairing again. It is kept in
   RAM, so restarting clears it. The pairing log contains separate persistent
   attempt summaries.
5. Disable recording when finished. **Clear recorded logs** removes the health
   and poll logs from flash and clears the RAM pairing trace. Saved inverter
   settings, pairing summaries, energy history, and crash dumps are retained.

Downloads include inverter identifiers and raw packets. Review them before
sharing publicly. Only an authenticated administrator can download or clear the
trace. If a trace download is still running, a new pairing can proceed but skips
its optional detailed recording; finish downloading before starting another
attempt when a fresh trace is needed.

## Evidence and remaining limits

[Issue #25](https://github.com/leaberry/ESP32C6-APsystems-ECU/issues/25#issuecomment-5886929541)
confirmed the experimental predecessor pairing a DS3-H on firmware 5.354 in
plaintext, normal polling alongside five other inverters, and persistence across
an ECU restart. The reporter also said power limiting worked.

That successful attempt found the inverter already on the operating network from
earlier tests. The full fresh-assignment sequence on an untouched unit, successful
AES communication, and the cleaned implementation's hardware behavior require
separate validation. This feature does not establish an AES-disable command or a
firmware downgrade.
