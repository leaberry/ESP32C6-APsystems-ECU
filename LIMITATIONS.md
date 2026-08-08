# Limitations and hardware test checklist

This is a concrete, buildable port, but no inverter or ESP32-C6 was attached during development. Do not treat compile success as RF interoperability proof.

The included 4 MB custom partition leaves about 488 KB for SPIFFS. That is ample for the application's small configuration files, but less than the upstream classic-ESP32 layout.

## Items needing physical validation

1. **Pairing broadcast mapping.** TI ZNP's special `AF_DATA_REQUEST_EXT` address mode `0x0F` with an all-ones destination is mapped to Zigbee short broadcast `0xFFFF`. This is the standards-equivalent all-device broadcast, but it is the largest remaining interoperability question.
2. **Unsecured network formation.** Captures support security level zero. Confirm on a sniffer, if available, that the C6 NWK frame-control security bit remains clear.
3. **Extended PAN ID byte order.** The code reproduces the bytes sent to `ZCD_NV_EXT_PAN_ID`: `FF FF` followed by the reversed six-byte ECU ID. Verify the formed network beacon against a known-working CC2530 setup.
4. **Receive filtering.** Confirm cluster `0x0106`, profile `0x0F05`, endpoint `0x14` indications reach the raw APS callback on Arduino-ESP32 3.3.8.
5. **Wi-Fi coexistence/range.** Run repeated polls while serving the web UI and publishing MQTT.

## Suggested first test

Use one inverter close to the C6. Configure the same ECU ID used by the working CC2530 device, pair it, then issue a manual poll from the console. At 115200 baud, expect coordinator formation followed by transmit confirms and a cluster `0x0106` receive message. If pairing fails, capture channel 16 and compare the four pairing broadcasts byte-for-byte with the legacy unit.
