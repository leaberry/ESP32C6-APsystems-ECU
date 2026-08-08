# ESP32-C6 read APsystems inverters

An ESP32-C6 port of [patience4711/ESP32-read-APS-inverters](https://github.com/patience4711/ESP32-read-APS-inverters). It runs the coordinator on the C6's integrated 802.15.4 radio, so a CC2530/CC2531, ZNP firmware, UART wiring, and reset wire are no longer required.

The existing application is retained: YC600/QS1/DS3 pairing and polling, inverter decoding, web UI, Wi-Fi setup portal, HTTP endpoints, MQTT formats, scheduling, OTA, logging, reboot and output throttling. The original repository does **not** contain a Modbus server; this port therefore does not claim one.

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

Replace `COM7` with your port. This erases existing settings. SHA-256: `250601f2a7846ca1611279cb2c648ca889e2a095abf3790c6b2963e3a4ec11f5`.

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
| Security | none for APsystems APS traffic |

## Fresh network state

The port deliberately erases Zigbee NVRAM at boot and reforms the fixed APsystems network. This prevents an old PAN/channel/security configuration from silently overriding the ECU-derived settings. Wi-Fi and application configuration in Preferences/SPIFFS is unaffected.

## Repository layout

- `ESP32C6_ECU.ino` — original application entry point, with UART setup removed
- `ZIGBEE_A_TRANSPORT.ino` — C6 raw-APS transport and legacy receive adapter
- `ZIGBEE_COORDINATOR.ino` — integrated coordinator setup
- `ZIGBEE_PAIR.ino`, `ZIGBEE_POLLING.ino`, `ZIGBEE_QUERYING.ino`, `SETPOWER.ino` — preserved APsystems protocol logic
- `SECURITY-AUDIT.md` — CC2530 firmware/security findings and evidence
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
