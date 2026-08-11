# Upstream provenance and refactor boundary

## Starting point

- Repository: <https://github.com/patience4711/ESP32-read-APS-inverters>
- Imported source commit: `7b0ff63`
- Original author/copyright: patience4711 (Johannes), 2023
- License: MIT, retained verbatim in `LICENSE`
- Historical predecessors:
  - <https://github.com/patience4711/read-APSystems-YC600-QS1-DS3>
  - <https://github.com/patience4711/RPI-APS-inverters>

The Git remote named `upstream` tracks the original ESP32 repository. The
current project is a descendant, not a drop-in upstream branch and not merely a
board-definition port.

## Substantially retained concepts and code

The strongest inherited areas are APsystems L2 payload construction, the
four-step pairing sequence, YC600/QS1/DS3 telemetry layouts and conversions,
inverter configuration structures, MQTT message formats and the synchronous
`readZB()` decoder boundary. Many files have since received bug fixes and safety
changes, but their working protocol lineage belongs to the upstream projects.

## Replaced or newly implemented here

- external CC2530/CC2531, UART and TI ZNP coordinator operations;
- raw ESP32-C6 IEEE 802.15.4/NWK/APS transmit, receive, fragment acknowledgement
  and reassembly;
- proprietary pairing-route learning and multi-PAN peer persistence;
- APsystems L1 AES envelope support based on OpenAPS/community research;
- cooperative fleet scheduler, contention retry and Modbus isolation;
- native Wi-Fi provisioning, DHCP hostname and static IPv4 support;
- modern web routes/pages, diagnostics, API additions and safer validation;
- firmware/model query, grid-profile guardrails, energy history/statistics and
  history backup/restore/wipe;
- read-only SunSpec/Modbus TCP and Home Assistant documentation; and
- 4/8 MB partition layouts, OTA UX, reproducible CI artifacts and releases.

The original CC25xx binaries were investigated as evidence for
`SECURITY-AUDIT.md`; they are neither required nor redistributed here. More
detailed community credit is in [ACKNOWLEDGEMENTS.md](ACKNOWLEDGEMENTS.md).
