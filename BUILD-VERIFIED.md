# Build verification

Verified on 2026-08-08 with:

- Arduino CLI 1.5.1
- Espressif Arduino core 3.3.8 (ESP-IDF 5.5.4)
- target `esp32:esp32:esp32c6`
- Zigbee mode `zczr`
- included custom 4 MB partition table
- ESP Async WebServer 3.12.0 and Async TCP 3.5.0

Result:

```text
Sketch: 1,669,972 bytes
Global variables: 66,256 bytes
Application slot: 1,769,472 bytes
Merged flash image: 4,194,304 bytes
```

The generated partition binary was decoded and verified as two 1,728 KiB OTA slots, 488 KiB SPIFFS, plus `zb_storage`, `zb_fct`, `rcp_fw`, and coredump partitions.
