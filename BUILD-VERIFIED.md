# Build and hardware verification

Verified on 2026-08-09 with:

- Arduino CLI and Espressif Arduino core 3.3.8
- target `esp32:esp32:esp32c6`
- Zigbee mode `default` (ZBOSS disabled)
- the included custom 8 MB OTA partition table
- ESP Async WebServer 3.12.0 and Async TCP 3.5.0

The native-radio build result was:

```text
Sketch: 1,389,538 bytes
Global variables: 87,552 bytes
8 MB OTA application slot: 3,145,728 bytes
8 MB OTA application-slot margin: about 1.76 MB
```

Live testing used an 8 MB ESP32-C6 with Wi-Fi active and three DS3 inverters
in range. It verified:

- raw channel-16 receive while serving the web UI;
- proprietary APsystems pairing reply parsing;
- safe unique-peer inference when an inverter omits its pairing-ID announcement;
- persistence of inverter PAN and short radio address;
- transient Wi-Fi/802.15.4 coexistence retry;
- one jittered retry when RF contention interrupts a fragmented poll reply;
- APS fragment acknowledgements for blocks zero and one;
- reassembly and decoding of a 105-byte telemetry response; and
- firmware-version query and decode (`5.456` on the tested inverter).

A second DS3 was learned on a different PAN (`0x01CE`, source `0xAAEC`),
polled successfully after reboot, and reported firmware `5.307`.

The boot-time APsystems AES known-answer test uses the published reverse-
engineered vector. Hardware validation of an encrypted inverter remains open.
The 4 MB source variant is built by CI with the same transport and no OTA slot.
