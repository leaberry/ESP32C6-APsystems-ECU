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

## Reporter-verified in v1.4.14

[Issue #11 feedback](https://github.com/leaberry/ESP32C6-APsystems-ECU/issues/11#issuecomment-5653722474) verifies custom NTP, basic Home Assistant MQTT,
antenna switching (about +20 dB with the external antenna in that setup), and
hostname changes on the reporter's network. These results do not cover advanced
antenna pins, every MQTT sensor/control or other network environments.
Settings restore was explicitly not tested. See [the verification record](BUILD-VERIFIED.md#issue-11-reporter-verification-2026-09-13-v1414)
for the exact scope.

[Issue #17 feedback](https://github.com/leaberry/ESP32C6-APsystems-ECU/issues/17#issuecomment-5701304771)
adds successful YC600 pairing and operation, and native HA MQTT discovery and
separate readings for two DS3s. The screenshot shows power, energy, temperature,
AC voltage, frequency and panel power for both DS3s. The power-limit controls
appear, but the reporter explicitly did not test throttling. Migration of their
old Energy dashboard history is still planned. See the
[issue #17 verification record](BUILD-VERIFIED.md#issue-17-reporter-verification-2026-09-16-v1414).

## Still requiring validation or intentionally unsupported

1. **Encrypted transport:** key derivation and the known-answer test pass, but
   no physical AES-enabled inverter has been tested.
2. **Other models:** YC600 pairing and operation now have reporter confirmation;
   detailed model-specific telemetry and control validation remain open. QS1
   has not been tested through the native C6 radio.
3. **Grid-protection writes:** inspect read-only values first, then test one
   utility-approved value on a service bench. YC600 writes are disabled.
4. **Outbound fragmentation:** current commands fit one 802.15.4 frame. A future
   larger request needs transmit fragmentation.
5. **History lifecycle:** native binary backup/restore has prior hardware
   evidence in BUILD-VERIFIED.md; local-midnight rollover and a successful wipe
   still need targeted validation. History restore excludes today's unfinished
   data, clears today's totals/hourly/statistics RAM and deletes the shutdown
   checkpoint. Empty today after restore is expected, not evidence of a failed
   restore. Settings restore and replacement-board continuity remain unverified.
6. **MQTT end to end:** HA discovery and separate DS3 sensor readings now have
   reporter confirmation. Broker persistence/restart recovery, Energy statistics
   and old-history migration, physical limit commands and simultaneous
   HA/Domoticz load still need explicit field validation.
7. **Soak testing:** continue repeated polling while UI, MQTT and persistent
   Modbus clients are active and monitor coexistence retries and dropped frames.

Changing partition layouts or flashing a merged image can erase NVS/SPIFFS.
Download a restorable production-history backup before maintenance. This is a
community reverse-engineering project, not a replacement for manufacturer or
utility commissioning tools.
