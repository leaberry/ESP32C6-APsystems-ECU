# Native ESP32-C6 transport and legacy ZNP operation map

The repository began as a ZNP-based ESP32 application, but the current project
is a broader refactor. This document covers only the compatibility layer that
allowed proven APsystems payloads and decoders to survive the radio replacement.

| Legacy TI operation | Purpose | Native ESP32-C6 implementation |
|---|---|---|
| `SYS_RESET_REQ` / GPIO reset | Reset CC25xx | Removed; the integrated radio is initialized once |
| `ZCD_NV_CHANLIST = channel 16` | Fixed RF channel | `esp_ieee802154_set_channel(16)` |
| `ZCD_NV_PANID` | ECU or inverter PAN | `esp_ieee802154_set_panid()`; learned per inverter |
| Coordinator short address | Receive destination | `esp_ieee802154_set_short_address(0x0000)` |
| `AF_REGISTER` endpoint `0x14` | APsystems endpoint | Encoded directly in APS headers |
| `AF_DATA_REQUEST` | Poll/query/reboot/throttle | Raw MAC + NWK + APS data frame |
| `AF_DATA_REQUEST_EXT`, mode `0x0F` | Pairing broadcasts | Raw broadcast on PAN `0xFFFF` |
| `AF_DATA_CONFIRM` | Transmit result | IEEE 802.15.4 transmit callbacks with coexistence retry |
| `AF_INCOMING_MSG` | Inverter response | Raw receive callback, APS parsing and legacy queue adapter |
| ZBOSS fragmentation | Multi-frame responses | Native APS fragment ACK and bounded reassembly |
| `UTIL_GET_DEVICE_INFO` | Health check | Integrated-radio start state |

## Why ZBOSS was removed

APsystems uses a proprietary NWK pairing command (`0x1009`) and inverters do
not join the coordinator as ordinary Zigbee neighbors. ZBOSS receives those RF
frames but rejects them before the application APS callback. It also owns the
ESP32-C6 transmit buffer, making a second raw transmitter unsafe in the same
image. Building with Arduino `ZigbeeMode=default` avoids both restrictions and
gives this transport exclusive ownership of the IEEE 802.15.4 callbacks.

## Preserved application protocol

The adapter strips only the ZNP serial envelope. Existing command builders still
produce the pairing clusters `0x020D`, `0x020C`, `0x010F`, `0x0101`, operational
request cluster `0x0006`, response cluster `0x0106`, and all APsystems
`FB FB ... FE FE` payload bytes. Plaintext and APsystems application-layer AES
remain above this transport.

The receive adapter reconstructs the subset of TI `AF_INCOMING_MSG` consumed by
the original decoders. A dedicated worker owns parsing and fragment ACKs; the
main application consumes completed messages through the original synchronous
`readZB()` interface. Wi-Fi, the refactored web application, MQTT and Modbus
continue independently. See [UPSTREAM.md](UPSTREAM.md) for the larger retained
and replaced boundary and [ACKNOWLEDGEMENTS.md](ACKNOWLEDGEMENTS.md) for credit.
