# ESP32-C6 APsystems ECU
An inexpensive, single-board local ECU for YC600, QS1 and DS3 microinverters.
It pairs and polls APsystems inverters directly with the ESP32-C6's integrated
IEEE 802.15.4 radio, then presents production through a web interface, HTTP,
MQTT and read-only SunSpec/Modbus TCP.

This repository began at
[`patience4711/ESP32-read-APS-inverters`](https://github.com/patience4711/ESP32-read-APS-inverters)
commit `7b0ff63`. It retains proven APsystems command builders, telemetry
decoders, MQTT formats and configuration concepts, but it is no longer merely a
hardware port. The external CC2530/CC2531 and TI ZNP architecture was replaced
with a native raw-radio transport, and the Wi-Fi setup, web application,
scheduler, history, diagnostics, OTA/release process, Modbus service, security
handling and many safety checks were substantially refactored or newly built.

This is an independent community project. It is not affiliated with or
endorsed by APsystems, Espressif, the SunSpec Alliance or the upstream projects.
See [ACKNOWLEDGEMENTS.md](ACKNOWLEDGEMENTS.md), [UPSTREAM.md](UPSTREAM.md) and
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) for detailed provenance and
credit.

## What it does

- pairs and polls up to nine YC600, QS1 or DS3 inverters;
- uses the ESP32-C6 radio directly: no Zigbee module, UART wiring or CC25xx
  firmware is required;
- supports plaintext and the reverse-engineered APsystems L1 AES envelope per
  inverter;
- polls the fleet cooperatively, defaults to 300 seconds and enforces a safe
  minimum based on fleet size;
- provides a responsive local web dashboard, diagnostics and configuration;
- queries inverter model and firmware information with APsystems command `0xDC`;
- exposes cached telemetry through HTTP, MQTT and read-only SunSpec/Modbus TCP
  on port 502;
- keeps current-day hourly energy in RAM and appends one finalized daily record
  to flash;
- backs up, validates, restores or deliberately wipes finalized production
  history;
- records RAM-only daily runtime, operating window, peak output, temperature
  range and grid-voltage range per inverter;
- monitors the ESP32-C6 internal die temperature, including low/high values
  and local timestamps since boot on the System information page;
- supports guarded OpenAPS-compatible grid-protection profile read, apply and
  restore operations; and
- builds for 8 MB boards with OTA by default, with a supported 4 MB USB-only
  alternative.

## Hardware status

The native transport has been field-tested with three plaintext DS3 inverters,
including two units on the same PAN and another on a different PAN. Pairing,
two-fragment telemetry, firmware queries, repeated polling, Wi-Fi coexistence,
OTA and the modern web interface have been exercised on an 8 MB ESP32-C6.

YC600, QS1, encrypted inverter transport and grid-protection writes retain
known protocol implementations but still need model-specific hardware testing.
Read [LIMITATIONS.md](LIMITATIONS.md) before using control functions.

## Fastest path: buy, flash, run

### 1. Buy an ESP32-C6 board

The recommended target is an **8 MB ESP32-C6 development board** with:

- 8 MB flash;
- a USB connector that supports flashing;
- a suitable 2.4 GHz antenna; and
- a stable 5 V USB power supply.

The ESP32-C6 is single-core plus a low-power core; that is sufficient because
radio, Wi-Fi, web and Modbus work are event-driven. An 8 MB board is preferred
because it supports safe dual-slot OTA updates and has more history/storage
space. A 4 MB board runs the same application but must be updated over USB.

Keep the ECU reasonably close to the inverters and away from metal enclosures.
The board is normally cool enough without a heatsink, but use a ventilated,
UV- and moisture-resistant enclosure in a hot installation location and keep
it out of direct sunlight.

### 2. Download the firmware

