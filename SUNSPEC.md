# SunSpec Modbus/TCP

The ESP32-C6 listens on TCP port 502. It exposes a standards-shaped, read-only
SunSpec chain at holding-register address 40000 and supports Modbus function
codes 03 and 04.

| Unit ID | Device |
|---:|---|
| 1 | Aggregate of all configured inverters |
| 2 | Inverter index 0 |
| 3 | Inverter index 1 |
| 4–10 | Inverter indexes 2–8 |

Each unit contains:

1. `SunS` marker (`0x5375 0x6E53`)
2. Common Model 1, length 66
3. Single-phase Inverter Model 101, length 50
4. End model (`0xFFFF`, length 0)

The model publishes AC current, voltage, power, frequency, accumulated daily energy,
combined DC current/voltage/power, cabinet temperature, and operating state.
Unsupported Model 101 points use the correct SunSpec not-implemented sentinels.
The server accepts both the normal 40000 addressing and clients that strip the
4xxxx reference and request address 0. It also detects scanners that begin at
40001 and consistently applies their one-register offset for that connection.

## Home Assistant: automatic SunSpec discovery

Install `CJNE/ha-sunspec` through HACS, restart Home Assistant, then add the
**SunSpec** integration:

- Host: the ESP32-C6's fixed/reserved Wi-Fi IP address
- Port: `502`
- Slave ID: `1`

The integration discovers Models 1 and 101 and creates the usual inverter
sensors. Repeat with slave IDs 2 onward to create a device for each inverter.

## Home Assistant: generic Modbus connection

Home Assistant's built-in Modbus integration can also connect to the server:

```yaml
modbus:
  - name: apsystems_esp32c6
    type: tcp
    host: 192.168.1.50
    port: 502
    timeout: 5
```

The generic integration requires individual sensor definitions; use a SunSpec
client when automatic model discovery and scaling are preferred.

## Scope

The server is intentionally read-only. It does not expose SunSpec control
models or accept Modbus writes; inverter throttling remains available through
the existing web, HTTP, and MQTT paths. Telemetry is the most recently decoded
poll, so its refresh cadence follows the firmware's APsystems polling interval.
The inherited decoder's energy counter resets each day; Model 101 `WH` therefore
also resets daily and is not a lifetime-production counter.
