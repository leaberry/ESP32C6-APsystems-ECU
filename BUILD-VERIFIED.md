# Build and hardware verification

This document records evidence for the current source tree. It is not a claim
that every supported inverter model or control path has been field-tested.

## Reproducible build environment

Verified on 2026-08-14 with:

- Arduino CLI and Espressif Arduino core 3.3.8;
- target `esp32:esp32:esp32c6`;
- Zigbee mode `default` (ZBOSS disabled);
- the included 8 MB dual-OTA layout and 4 MB USB-only alternative;
- ArduinoJson 7.4.2, PubSubClient 2.8, NTPClient 3.2.1, Time 1.6.1 and
  PSACrypto 1.1.1; and
- ESP Async WebServer 3.12.0 and AsyncTCP 3.5.0.

Final v1.4.9 sizes for both layouts:

```text
8 MB application: 1,487,638 bytes
4 MB application: 1,487,638 bytes
Global variables: 89,008 bytes (238,672 bytes free)
8 MB OTA application slot: 3,145,728 bytes
8 MB OTA application-slot margin: 1,658,090 bytes
4 MB factory application slot: 3,538,944 bytes
4 MB application-slot margin: 2,051,306 bytes
```

GitHub Actions explicitly copies `partitions-8mb-ota.csv` or
`partitions-4mb-noota.csv` before compiling, so changing the repository default
to 8 MB does not make the 4 MB artifact ambiguous. Both variants were also
compiled in a clean Ubuntu environment after applying the documented
`sunMoon`/Time 1.6.1 header compatibility adjustment used by the workflow.

## Live hardware evidence

Testing used an 8 MB ESP32-C6 with Wi-Fi active and three plaintext DS3
inverters in range. It verified:

- raw channel-16 receive while serving the web UI;
- proprietary APsystems pairing reply parsing;
- safe unique-peer inference when an inverter omits its pairing-ID announcement;
- persistence of PAN and short radio address across reboot;
- two inverters on one PAN plus a third on another PAN;
- transient Wi-Fi/802.15.4 coexistence retry;
- one jittered retry when contention interrupts a fragmented reply;
- APS fragment acknowledgements, 105-byte reassembly and telemetry decode;
- firmware versions `5.456`, `5.307` and `5.456` from the three units;
- repeated 45-second fleet polling with the web and Modbus services active;
- first-post-reboot energy baselining without a false power spike or duplicate
  production; and
- v1.4.x OTA installation with NVS/SPIFFS configuration preserved;
- lossless download of a 96-byte, two-record history journal with attachment
  headers;
- rejection of malformed restore input and an incorrect wipe confirmation with
  HTTP 400 while preserving the journal; and
- successful restore of that native journal with identical before/after
  SHA-256 and unchanged daily totals;
- v1.4.4 administrator-password validation rejecting an incorrect current
  password, mismatched confirmation and shared role passwords without changing
  stored credentials; and
- the read-only `user` account receiving the dashboard while being rejected
  from the administrator menu;
- retrieval of a 12,196-byte ESP-IDF flash coredump from a field watchdog
  failure and identification of the stalled `async_tcp` task; and
- 656 authenticated API requests in 45 seconds while a deliberately
  non-reading WebSocket client applied backpressure, with all three inverters
  continuing to poll and at least 151 KB of free heap remaining.

The boot-time AES known-answer test passes the published reverse-engineered
vector. Hardware validation of an encrypted inverter remains open. A deliberate
successful wipe was not performed on the field device; its confirmation guard
and non-destructive rejection path were tested instead.
