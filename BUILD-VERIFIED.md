# Build verification

Verified on 2026-08-08 with:

- Arduino CLI 1.5.1
- Espressif Arduino core 3.3.8
- target `esp32:esp32:esp32c6`
- Zigbee mode `zczr`
- included custom 4 MB partition table
- ESP Async WebServer 3.12.0 and Async TCP 3.5.0

Result:

```text
Sketch: 1,708,230 bytes
Application binary: 1,708,336 bytes
Global variables: 71,032 bytes
4 MB application slot: 3,538,944 bytes
4 MB application-slot margin: 1,830,608 bytes
8 MB OTA application slot: 3,145,728 bytes
8 MB OTA application-slot margin: 1,437,392 bytes
```

The boot-time APsystems AES known-answer test uses the published reverse-
engineered vector. The final build includes the mixed plaintext/AES transport,
multi-client SunSpec Modbus/TCP server, and web transport-status fields.

The 4 MB partition image was decoded as one 3,456 KiB factory application and
488 KiB SPIFFS. The 8 MB image contains two 3 MiB OTA applications and 1,896
KiB SPIFFS. Both retain `zb_storage`, `zb_fct`, `rcp_fw`, and coredump partitions.
