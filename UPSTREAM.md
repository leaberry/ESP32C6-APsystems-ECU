# Upstream provenance

This repository was ported from:

- Repository: https://github.com/patience4711/ESP32-read-APS-inverters
- Source commit: `7b0ff63`
- License: GPL-3.0 (retained as `LICENSE`)

The APsystems payload builders, pairing sequence, inverter decoders, web application, MQTT formats and configuration behavior are upstream code. The principal port files are `ZIGBEE_A_TRANSPORT.ino`, `ZIGBEE_COORDINATOR.ino`, the simplified integrated-radio health check, `partitions.csv`, and the C6-related compatibility fixes documented in the initial commit.

The CC25xx firmware investigated for `SECURITY-AUDIT.md` came from the predecessor repository and the development thread linked there. It is evidence only and is not needed or redistributed in this C6 repository.
