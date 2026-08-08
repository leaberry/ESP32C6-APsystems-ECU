# ESP32-C6 ECU for APsystems inverters

This is a standalone ESP32-C6 port of
[patience4711/ESP32-read-APS-inverters](https://github.com/patience4711/ESP32-read-APS-inverters).
It replaces the external CC2530/CC2531 and TI ZNP firmware with the C6's
integrated IEEE 802.15.4 radio and Espressif Zigbee stack. No Zigbee UART or
reset wiring is needed.

The project includes:

- YC600, QS1 and DS3 pairing, polling and telemetry decoding
- automatic plaintext or APsystems AES application transport per inverter
- cooperative fleet polling, defaulting to 300 seconds and configurable in seconds
- Wi-Fi web UI, HTTP API, MQTT, scheduling and power limiting
- inverter model and firmware-version query (`0xDC`)
- read-only SunSpec Modbus/TCP on port 502
- daily and current-hour energy history
- cautious OpenAPS-compatible grid-protection profile apply and restore
- one source tree for 4 MB USB-only and 8 MB OTA-capable boards

> Hardware status: both flash variants compile successfully. Development did
> not have a physical APsystems inverter, so pairing, encrypted transport,
> firmware information and protection writes still require real-hardware
> validation. Start with one nearby inverter and see [LIMITATIONS.md](LIMITATIONS.md).

## Flash-size choices

| Board flash | Application layout | Web OTA | SPIFFS |
|---|---|---:|---:|
| 4 MB | one 3.375 MB factory image | no; reflash over USB | 488 KB |
| 8 MB | two 3 MB OTA slots | yes | about 1.85 MB |

The default `partitions.csv` is the 4 MB USB-only layout. It is identical to
`partitions-4mb-noota.csv`. For an 8 MB board, replace `partitions.csv` with
the contents of `partitions-8mb-ota.csv` before compiling and select an 8 MB
flash size in the board menu. The application code is otherwise identical.

Do not flash the 8 MB merged image onto a 4 MB module. A generic ESP32-C6 board
is suitable when it has enough flash, exposes a usable USB/programming path,
and the Arduino core supports its flash and pin configuration.

## Build and upload with Arduino IDE

1. Install Arduino IDE 2.x.
2. Add this Boards Manager URL in Preferences:
   `https://espressif.github.io/arduino-esp32/package_esp32_index.json`
3. Install **esp32 by Espressif Systems 3.3.8**.
4. Install these libraries: ArduinoJson, ESP Async WebServer by ESP32Async,
   AsyncTCP by ESP32Async, PubSubClient, NTPClient, Time, sunMoon and PSACrypto.
5. Open `ESP32-read-APS-inverters.ino` and select the specific C6 board, or **ESP32C6 Dev
   Module** for a generic module.
6. Select **Zigbee Mode: ZCZR (coordinator/router)**, **Partition Scheme:
   Custom**, and the actual 4 MB or 8 MB flash size.
7. For 8 MB OTA, first replace `partitions.csv` with
   `partitions-8mb-ota.csv`. Keep the default file for a 4 MB build.
8. Enable **USB CDC On Boot** when using native USB, select the port and Upload.
9. Open Serial Monitor at 115200 baud and complete the Wi-Fi setup portal.

If upload does not start, hold BOOT, tap RESET, release BOOT and retry. Exact
button behavior depends on the board.

### Windows quick start with ESP-IDF tools

On a Windows system with Espressif's ESP-IDF 5.5 tools installed under
`C:\Espressif`, open the **ESP-IDF 5.5 PowerShell** desktop shortcut, change to
this repository, and use:

```powershell
.\tools\Flash-Firmware.ps1 -Variant 8MB -Port COM7
.\tools\Serial-Monitor.ps1 -Port COM7
```

Omit `-Port` when the C6 is the only COM device. The flash helper erases the
board by default, which is appropriate for its first installation. Add
`-SkipErase` only for a same-layout replacement when retaining settings is
intentional. The monitor exits with `Ctrl+]`.

For source-level debugging through the C6's built-in USB-JTAG interface, connect
the board's native USB/JTAG port and run:

```powershell
.\tools\Debug-ESP32C6.ps1
```

This starts OpenOCD with `board/esp32c6-builtin.cfg` and opens the C6 RISC-V
GDB using the packaged 8 MB ELF symbols. Boards with separate UART and native
USB connectors must use the native USB connector for JTAG debugging.

### Flash a supplied merged image

Put the C6 into download mode and use the merged image matching its flash size:

```text
esptool --chip esp32c6 --port COM7 erase-flash
esptool --chip esp32c6 --port COM7 write-flash 0x0 firmware/ESP32C6_ECU-4MB-noOTA.merged.bin
```

Use `ESP32C6_ECU-8MB-OTA.merged.bin` for an 8 MB board. Replace `COM7` with the
actual port. A merged image is intended for first installation and can clear
settings, inverter configuration and energy history. For a later 4 MB
same-layout USB update, flash only `ESP32C6_ECU-4MB-noOTA.bin` at `0x10000`.
For an installed 8 MB build, upload `ESP32C6_ECU-8MB-OTA.bin` through its web
OTA page so the inactive slot is selected safely.

## Polling and bus arbitration

The polling interval is configured in seconds on the Basic configuration page.
Its default is 300 seconds. The firmware enforces a dynamic minimum of three
seconds per configured inverter and never less than five seconds. One inverter
is polled at a time, operator actions run between transactions, stale Zigbee
receive data is cleared, and consecutive sends are separated by 250 ms.

The Modbus server is independent and only reads the last completed telemetry
snapshot. A persistent Home Assistant TCP session therefore does not issue
Zigbee requests or contend for the inverter bus.

## Firmware version

After successful telemetry, the firmware sends APsystems L2 command `0xDC`.
It understands the three reply layouts implemented by OpenAPS, including
two-component and three-component software versions. A failed query is retried
on up to three successful poll rounds. The result appears on the home and
inverter-detail pages and in SunSpec Common Model 1 `Vr` for each inverter.

## Energy history

Energy deltas from decoded telemetry are accumulated per inverter:

- the current day's 24 hourly buckets stay in RAM;
- one finalized record per day is appended to `/energy-days.bin` in SPIFFS;
- the recorded-energy total is reconstructed from that journal at boot;
- the web API `get.Data?Energy=N` returns the current date, today, recorded total
  and 24 hourly values for inverter index `N`.

This minimizes flash wear to one logical append per day. The total begins when
this firmware is installed; it is not the inverter's factory lifetime counter.
At 48 bytes per day, the 4 MB layout's energy journal has decades of capacity.
Erasing flash or changing partition layouts erases the history unless it is
backed up first.

## Grid-protection profiles

The Grid profile page accepts OpenAPS `invdriver.gridprofile/v1` JSON. This
changes utility protection settings such as trip voltage, frequency and timing;
it does **not** update the inverter's executable firmware.

Safety behavior is deliberate:

- applies to one selected inverter only; there is no broadcast write;
- supports DS3 model codes 20/21/22/36 and QS1 codes 08/18;
- reads the current values and writes a per-inverter backup first;
- only writes parameters returned by that inverter and supported by its encoder;
- enforces the profile's range and additional physical bounds;
- reads every intended value back and reports verification failure;
- restore also performs a full value-by-value read-back check.

YC600 protection writes remain disabled because the available OpenAPS codec does
not provide a verified YC600 encoder path. Incorrect protection values can make
an inverter disconnect or violate local interconnection rules. Use only a profile
required by the utility and keep the manufacturer's commissioning path available.

## SunSpec and Home Assistant

The read-only server implements Modbus functions 03 and 04 on TCP port 502.
Unit 1 is the fleet aggregate; units 2 through 10 map to inverter indexes 0
through 8. It exposes Common Model 1 and single-phase Inverter Model 101.

For `CJNE/ha-sunspec`, enter the C6's IP address, port 502 and unit 1. Add unit
2 onward for individual inverter devices. See [SUNSPEC.md](SUNSPEC.md).

## Architecture and repository files

The original path was:

`application -> TI ZNP over UART -> CC2530/CC2531 -> Zigbee APS`

This port is:

`application -> compatibility adapter -> Espressif raw APS -> C6 radio`

`ZIGBEE_A_TRANSPORT.ino` parses the legacy `AF_DATA_REQUEST` calls and renders
received raw APS frames in the legacy in-memory format, preserving the upstream
APsystems protocol and decoders. Key additions are:

- `APS_CRYPTO.ino` - plaintext/AES application transport
- `POLL_SCHEDULER.ino` - configurable cooperative polling
- `INVERTER_INFO.ino` - model and software-version query
- `ENERGY_HISTORY.ino` - RAM hourly and daily flash journal
- `GRID_PROFILE.ino` - guarded profile read/apply/restore
- `SUNSPEC_MODBUS.ino` and `SUNSPEC.md` - Modbus/TCP service and map
- `SECURITY-AUDIT.md` - CC2530 firmware and security findings

## Publish to GitHub

This directory is a Git repository with the original project retained as the
`upstream` remote. Create an empty GitHub repository, then run:

```text
git remote add origin https://github.com/YOUR-NAME/YOUR-REPOSITORY.git
git push -u origin main
```

## License

The application is derived from patience4711's MIT-licensed project; see [LICENSE](LICENSE).
Espressif's Zigbee stack and installed libraries retain their own licenses.
