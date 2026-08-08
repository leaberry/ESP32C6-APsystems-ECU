# ZNP-to-ESP32-C6 operation map

| Legacy TI operation | Purpose | ESP32-C6 implementation |
|---|---|---|
| `SYS_RESET_REQ` / GPIO reset | Reset CC25xx | Removed; integrated stack is started once in its own FreeRTOS task |
| `ZB_WRITE_CONFIGURATION: ZCD_NV_LOGICAL_TYPE=0` | Coordinator role | `esp_zb_init()` with `ESP_ZB_DEVICE_TYPE_COORDINATOR` |
| `ZCD_NV_CHANLIST = channel 16` | Fixed RF channel | `esp_zb_set_primary_network_channel_set(1UL << 16)` |
| `ZCD_NV_PANID` | ECU PAN | `esp_zb_set_pan_id()` |
| `ZCD_NV_EXT_PAN_ID` | ECU extended PAN | `esp_zb_set_extended_pan_id()` |
| `AF_REGISTER` endpoint `0x14` | APsystems application endpoint | `esp_zb_ep_list_add_ep()` with profile `0x0F05`, device `0x0100` |
| `ZB_START_REQUEST` | Form network | BDB initialization and network formation signals |
| `AF_DATA_REQUEST` | Poll/query/reboot/throttle | `esp_zb_aps_data_request()` to the inverter short address |
| `AF_DATA_REQUEST_EXT`, mode `0x0F` | Four pairing broadcasts | `esp_zb_aps_data_request()` to short broadcast `0xFFFF` |
| `AF_DATA_CONFIRM` | Transmit result | raw APS confirm callback |
| `AF_INCOMING_MSG` | Inverter response | raw APS indication callback; adapted to the legacy decoder layout in RAM |
| `UTIL_GET_DEVICE_INFO` | Health check | internal stack formation state (`zbStarted`) |

## Preserved wire protocol

The adapter strips only the ZNP transport fields. It does not create or reinterpret APsystems application data. Existing command builders still produce:

- pairing clusters `0x020D`, `0x020C`, `0x010F`, `0x0101`
- operational request cluster `0x0006`
- response cluster `0x0106`
- all `FB FB ... FE FE` APsystems payload bytes for polling, query, reboot, and throttle

The receive adapter reconstructs the subset of TI's `AF_INCOMING_MSG` structure consumed by the original decoder: group, cluster, source address, endpoints, broadcast flag, LQI, security byte, timestamp, sequence, payload length, and payload. This intentionally avoids rewriting the extensively tested inverter parsers.

## Concurrency

The Zigbee stack runs in its own FreeRTOS task. APS callbacks copy frames into a bounded queue; the Arduino application task consumes them synchronously through the original `readZB()` interface. This avoids calling web, MQTT, SPIFFS, or decoder code from the Zigbee callback context. Wi-Fi, web serving and MQTT continue to run through their existing Arduino/FreeRTOS tasks; a second symmetric application core is not required.

## Remaining blocker

There is no API blocker: Espressif exposes all required APS request, confirm and indication primitives. The remaining blocker is empirical RF testing of TI address mode `0x0F` versus the standard `0xFFFF` broadcast mapping. See `LIMITATIONS.md`.
