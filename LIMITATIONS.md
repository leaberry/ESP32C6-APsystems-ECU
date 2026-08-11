# Limitations and hardware test checklist

The recommended/default 8 MB layout has dual OTA slots and about 1.85 MB
SPIFFS. The 4 MB alternative has about 488 KB SPIFFS and no OTA slot.

## Validated on hardware

- ESP32-C6 Wi-Fi and raw IEEE 802.15.4 coexist on channel 16.
- Pairing discovers proprietary replies and persists inverter PAN/address.
- A missing legacy ID can be inferred only when one unique unknown responder
  remains after known peers are removed.
- Three plaintext DS3s are polled: two on one PAN and one on another.
- Two APS fragments are acknowledged, reassembled and decoded into voltage,
  frequency, temperature, per-input power and energy.
- Replies are matched to the requested inverter by serial number when same-PAN
  units answer a broadcast.
- Firmware versions `5.456`, `5.307` and `5.456` were returned by the field
  units.
- OTA, modern Wi-Fi setup, DHCP/static network settings, web/API, diagnostics,
  scheduler and first-poll energy baselining have been exercised on the 8 MB
  board.

## Still requiring validation or intentionally unsupported

1. **Encrypted transport:** key derivation and the known-answer test pass, but
   no physical AES-enabled inverter has been tested.
2. **Other models:** YC600 and QS1 retain known payload builders/decoders but
   have not been tested through the native C6 radio.
3. **Grid-protection writes:** inspect read-only values first, then test one
   utility-approved value on a service bench. YC600 writes are disabled.
4. **Outbound fragmentation:** current commands fit one 802.15.4 frame. A future
   larger request needs transmit fragmentation.
5. **History lifecycle:** verify local-midnight rollover plus successful binary
   backup/restore/wipe on live hardware. Restore intentionally excludes volatile
   current-day hourly and operating-statistics RAM.
6. **MQTT end to end:** settings and truthful connection failure are tested, but
   a reachable local broker is still needed to capture a live publish.
7. **Soak testing:** continue repeated polling while UI, MQTT and persistent
   Modbus clients are active and monitor coexistence retries and dropped frames.

Changing partition layouts or flashing a merged image can erase NVS/SPIFFS.
Download a restorable production-history backup before maintenance. This is a
community reverse-engineering project, not a replacement for manufacturer or
utility commissioning tools.
