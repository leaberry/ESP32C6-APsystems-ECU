# Acknowledgements and project lineage

This project exists because multiple people published difficult reverse-
engineering work, field captures and working code. Credit is intentionally
broad; an omission should be treated as a documentation bug and corrected.

## Direct software lineage

- **patience4711 (Johannes)** created and maintained
  [`read-APSystems-YC600-QS1-DS3`](https://github.com/patience4711/read-APSystems-YC600-QS1-DS3),
  [`ESP32-read-APS-inverters`](https://github.com/patience4711/ESP32-read-APS-inverters)
  and [`RPI-APS-inverters`](https://github.com/patience4711/RPI-APS-inverters).
  This repository started from the ESP32 project and still uses its APsystems
  payload builders, pairing sequence, YC600/QS1/DS3 telemetry decoders, MQTT
  formats and many configuration concepts. The upstream MIT copyright and
  license remain in `LICENSE`.
- **fwolfst** is credited by the ESP8266 predecessor for helping clean up and
  streamline the original engine. That work flowed into the later ESP32 code.
- **Daniel Leaberry** maintains this ESP32-C6 refactor and performed the current
  8 MB board and three-DS3 field testing. The native radio adapter, modern web
  and Wi-Fi layers, cooperative scheduler, history/statistics, OTA/release
  automation, diagnostics, SunSpec/Modbus service and associated safety work
  were developed for this repository.

## APsystems radio and protocol research

- **kadzsol** developed the custom CC2530/CC2531 firmware used by the predecessor
  projects and explained important DS3 serial-buffer behavior. Although this
  repository no longer ships or requires that binary, it was essential to the
  working protocol lineage and to understanding the old ZNP boundary.
- **petsch9** opened
  [`Koenkk/zigbee2mqtt` issue #4221](https://github.com/Koenkk/zigbee2mqtt/issues/4221),
  and the many people who contributed captures, decoded frames, tests and
  discussion there established much of the community knowledge needed to talk
  to APsystems inverters.
- **Koen Kanters (Koenkk)** and the Zigbee2MQTT/Z-Stack-firmware communities
  hosted and supported that long-running investigation. Koenkk's firmware
  repositories also made the predecessor's TI stack configuration auditable.
- **Bolk de Bruin** and contributors to
  [`bolkedebruin/openaps`](https://github.com/bolkedebruin/openaps) published
  reusable APsystems protocol work. This repository uses OpenAPS ideas and
  formats for the proprietary L1 AES envelope, inverter information/version
  reply layouts and `invdriver.gridprofile/v1` protection profiles and codecs.
- Contributors to
  [`ESP32-read-APS-inverters` issue #55](https://github.com/patience4711/ESP32-read-APS-inverters/issues/55)
  and
  [`RPI-APS-inverters` issue #70](https://github.com/patience4711/RPI-APS-inverters/issues/70)
  supplied the security and firmware-version observations that connected those
  implementations to the earlier projects.

## Platforms, specifications and libraries

- **Espressif** provides the ESP32-C6, Arduino-ESP32 core and raw IEEE 802.15.4,
  Wi-Fi, OTA, NVS/SPIFFS and coexistence facilities used by the refactor.
- **SunSpec Alliance** publishes the interoperable model conventions used by
  the read-only Modbus server. SunSpec names and marks belong to their owners.
- **Home Assistant** documentation and community examples informed the built-in
  Modbus integration guide.
- **Benoit Blanchon** — ArduinoJson.
- **ESP32Async contributors**, building on work including **Hristo Gochkov** —
  ESP Async WebServer and AsyncTCP.
- **Nick O'Leary** — PubSubClient.
- **Fabrice Weinberg** and Arduino Libraries contributors — NTPClient.
- **Michael Margolis** and **Paul Stoffregen** — Time library.
- **IoTeX** — PSACrypto.
- **sfrwmaker** — sunMoon sunrise/sunset calculations.

See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) for licensing details.
Acknowledgement does not imply endorsement of this project by any person or
organization listed here.

