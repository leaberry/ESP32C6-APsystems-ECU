# USB-only QS1 capture

Diagnostic version: `ESP32C6-ECU_v1_4_20-qs1-usb6`.

This build listens on Zigbee channel 16 and sends the recording to a computer
through USB. Wi-Fi and Bluetooth stay off. There is no web page. It never pairs,
polls, or acknowledges an inverter. It does not mount the filesystem or change
saved ECU settings, inverter records, or production history. Capture data stays
in RAM until sent to the computer. Install normal firmware again to use the
board as an ECU; a restart alone does not restore normal operation.

The first test is to confirm that we can record replies while the older
controller continues getting fresh readings. Do not pair anything again for
this test. The listener cannot see every Zigbee channel at once, and software
drop counts cannot measure packets missed over the air.

## Install on an existing ECU

Use `qs1-usb6-4mb-noota.zip` for the reporter's 4 MB XIAO. The 8 MB package is
only for an 8 MB board with the matching partition layout. These packages
contain application images, not merged first-install images.

Follow the [application-only USB upgrade procedure](README.md#usb-update-either-board-size-while-keeping-its-data),
including comparison of the installed partition table with the supplied table.
Do not erase the board or write the supplied partition table for this test.

After confirming the partition tables match, open PowerShell in the extracted
4 MB package folder. Replace `COM7` with the XIAO's USB port:

```powershell
py -m esptool --chip esp32c6 --port COM7 --baud 460800 write-flash 0x10000 ESP32C6_ECU-4MB-noOTA.bin
```

For 8 MB, follow the linked procedure, including both OTA slots. Do not use the
4 MB command on that layout. No board is flashed by the capture script.

## Record 15 minutes

1. Test during daylight. With the XIAO unplugged, confirm that the older
   controller gets a new reading from both QS1s. Note the time.
2. Connect the XIAO's external antenna, then connect its USB port to the computer
   with a data cable. Put it where it can hear the inverters and older controller,
   with some space between the two boards. Close other serial-monitor windows.
3. In PowerShell, still in the extracted package folder, run:

   ```powershell
   py -m pip install pyserial
   py capture_usb.py --port COM7 --antenna xiao
   ```

4. Check that the script says the firmware was verified and Wi-Fi and Bluetooth
   are off. Leave it running for 15 minutes. Keep the computer awake and the USB
   cable connected. The older controller should continue polling normally.
5. During the recording, check whether the older controller still gets new
   readings from both QS1s. Save its Console text or a short screen recording
   showing a successful poll. Record any time that readings stop or resume.
6. When the script says `Finished`, send the `aps-usb-capture-...csv` file created
   in that folder, the Console output, and whether both inverters kept updating.
   Send the CSV even if it has no packets or reports dropped packets.

`--antenna xiao` selects the XIAO external antenna for this boot only. Do not
use it on another board. For another supported board, use `--antenna saved`
to apply its previously configured antenna settings. The script reports the
selected mode and the file records the GPIO settings. It does not change NVS.

If the port is unknown, omit `--port COM7`; the script lists ports and asks which
one to use. A board can acquire a different port after flashing or reconnecting.

Press Ctrl+C once to stop early and save the remaining packets. A second Ctrl+C
exits immediately. If USB disconnects or the board restarts, the file is marked
incomplete. Keep that file and start a new recording after reconnecting; files
are never overwritten. The listener stops receiving if USB stalls for ten
seconds, disconnects, or the 15-minute limit is reached.

The script supports Python 3 on Windows, Linux and macOS. On Linux/macOS, use
`python3` instead of `py` and the appropriate serial port, such as `/dev/ttyACM0`.

## Capture format and checks

The first line starts with `# aps-passive-v1` and identifies the firmware,
channel, antenna, transport and radio state. Packet rows contain board uptime,
channel, RSSI, LQI, raw frame bytes and a sequence number. Raw bytes include the
PHY length and the driver's two FCS slots; those slots are not the on-air CRC.
The final device line reports received/written counts, queue drops, invalid
frames, unexpected TX callbacks, elapsed time and the stop reason. The host
adds a complete/partial marker after checking packet lengths and counters.

A 256-frame queue absorbs short USB delays. A full queue counts new arrivals
as dropped without overwriting queued packets. There is no total packet limit.
The host saves each complete line immediately. The version, radio state,
sequence numbers and final counts are checked before a recording is called
complete. These checks cannot establish lossless radio reception.

Optional conversion for Wireshark:

```powershell
py passive_capture_to_pcap.py aps-usb-capture-YOUR-FILE.csv qs1-traffic.pcap
```

Always send the original CSV, which contains the diagnostic counters.