Open the
[latest GitHub Release](https://github.com/leaberry/ESP32C6-APsystems-ECU/releases/latest)
and download:

- `ESP32C6_ECU-8MB-OTA.merged.bin` for the recommended 8 MB board; or
- `ESP32C6_ECU-4MB-noOTA.merged.bin` only for a confirmed 4 MB board.

The merged image is the simplest first-install image and is flashed at address
`0x0`. Firmware binaries are release assets, not files committed to this
repository. Verify `BUILD-INFO.txt` and `SHA256SUMS.txt` when using an Actions
artifact or release bundle.

Do not flash the 8 MB image onto a 4 MB module. First installation or a full
erase removes all previous settings, pairing records and production history.

### 3. Flash over USB

#### Windows graphical flasher

Espressif's official
[Flash Download Tool](https://docs.espressif.com/projects/esp-test-tools/en/latest/esp32c6/production_stage/tools/flash_download_tool.html)
does not require ESP-IDF, Arduino IDE or Python.

1. Connect the board by USB and close any serial monitor using its COM port.
2. Start Flash Download Tool and select **ESP32-C6** and **Develop** mode.
3. Select the downloaded `.merged.bin`, enable its row and enter address `0x0`.
4. Select **DoNotChgBin**, choose the board's COM port and select **START**.
5. If synchronization fails, hold **BOOT**, tap **RESET**, release **BOOT** and
   retry.
6. Reset the board when flashing completes.

#### Standalone esptool

Espressif also publishes a
[standalone esptool executable](https://github.com/espressif/esptool/releases/latest)
for Windows, macOS and Linux:

```text
esptool --chip esp32c6 --port COM7 erase-flash
esptool --chip esp32c6 --port COM7 write-flash 0x0 ESP32C6_ECU-8MB-OTA.merged.bin
```

Replace `COM7` and the filename for your system. Some boards require the same
BOOT/RESET sequence described above.

### 4. Join Wi-Fi

On first boot the ECU creates an open setup access point named
`aps-ecu-xxxxxx`.

1. Connect a phone or computer to that access point.
2. If the captive page does not open, browse to `http://192.168.4.1/`.
3. Enter the 2.4 GHz Wi-Fi SSID and password. Hidden SSIDs can be typed.
4. Choose a DHCP hostname.
5. Use DHCP unless you specifically need a static IPv4 address, netmask and
   gateway. The gateway is also used for DNS in static mode.
6. Set and record an administrator password of 8 to 32 printable, non-space
   characters. A fresh installation initially offers `0000`; do not leave that
   default on an untrusted network.
7. Save. The ECU restarts and requests an address using the selected hostname.

Find the address in the router's DHCP leases and reserve it, or use the static
settings. If the ECU cannot reconnect, it returns to the setup access point.

### 5. Configure time and polling

Open `http://ECU-IP/`, sign in as `admin`, and use **Menu**:

1. **Time and location:** enter signed decimal latitude and longitude, such as
   `39.7392` and `-104.9903`, then select the nearest named time zone. Regional
   zones apply daylight-saving transitions automatically.
2. **Polling and access:** automatic polling defaults to enabled at 300 seconds.
   The minimum is three seconds per configured inverter and never below five
   seconds. Start with the default until communication is proven. The read-only
   SunSpec/Modbus TCP server also defaults to enabled and can be disabled here.
3. **Network:** confirm the hostname, address and Wi-Fi signal.

### Administrator and read-only accounts

The local web server has two fixed usernames with different privileges:

- **`admin`** can open the administration menu, change settings, add or control
  inverters, manage grid profiles and history, view diagnostics, restart the
  ECU and install OTA firmware.
- **`user`** is read-only. It can view the dashboard, inverter details, energy
  history and telemetry APIs, but cannot open administrative pages or submit
  control/configuration actions.

A fresh installation starts with `admin` / `0000` and `user` / `1111` for
compatibility with the original project. Replace both defaults before placing
the ECU on any network that is not completely trusted.

Use **Menu > Polling and access** while signed in as `admin` to change either
password. Changing the administrator password requires the current password
and matching confirmation. New passwords must contain 8 to 32 printable
non-space characters, and the two accounts cannot share a password. The ECU
never displays an existing password. HTTP Basic Authentication is stateless,
so the browser may retry cached old credentials once before prompting after an
administrator-password change.

These accounts protect the web interface only. Modbus/TCP and MQTT use their
own network/service configuration, so keep the ECU on a trusted IoT network and
do not expose it directly to the Internet.

Daylight-aware polling pauses inverter radio traffic outside the calculated
sunrise/sunset window. When Night Mode begins, all per-input and total power
values are set to zero immediately; Modbus then serves zero watts and MQTT
publishes one zero-output update per inverter. Energy counters are preserved,
while non-power telemetry such as the last voltage and temperature remains the
most recent observation. If time or location is invalid, the scheduler
deliberately falls back to 24-hour polling rather than silently stopping.

The application clock uses ESP-IDF's 64-bit monotonic timer behind a
task-safe local-time wrapper. This avoids the 32-bit `millis()` rollover and
cross-task race behavior of the legacy Time library while the web, scheduler
and Modbus tasks are active together.

During that pause the dashboard prominently reports **Night Mode** and the
calculated local time when polling will resume at the next sunrise. The fleet
heading defaults to **APsystems Fleet** and can be changed from its adjacent
edit icon; the custom name is retained across restarts and OTA updates.

### 6. Add and pair each inverter

For each inverter:

1. Open **Menu > Inverters > Add inverter**.
2. Enter the 12-digit serial number printed on the inverter.
3. Select the model, assign a useful name and mark the physically connected PV
   inputs.
4. Leave calibration empty/default unless a verified model-specific correction
   is required. The Domoticz index is used only by the legacy Domoticz MQTT
   format.
5. Select **Save inverter**. Saving must happen first because pairing frames use
   the serial number and the learned address is written to that inverter's
   configuration.
6. Select **Pair inverter** and wait for success.

Pair near the array. Once all inverters are configured, confirm that the
dashboard reports recent poll times, firmware versions and sensible per-input
values. DS3 units show only their two physical PV inputs.

## Updating an existing 8 MB installation

OTA requires an existing 8 MB dual-slot installation. **Menu > System
information** must show **OTA available: Yes**.

1. Back up production history from **Menu > Energy history > Download
   restorable backup**.
2. Download `ESP32C6_ECU-8MB-OTA.bin` from the desired release. Use the
   application `.bin`, not the merged image, bootloader or partition image.
3. Open **Menu > Firmware update**, select the application image and install it.
4. Keep power and Wi-Fi stable until the page reports success, then reboot.
5. Verify the firmware version, network, inverter list, polling and history.

OTA writes the inactive application slot and preserves NVS/SPIFFS. It cannot
convert a 4 MB installation or change partition layouts. Use USB for those
operations and keep physical USB access available for recovery.

## Production history backup and recovery

The Energy history page offers two downloads and an explicit shutdown save:

- **Download CSV** includes finalized days plus the volatile current day and is
  intended for people and spreadsheets.
- **Download restorable backup** downloads the exact finalized binary journal.
  This is the file accepted by Restore history.
- **Save today to flash now** writes a CRC-protected snapshot of the current
  day's per-inverter totals to `/energy-today.bin`. It does not end the day or
  stop later production from accumulating. Use it immediately before an
  intentional power-off when preserving today's total matters.

Restore first writes a temporary file, checks record size, magic, date range
and CRC, preserves the current journal for rollback,
then activates the validated backup. It replaces finalized history and resets
the current day's volatile hourly/statistics RAM.

Wipe is permanent and requires typing `WIPE` plus accepting a browser warning.
Download a binary backup first. Changing partition layouts or flashing a merged
image can erase history independently of the web controls.

Only finalized daily records are included in the downloadable backup. A saved
current-day checkpoint is restored automatically after a same-day restart and
promoted to finalized history if the ECU next starts on a later local date.
Hourly buckets and per-inverter operating statistics intentionally live only
in RAM to avoid flash wear; the manual save preserves totals, not those
fine-grained observations.

## Energy accounting

Telemetry energy deltas are accumulated per inverter:

- current-day 24-hour buckets remain in RAM;
- one finalized record is appended to `/energy-days.bin` at local-day rollover;
- an optional administrator-requested `/energy-today.bin` checkpoint is the
  only normal current-day energy write;
- recorded/lifetime energy is reconstructed from the validated journal at boot;
- `/api/energy/hourly?inv=N` returns one inverter, and `inv=-1` returns the
  fleet;
- `/api/energy/days?limit=90` returns recent finalized records plus today;
- `/api/energy/history.csv` streams CSV; and
- `/energy/backup` downloads the lossless restorable journal.

The recorded counter starts when this firmware's history is initialized. It is
not the inverter's factory lifetime counter. The first telemetry response after
an ECU restart establishes energy/time baselines and reports zero power; this
prevents accumulated inverter energy from becoming a false startup power spike
or duplicate energy.

The dashboard's **Lifetime Energy** value is therefore the durable production
recorded by this ECU since its history was initialized (or last wiped), not the
microinverter's factory lifetime production. Per-input **Inverter energy
counter** values are separate raw, short-window counters reported by the
inverter in Wh. They can reset or wrap and should not be interpreted as daily
or lifetime totals; the ECU uses their deltas to build its authoritative daily
and lifetime history.

## Output limiting and grid profiles

The inverter output target is **watts per connected PV input**, not a percentage
or whole-inverter limit. For example, `100` requests about 100 W from each DS3
input, approximately 200 W total. The web UI accepts 20-500 W per input; 500 W
requests normal maximum output. Use low limits cautiously.

Grid profiles use OpenAPS `invdriver.gridprofile/v1` JSON and change utility
protection values such as voltage/frequency trip thresholds and timing. They do
**not** install executable inverter firmware. Operations target one inverter,
back up readable current values, enforce bounds and read every written value
back. YC600 writes remain disabled because no verified encoder path is
available. Use only utility-approved settings.

## Home Assistant, Modbus and SunSpec

When enabled, the read-only server accepts Modbus functions 03 and 04 on TCP
port 502. It is enabled by default and can be switched off under **Menu >
Polling and access**. Unit 1 is the fleet aggregate; units 2-10 map to inverter
indexes 0-8. It exposes SunSpec Common Model 1 and single-phase Inverter Model
101.

Home Assistant's built-in Modbus integration can create fleet and per-inverter
power/energy sensors without HACS. Copy-ready YAML is in
[HomeAssistant.md](HomeAssistant.md); the register map is in
[SUNSPEC.md](SUNSPEC.md). Modbus serves the last completed telemetry snapshot,
so a persistent client does not generate radio requests or contend with the
poll scheduler.

## MQTT and HTTP

The project preserves the upstream MQTT formats and command topic. MQTT is
disabled by default. Configure a broker reachable from the ECU's network,
choose the required format and use **Send test**; the test reports actual
connection or publish failure.

The compatibility `get.Data` interface remains available. New UI/API code uses
lowercase routes under `/api`. Do not expose the ECU directly to the Internet;
place remote access behind a trusted VPN or authenticated reverse proxy.

## Failure diagnostics

**Menu > Diagnostics** provides three administrator-only downloads:

- the current system/radio report and newest in-memory trace lines;
- a fixed-size twelve-hour flight recorder with one-minute heap, largest-free-
  block, task-stack, Wi-Fi, temperature and polling snapshots; and
- the ESP-IDF crash dump stored in the dedicated flash partition, when a panic
  or watchdog failure has created one.

The flight recorder is **disabled by default** and can be enabled under
**Menu > Polling and access** for intermittent-failure investigation. Enabling
it creates or reuses `/flight-recorder.bin`, a preallocated 720-record circular
file (74 bytes per record, 53,280 bytes total). While enabled it overwrites one
slot each minute and also records Wi-Fi loss, restoration and reconnect events.
Each record captures uptime and local time, free/minimum heap, largest free
block, task stack margins, Wi-Fi state/RSSI/reason, die temperature and current
poll activity. It retains roughly twelve hours and never grows with uptime.

Disabling it stops all recorder flash writes immediately but retains existing
records for download. Wi-Fi reconnect supervision is independent and remains
active. The bounded live trace shown in the diagnostic report is RAM-only. An
ESP-IDF crash dump is written only after a qualifying panic/watchdog failure.
A crash dump must be decoded with the exact `.elf` from the firmware build that
crashed, so retain the release ELF and download all three diagnostic files
before installing another build whenever possible.

## Build from source

### Default 8 MB build

`partitions.csv` now defaults to the recommended 8 MB dual-OTA layout and is
identical to `partitions-8mb-ota.csv`.

1. Install Arduino IDE 2.x.
2. Add
   `https://espressif.github.io/arduino-esp32/package_esp32_index.json` to Boards
   Manager URLs.
3. Install **esp32 by Espressif Systems 3.3.8**.
4. Install ArduinoJson 7.4.2, PubSubClient 2.8, NTPClient 3.2.1, Time 1.6.1,
   PSACrypto 1.1.1, ESP Async WebServer, AsyncTCP and sunMoon. CI contains the
   authoritative dependency commands.
5. Open `ESP32C6-APsystems-ECU.ino` and select the board or **ESP32C6 Dev
   Module**.
6. Select **Flash size: 8 MB**, **Partition Scheme: Custom**, **USB CDC On Boot:
   Enabled**, and leave **Zigbee Mode: Disabled/default**.
7. Upload and monitor at 115200 baud.

Do not enable ZCZR/ZBOSS. The native APsystems transport requires exclusive
ownership of the C6's IEEE 802.15.4 callbacks.

### 4 MB USB-only build

Before compiling for a 4 MB board, replace `partitions.csv` with
`partitions-4mb-noota.csv` and select 4 MB flash. That layout has one factory
application and no OTA slot. Restore the 8 MB default afterward by copying
`partitions-8mb-ota.csv` back to `partitions.csv`.

| Flash | Application layout | Web OTA | SPIFFS |
|---|---|---:|---:|
| 8 MB default | two 3 MB OTA slots | yes | about 1.85 MB |
| 4 MB alternative | one 3.375 MB factory image | no | about 488 KB |

CI explicitly substitutes each named partition file, compiles both variants and
packages separate artifacts. See [BUILD-VERIFIED.md](BUILD-VERIFIED.md).

### Local Windows helpers

With Espressif's ESP-IDF 5.5 tools installed under `C:\Espressif`:

```powershell
.\tools\Flash-Firmware.ps1 -Variant 8MB -Port COM7
.\tools\Serial-Monitor.ps1 -Port COM7
```

The flash helper erases by default. Add `-SkipErase` only for a same-layout
replacement when retaining configuration is intentional. For built-in USB-JTAG
source debugging, connect the native USB/JTAG port and run:

```powershell
.\tools\Debug-ESP32C6.ps1
```

## Architecture

The inherited architecture was:

`application -> TI ZNP over UART -> CC2530/CC2531 -> Zigbee APS`

The current architecture is:

`refactored application/services -> compatibility adapter -> native APsystems MAC/NWK/APS -> ESP32-C6 radio`

`ZIGBEE_A_TRANSPORT.ino` accepts the preserved `AF_DATA_REQUEST`-style calls,
builds raw IEEE 802.15.4/NWK/APS frames, acknowledges and reassembles fragmented
responses, and renders the subset of TI `AF_INCOMING_MSG` expected by the
proven decoders. Pairing persists each inverter's PAN and short radio address.

Important modules include:

- `ZIGBEE_A_TRANSPORT.ino` and `ZIGBEE_COORDINATOR.ino` — native radio path;
- `APS_CRYPTO.ino` — plaintext/APsystems AES envelope;
- `POLL_SCHEDULER.ino` — cooperative fleet arbitration;
- `INVERTER_INFO.ino` — model and firmware query;
- `ENERGY_HISTORY.ino` — low-wear journal, statistics and backup/restore;
- `GRID_PROFILE.ino` — guarded protection profile operations;
- `SUNSPEC_MODBUS.ino` — read-only Modbus/SunSpec service; and
- `PORTAL_WIFI.ino`, `WEB_UI.ino` and related pages — refactored local UI.

The ZNP-to-native operation map is in [PORTING-NOTES.md](PORTING-NOTES.md).
Security analysis is in [SECURITY-AUDIT.md](SECURITY-AUDIT.md).

## Release automation

Every push and pull request builds separate `4mb-noota` and `8mb-ota` bundles.
Each contains application and merged images, bootloader, partition image,
checksums and `BUILD-INFO.txt`; the 8 MB bundle also contains the ELF file.
Artifacts are retained for 30 days. A pushed tag beginning with `v` publishes
the same outputs as a permanent GitHub Release.

## Documentation map

- [ACKNOWLEDGEMENTS.md](ACKNOWLEDGEMENTS.md) — people, projects and ideas;
- [UPSTREAM.md](UPSTREAM.md) — source lineage and retained/refactored areas;
- [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) — licenses and dependencies;
- [BUILD-VERIFIED.md](BUILD-VERIFIED.md) — current build/hardware evidence;
- [LIMITATIONS.md](LIMITATIONS.md) — unvalidated and unsupported behavior;
- [PORTING-NOTES.md](PORTING-NOTES.md) — native transport mapping;
- [SECURITY-AUDIT.md](SECURITY-AUDIT.md) — CC25xx/Zigbee/AES findings;
- [SUNSPEC.md](SUNSPEC.md) and [HomeAssistant.md](HomeAssistant.md) — integrations;
- [DEFERRED-WORK.md](DEFERRED-WORK.md) — deliberately postponed improvements.

## License

The application remains under the inherited MIT license in [LICENSE](LICENSE).
Third-party components and referenced projects retain their own licenses.
