# SunSpec Modbus/TCP

The ESP32-C6 listens on TCP port 502 and exposes a read-only SunSpec chain at
holding-register address 40000. Modbus function codes 03 and 04 are supported.
The server reads cached telemetry only, so a long-lived client connection never
causes extra Zigbee traffic or blocks the APsystems poll scheduler.

| Unit ID | Device |
|---:|---|
| 1 | Aggregate of all configured inverters |
| 2 | Inverter index 0 |
| 3 | Inverter index 1 |
| 4-10 | Inverter indexes 2-8 |

Each unit contains the `SunS` marker, Common Model 1, single-phase Inverter
Model 101 and the end model. Model 101 publishes AC current, voltage, power,
frequency, recorded accumulated energy, combined DC values, cabinet temperature
and operating state. Unsupported points use SunSpec not-implemented sentinels.

Common Model 1 `Vr` contains the queried inverter firmware version for units
2-10. Unit 1 contains the ESP32 bridge firmware version because a fleet may
contain different inverter versions. The Model 101 `WH` counter is the
monotonic energy recorded since this firmware's history was initialized, not a
factory lifetime value reported by the inverter.

The server accepts normal 40000 addressing and clients that strip the 4xxxx
reference and request address 0. It also detects clients that begin at 40001
and applies their one-register offset for that TCP connection.

## Home Assistant

Home Assistant's built-in Modbus integration can read aggregate and per-inverter
power and energy without HACS. See [HomeAssistant.md](HomeAssistant.md) for a
complete `configuration.yaml` example and Energy dashboard instructions.

The server does not implement SunSpec control models or Modbus writes. Inverter
power limiting and grid-profile actions remain behind the existing authenticated
web/API paths.
