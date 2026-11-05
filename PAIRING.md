# Pairing and communication mode

## Combined QS1 test in diagnostic build 4

`ESP32C6-ECU_v1_4_20-qs1-diag4` runs the QS1 checks in one session and puts the
results in one download. Install the matching application using the
[same-layout upgrade procedure](README.md#usb-update-either-board-size-while-keeping-its-data),
including the partition comparison. Keep the ECU ID, radio identity and inverter
list. DS3 and QT2 keep their existing pairing and polling paths.

1. Test during daylight with the QS1 powered. Turn off the older controller.
   Confirm the version ends in `-qs1-diag4` and the external antenna is connected
   and selected. Restart after changing antenna settings.
2. Choose **Pair inverter** for one QS1, once. Allow up to six minutes. The page
   stays on the pairing screen while the tests run. Do not start passive capture.
3. Open **Diagnostic snapshot** and choose **Download QS1 test bundle**.
4. Attach `aps-qs1-test-bundle.csv` to Issue 39, even if pairing fails. Say which
   QS1 you tested, whether pairing succeeded, and whether power readings appeared
   afterward. No separate report, pairing log, ZIP, flight-recorder setting or
   Wireshark conversion is needed.
5. Save the file before restarting or pairing again. If testing another QS1,
   download the first result before starting its test.

The suite first listens quietly and checks the current network, discovery PAN
`FFFF`, the older controller's `A3D8`, the earlier ESP32 test's `10C4`, and any
different saved peer network. Duplicate networks are skipped. These two fixed
historical PANs are Issue 39 test presets, not automatic pairing destinations.
Version and power reads compare the short network header from the old capture
with the native extended header. They also compare the current controller ID
with the old project's default ID in read requests only. No stored identity is
changed. A freshly identified target receives directed reads; otherwise the
suite sends broadcasts on the current and old-controller networks and records
raw responses without claiming to identify or reassemble the target's reply.

If the target is already found on the operating network, the initial assignment
is skipped. Otherwise the normal diag3 handshake runs. Failed verification is
followed by slower preparation, three commits and a settling period, then the
operating-PAN `020D` variation investigated in Issue 25. Each variation is followed
by fresh filtered verification. Further assignment variations stop on success.

If needed, a complete handshake is repeated with address filtering off, followed
by queries on the current, discovery and historical networks. This observation
pass disables automatic MAC acknowledgements and cannot itself verify pairing.
Filtered verification follows it. After the final application reads, a failed
run briefly listens on channels 11 through 26 without transmitting there. A
quiet channel scan does not prove the inverter is absent. Normal channel 16,
PAN and filtering must be restored before a pairing can be saved.

The RAM bundle contains before/after diagnostic reports, radio IEEE and ECU
identities, antenna settings, the pairing journal, per-stage results and counts,
raw RX/TX samples, and the last assembled response for each directed read.
Transmissions include software APS acknowledgements; automatic hardware MAC
acknowledgements are not captured. Up to 52 stages retain their first three and
latest three events plus the first sample containing the target serial, with
known request echoes excluded from that extra slot. This preserves evidence from
the middle of the run under background traffic. Omitted events/stages, queue drops
and per-channel RX counts are explicit. The allocation is reused for later runs;
downloads block replacement until completion or disconnect.

Raw samples and reports remain bounded. Hardware filtering during normal phases,
RF loss and Wi-Fi coexistence can still hide traffic. Application reads do not
publish telemetry or learn peers. A forwarded request, diagnostic read or packet
seen only during unfiltered observation cannot save a pairing.

The existing converter also accepts the bundle:

```powershell
py tools/passive_capture_to_pcap.py aps-qs1-test-bundle.csv qs1-test.pcap
```

Keep the CSV: the PCAP omits the reports, validation results and assembled replies.

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

`ESP32C6-ECU_v1_4_20-qs1-passive5` listens from startup. It sends no Zigbee
requests or acknowledgements, including after a restart. It preserves the saved
ECU/radio identity and pairing records. Normal ECU operation requires installing
the normal firmware again. Wi-Fi remains active for the capture page.

1. Install the diagnostic application for your board's flash size using the
   [same-layout USB upgrade procedure](README.md#usb-update-either-board-size-while-keeping-its-data).
   Compare partition tables first. Keep the ECU ID and inverter list.
2. During daylight, leave the working older controller running. Confirm that
   it is getting fresh readings from the QS1s. There is no need to pair them again.
3. Put the ESP32 near the inverters, where it can also hear the old controller.
   Attach the antenna. On a XIAO using its external antenna, check the external
   antenna preset, save it and restart if you changed it.
4. Open **Diagnostic snapshot** on a computer and choose **Start 15-minute
   capture**. Keep this page open in the foreground and keep the computer awake.
   Leave the old controller polling normally throughout the recording.
5. When the page says the capture stopped, choose **Download captured packets**.
   Send `aps-passive-15min.csv`, the old controller's log for the same period if
   available, and the serial numbers of the inverters that had fresh readings.
   Include the approximate start time and any connection/drop warnings.
6. Save the file before closing this page or starting another capture. **Stop
   capture** ends a recording early. **Download captured packets** also works
   during recording if you need to save a partial file.

Recording needs no flight-recorder setting and writes no capture data to flash.
The browser receives packets throughout the 15 minutes and holds the file until
download. The board's 256-packet queue is reused as packets are acknowledged by
the browser; it is not a total capture limit. Interrupted web transfers retry
the same packets. A long interruption can fill the queue; further packets are
counted as dropped, and recording continues. Drop counts appear in the page and
file. Closing or refreshing the page loses its unsaved recording. A board restart
ends the capture; save the partial file from the open page before starting again.

The capture includes complete driver-provided frames, receive times, channel,
RSSI and LQI, without filtering by inverter identity, PAN or packet format.
It listens only on channel 16. Wi-Fi shares the radio, so packets can still be
missed; queue drops are reported, but RF/coexistence losses are not counted.
An empty file cannot prove that the inverter sent nothing. The same ESP32
cannot listen passively and run its own pairing attempt at the same time.

For Wireshark, from the repository root run:

```powershell
py tools/passive_capture_to_pcap.py aps-passive-15min.csv qs1-traffic.pcap
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
