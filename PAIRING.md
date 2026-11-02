# Pairing and communication mode

## QS1 test in diagnostic build 3

`ESP32C6-ECU_v1_4_20-qs1-diag3` changes QS1 pairing to match the packet headers,
command spacing and operating-network repeats seen in the older controller's
capture. It keeps listening on the operating network during the handshake.
DS3 and QT2 retain their existing pairing and polling paths. The fragment-buffer
fix from diag1 remains included. Successful QS1 pairing still needs a field test.

1. Install the application for your board's flash size using the
   [same-layout USB upgrade procedure](README.md#usb-update-either-board-size-while-keeping-its-data),
   including the partition comparison. Keep the ECU ID and inverter list.
2. Confirm the version ends in `-qs1-diag3`. Turn off the older controller.
   Test during daylight with the QS1 powered and the external antenna connected.
   Keep the XIAO's External antenna setting and board preset.
3. Choose **Pair inverter** for one QS1. Allow up to three minutes for the result.
   Do not start passive capture for this test.
4. Open **Diagnostic snapshot** and choose **Download QS1 pairing capture**,
   **Download pairing log**, and **Download report**. Send all three files and
   say which inverter you tested, whether pairing succeeded, and whether power
   readings appeared afterward.
5. Download before restarting or pairing again. If testing the second QS1,
   save the first set of files before repeating these steps.

The QS1 packet capture starts automatically and needs no flight-recorder setting.
It stays in RAM and records full outgoing packets and packets delivered by the
radio, including unmatched replies and network commands. It retains the first
64 and latest 192 events, reports overwritten events and queue drops, and includes
timing, stage, listening PAN, filter mode, signal strength and local TX errors.
Finish any capture download before starting another QS1 pairing attempt.

If normal verification fails, the build briefly listens with address filtering
off and sends two read-only discovery queries. These packets are recorded but
cannot verify pairing or update learned peers. Normal filtered reception must
be restored before a successful pairing can be saved. Radio setup or restoration
failure stops radio operation until restart. Hardware filtering during normal
pairing, RF loss and Wi-Fi coexistence can still hide packets.

The old project's Zigbee modem handled the radio headers and network messages
inside its own firmware. This project builds those packets itself. Comparing
complete packets helps find differences that application logs cannot show.

For Wireshark, from the repository root run:

```powershell
py tools/passive_capture_to_pcap.py aps-qs1-pairing-capture.csv qs1-pairing.pcap
```

Send the original CSV too. It retains stage and TX-result details absent from
the PCAP; a successful local transmission does not prove inverter reception.

## Passive capture in the Issue 39 diagnostic build

`ESP32C6-ECU_v1_4_20-qs1-diag2` can listen while the older controller pairs a
QS1. This is an optional diagnostic mode, not a pairing fix. The existing
pairing sequence and saved ECU/radio identities are unchanged.

1. Install the diagnostic application for your board's flash size using the
   [same-layout USB upgrade procedure](README.md#usb-update-either-board-size-while-keeping-its-data).
   Compare partition tables first. Keep the ECU ID and inverter list.
2. Leave the old controller off initially. On the XIAO, confirm the external
   antenna preset, save it and restart. Open **Diagnostic snapshot**.
3. Choose **Start passive capture**. Wait until the status says `recording`.
   If it stays queued, an existing controller transaction is still finishing.
4. Turn on the old controller and pair one QS1 with that controller. Keep the
   ESP32 close to it and the inverter. Do not select **Pair inverter** on the
   listening ESP32. Note whether the old controller reports success.
5. After that attempt, choose **Stop passive capture**. Wait for the stopped
   status, then choose **Download passive capture**. Attach the downloaded
   `aps-passive-capture.csv` and the old controller's serial log, if available.
   Report which inverter was tested and whether the old controller paired it.
6. Download before restarting or starting another capture. Turn the old
   controller off, then press the ESP32's reset button or power-cycle it to
   resume normal operation.

Recording needs no flight-recorder setting and writes no capture data to flash.
It stores the first 256 packets or 120 seconds, whichever comes first. A full
buffer is reported explicitly. If it fills before pairing starts, stop other
traffic if practical and start another capture immediately before pairing.
The ESP32 remains passive after recording stops: polling and controller work
stay paused, and hardware and software acknowledgements remain disabled until
restart. The web interface stays available for downloads.

The capture includes complete driver-provided frames, receive times, channel,
RSSI and LQI, without filtering by inverter identity, PAN or packet format.
It listens only on channel 16. Wi-Fi shares the radio, so packets can still be
missed; queue drops are reported, but RF/coexistence losses are not counted.
An empty file cannot prove that the inverter sent nothing. The same ESP32
cannot listen passively and run its own pairing attempt at the same time.

For Wireshark, from the repository root run:

```powershell
py tools/passive_capture_to_pcap.py aps-passive-capture.csv qs1-pairing.pcap
```

The PCAP timestamps are board uptime, not wall time. The converter removes the
PHY length byte and driver FCS slots; the original CSV retains every captured
byte plus RSSI and LQI. Attach the CSV even if you also provide the PCAP.

## Normal pairing

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

In `qs1-diag1`, the trace's `rx_total` counts packets that reached the radio
worker during each stage. `related` counts packets matching the target serial
or stage address. `OTHER_RX` retains the first and latest nonmatching packet
across the attempt. Packets rejected by the radio's hardware filter are not
captured. A successful broadcast transmission alone does not prove that an
inverter received it.

The diagnostic report also includes the actual radio channel and transmit
power, saved antenna settings, and antenna GPIO levels. Saved antenna changes
apply after restart. For a XIAO with External selected, the expected levels are
GPIO14 HIGH and GPIO3 LOW. See the [manufacturer's RF switch instructions](https://wiki.seeedstudio.com/xiao_esp32c6_getting_started/#back).

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
