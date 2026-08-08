# Limitations and hardware test checklist

This is a concrete, buildable port, but no inverter or ESP32-C6 was attached during development. Do not treat compile success as RF interoperability proof.

The default 4 MB custom partition leaves about 488 KB for SPIFFS and deliberately has no OTA slot. The 8 MB layout has dual OTA slots and about 1.85 MB SPIFFS.

## Items needing physical validation

1. **Pairing broadcast mapping.** TI ZNP's special `AF_DATA_REQUEST_EXT` address mode `0x0F` with an all-ones destination is mapped to Zigbee short broadcast `0xFFFF`. This is the standards-equivalent all-device broadcast, but it is the largest remaining interoperability question.
2. **Unsecured network formation.** Captures support security level zero. Confirm on a sniffer, if available, that the C6 NWK frame-control security bit remains clear.
3. **Extended PAN ID byte order.** The code reproduces the bytes sent to `ZCD_NV_EXT_PAN_ID`: `FF FF` followed by the reversed six-byte ECU ID. Verify the formed network beacon against a known-working CC2530 setup.
4. **Encrypted-inverter transmit validation.** The AES algorithm, key derivation,
   padding and raw-APS conversion match the open-source static reverse
   engineering linked from issue 55, but no public golden capture from a real
   AES-enabled inverter was available. Test pairing and polling with an inverter
   whose serial number has `2` as its second character.
5. **Receive filtering.** Confirm cluster `0x0106`, profile `0x0F05`, endpoint `0x14` indications reach the raw APS callback on Arduino-ESP32 3.3.8.
6. **Firmware information.** Verify command `0xDC` and all reply variants on YC600, QS1 and DS3 firmware generations.
7. **Grid-protection writes.** First perform read-only profile inspection, then test one utility-approved value on a service bench. Confirm the backup and value-by-value read-back before enabling production use. YC600 writes are intentionally unsupported.
8. **Energy rollover.** Run across sunset and local midnight, reboot, and confirm exactly one daily journal record plus intact current-day hourly RAM values.
9. **Wi-Fi coexistence/range.** Run repeated polls while serving the web UI, publishing MQTT and holding a persistent Modbus/TCP connection.

## Suggested first test

Use one inverter close to the C6. Configure the same ECU ID used by the working CC2530 device, pair it, then issue a manual poll from the console. At 115200 baud, expect coordinator formation followed by transmit confirms and a cluster `0x0106` receive message. If pairing fails, capture channel 16 and compare the four pairing broadcasts byte-for-byte with the legacy unit.
