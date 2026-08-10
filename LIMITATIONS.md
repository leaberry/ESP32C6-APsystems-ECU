# Limitations and hardware test checklist

The default 4 MB layout has about 488 KB for SPIFFS and no OTA slot. The 8 MB
layout has dual OTA slots and about 1.85 MB SPIFFS.

## Validated on hardware

- ESP32-C6 Wi-Fi and raw IEEE 802.15.4 coexist on channel 16.
- Pairing discovers the proprietary reply and persists the inverter PAN/address.
- If that reply omits the legacy inverter ID, pairing can safely infer a route
  when exactly one responder remains after known peers are eliminated.
- A plaintext DS3 returns two APS fragments; both are acknowledged, reassembled
  and decoded into voltage, frequency, per-panel power and energy.
- The tested DS3 returned firmware version `5.456`.
- A second DS3 was polled on a separate PAN and returned firmware `5.307`; its
  inferred route remained functional after reboot.
- Multiple inverters on the same PAN may answer a broadcast; replies are matched
  to the requested inverter by serial number.

## Items still needing validation

1. **Encrypted inverter transport.** AES key derivation and the known-answer
   self-test pass, but a physical AES-enabled inverter has not been tested.
2. **Other models.** YC600 and QS1 use the preserved protocol/decoders but have
   not yet been tested with the native radio.
3. **Grid-protection writes.** First perform read-only profile inspection, then
   test one utility-approved value on a service bench. YC600 writes remain
   intentionally unsupported.
4. **Long requests.** Outbound APS fragmentation is not implemented. Current
   poll, query, pairing, throttle and profile messages fit one 802.15.4 frame;
   future commands larger than that must add transmit fragmentation.
5. **Energy rollover.** Run across local midnight, reboot, and verify
   exactly one daily journal record and intact current-day hourly RAM values.
6. **Soak testing.** Exercise repeated polling while serving the UI, MQTT and a
   persistent Modbus/TCP client, and watch coexistence retries and dropped frames.
