# ESP32-C6 read APsystems inverters

An ESP32-C6 port of [patience4711/ESP32-read-APS-inverters](https://github.com/patience4711/ESP32-read-APS-inverters). It runs the coordinator on the C6's integrated 802.15.4 radio, so a CC2530/CC2531, ZNP firmware, UART wiring, and reset wire are no longer required.

The existing application is retained: YC600/QS1/DS3 pairing and polling, inverter decoding, web UI, Wi-Fi setup portal, HTTP endpoints, MQTT formats, scheduling, OTA, logging, reboot and output throttling. This port also handles APsystems' optional proprietary AES transport and serves live values over SunSpec Modbus/TCP.

> Hardware status: the project compiles for ESP32-C6 and its transport is mapped to Espressif's raw APS API. Actual pairing/polling still needs testing against physical APsystems inverters. See [LIMITATIONS.md](LIMITATIONS.md).

## Hardware

- ESP32-C6 board with at least 4 MB flash (for example Seeed XIAO ESP32-C6 or Espressif ESP32-C6-DevKitC-1)
- USB cable and a suitable 5 V supply
- No external Zigbee module

Wi-Fi and Zigbee share the 2.4 GHz radio resources. The ESP32-C6 coexistence layer schedules them; place the board where both Wi-Fi and inverter signal are good.

## Arduino IDE build and flash

1. Install Arduino IDE 2.x.
2. In **Preferences → Additional boards manager URLs**, add:
   `https://espressif.github.io/arduino-esp32/package_esp32_index.json`
3. In **Boards Manager**, install **esp32 by Espressif Systems 3.3.8**.
4. In **Library Manager**, install:
   - ArduinoJson
   - ESP Async WebServer (maintained by ESP32Async)
   - Async TCP (maintained by ESP32Async)
   - PubSubClient
   - NTPClient
   - Time
   - sunMoon
   - PSACrypto
5. Open `ESP32C6_ECU.ino`.
6. Select your ESP32-C6 board. For XIAO, select **XIAO_ESP32C6**; otherwise **ESP32C6 Dev Module** is a safe generic choice.
7. Set **Tools → Zigbee Mode → Zigbee ZCZR (coordinator/router)**.
8. Choose **Tools → Partition Scheme → Custom**. The included `partitions.csv` fits the full application, OTA slots, SPIFFS, and Zigbee state into a 4 MB C6. Espressif's stock 4 MB ZCZR layout has only a 1.25 MB application slot and is too small for the inherited web application.
9. Enable **USB CDC On Boot** if your board uses native USB, select its port, then click **Upload**.
10. Open Serial Monitor at 115200 baud. On first boot, use the same Wi-Fi/configuration portal workflow documented by the upstream project.

If upload does not start, hold **BOOT**, tap **RESET**, release **BOOT**, and upload again. Board-specific button behavior can differ.

### Flash the supplied build directly

`firmware/ESP32C6_ECU.ino.merged.bin` is the verified generic ESP32-C6 4 MB image. With `esptool` installed, put the board in download mode and run:

```text
esptool --chip esp32c6 --port COM7 erase-flash
esptool --chip esp32c6 --port COM7 write-flash 0x0 firmware/ESP32C6_ECU.ino.merged.bin
```

Replace `COM7` with your port. This erases existing settings. SHA-256: `3bf1df364625210c710876b12754dde97ca6a0bd013b5f9ebf821203e3c23671`.

The default button input is GPIO 0; the LED uses the board's `LED_BUILTIN` definition. Override `APS_BUTTON_PIN` or `APS_LED_PIN` at compile time for other wiring.

## What changed

The old design was:

`application → ZNP byte commands → UART → CC2530/CC2531 → Zigbee APS`

This port is:

`application → ZNP-compatible adapter → Espressif raw APS API → integrated C6 radio`

`ZIGBEE_A_TRANSPORT.ino` parses the two ZNP calls the application actually uses (`AF_DATA_REQUEST` and `AF_DATA_REQUEST_EXT`) and submits the exact same APsystems ASDU bytes. Received raw APS frames are rendered into the legacy `AF_INCOMING_MSG` layout in memory, allowing the tested upstream pairing and inverter decoders to remain unchanged.

Network parameters are also preserved:

| Setting | Value |
|---|---:|
| Role | coordinator |
| Channel | 16 |
| PAN ID | derived from ECU ID (normally `0xA3D8`) |
| Extended PAN ID | `FF:FF` plus reversed ECU ID |
| Endpoint | `0x14` |
| Profile | `0x0F05` |
| Device ID | `0x0100` |
| Request cluster | `0x0006` |
| Response cluster | `0x0106` |
| Zigbee NWK/APS security | disabled (matches the legacy ZNP application) |
| APsystems L1 transport | automatic plaintext or AES-128-ECB |

That final row refers to standard Zigbee NWK/APS security. APsystems' separate
application transport may be plaintext or AES-128-ECB. The firmware selects AES
for inverter serial numbers whose second character is `2`, confirms it when an
encrypted reply is successfully decoded, and supports a mixed installation.
The home and inverter-detail pages show `AES` or `plain` for every inverter.

## SunSpec and Home Assistant

The read-only Modbus/TCP server listens on port **502** and implements function
codes 3 and 4. Its standard model chain begins at register 40000:

- Unit ID 1: aggregate power, energy, voltage, frequency, temperature and DC data
- Unit IDs 2 through 10: inverter indexes 0 through 8
- Models: Common Model 1 and single-phase Inverter Model 101

For the HACS `CJNE/ha-sunspec` integration, use the ESP32-C6's Wi-Fi address,
port `502`, and slave/unit ID `1`. Add another integration entry with unit ID
`2`, `3`, and so on if you want separate devices for each microinverter. See
[`SUNSPEC.md`](SUNSPEC.md) for the map and generic Home Assistant Modbus setup.

## Fresh network state

The port deliberately erases Zigbee NVRAM at boot and reforms the fixed APsystems network. This prevents an old PAN/channel/security configuration from silently overriding the ECU-derived settings. Wi-Fi and application configuration in Preferences/SPIFFS is unaffected.

## Repository layout

- `ESP32C6_ECU.ino` — original application entry point, with UART setup removed
- `ZIGBEE_A_TRANSPORT.ino` — C6 raw-APS transport and legacy receive adapter
- `ZIGBEE_COORDINATOR.ino` — integrated coordinator setup
- `ZIGBEE_PAIR.ino`, `ZIGBEE_POLLING.ino`, `ZIGBEE_QUERYING.ino`, `SETPOWER.ino` — preserved APsystems protocol logic
- `SECURITY-AUDIT.md` — CC2530 firmware/security findings and evidence
- `APS_CRYPTO.ino` — optional plaintext/AES APsystems transport
- `SUNSPEC_MODBUS.ino` — read-only SunSpec Modbus/TCP server
- `SUNSPEC.md` — SunSpec and Home Assistant setup
- `LIMITATIONS.md` — remaining hardware-validation points
- `firmware/` — compile-verified generic ESP32-C6 binaries

## Publish as your GitHub repository

The included Git repository preserves upstream history and names the original remote `upstream`. After creating an empty repository on GitHub:

```text
git remote add origin https://github.com/YOUR-NAME/YOUR-REPOSITORY.git
git push -u origin main
```

Do not rename `upstream` to `origin`; keeping both names makes future comparison with patience4711's project straightforward.

## License and attribution

The application is derived from patience4711's GPL-3.0 project; see [LICENSE](LICENSE). Espressif's Zigbee stack is supplied through the ESP32 Arduino core under its own licenses.
