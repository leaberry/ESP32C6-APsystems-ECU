# Development and architecture

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

The flash helper is for first installation: it erases by default and writes a
merged image. `-SkipErase` does not make that image safe for preserving settings.
For an upgrade, use the application-only commands in [README.md](README.md#upgrade-without-losing-settings-or-data). For built-in USB-JTAG
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


## ECU identifier initialization

At boot, an ECU still using the legacy default `D8A3011B9780` generates and
saves a random 12-digit hexadecimal identifier **only if no pairing data is
present**. A custom or previously generated identifier is preserved. The
generated ID is not a secret and requires no cryptographic randomness.

The check covers all nine inverter slots, interrupted pairing files, and the
saved radio-peer table, including orphan peers. Malformed or unreadable pairing
records also preserve the current ID. The firmware saves and reads back the
new configuration before activating the ID; failed writes leave the active ID
unchanged. Interrupted file replacement can recover the previous configuration.

`ECU_ID` is used in inverter pairing, polling and control messages, determines
the operational PAN, and is exposed as the SunSpec ECU serial number. Existing
paired installations using the default retain it to avoid requiring re-pairing.
The legacy MQTT client ID remains derived from the board MAC; the separate
Home Assistant client ID uses ECU_ID. Identity initialization
happens before radio or web startup, and subsequent boots reuse the saved ID.

Developer check: `python3 tools/test_ecu_identity.py` requires g++ and ArduinoJson
headers. Set `ARDUINOJSON_INCLUDE` to the library's `src` directory if it is not
under `~/Arduino/libraries/ArduinoJson`.


The compatibility `get.Data` HTTP interface remains available. New UI/API code
uses lowercase routes under `/api`.
