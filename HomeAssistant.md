# Home Assistant using built-in Modbus

Home Assistant can read the ECU directly with its built-in Modbus integration.
HACS and a separate SunSpec integration are not required.

The ECU listens for Modbus/TCP on port `502`. Home Assistant only reads the
telemetry already cached by the ECU, so its persistent TCP connection and scan
interval do not cause extra radio traffic or change the inverter polling rate.

## Unit IDs and registers

The same registers are available on every unit ID:

| Unit ID | Data source |
|---:|---|
| 1 | All configured inverters combined |
| 2 | Inverter index 0 |
| 3 | Inverter index 1 |
| 4 | Inverter index 2 |
| 5-10 | Inverter indexes 3-8 |

| Address | Value | Home Assistant type | Conversion |
|---:|---|---|---|
| 84 | Current AC power | `int16` | watts |
| 94-95 | Recorded energy | `uint32` | watt-hours; multiply by `0.001` for kWh |

These are zero-based Modbus addresses. Do not add `40000` in the Home Assistant
configuration. The ECU also provides a complete SunSpec model starting at
register 40000 for SunSpec-aware clients; see [SUNSPEC.md](SUNSPEC.md).

## Basic site-total configuration

Add the following to Home Assistant's `configuration.yaml`. Replace
`192.168.20.45` with the reserved DHCP or static IP address of the ECU.

```yaml
modbus:
  - name: apsystems_ecu
    type: tcp
    host: 192.168.20.45
    port: 502
    timeout: 5
    message_wait_milliseconds: 50

    sensors:
      # Live production for dashboards and automations.
      - name: "APSystems solar power"
        unique_id: apsystems_solar_power
        slave: 1
        address: 84
        input_type: holding
        data_type: int16
        unit_of_measurement: W
        device_class: power
        state_class: measurement
        scan_interval: 30

      # Accumulated production for Home Assistant's Energy dashboard.
      - name: "APSystems solar production"
        unique_id: apsystems_solar_production
        slave: 1
        address: 94
        input_type: holding
        data_type: uint32
        scale: 0.001
        precision: 3
        unit_of_measurement: kWh
        device_class: energy
        state_class: total_increasing
        scan_interval: 30
```

### Using a separate include file

If `configuration.yaml` delegates the Modbus section to another file:

```yaml
modbus: !include includes/modbus.yaml
```

then `includes/modbus.yaml` represents the value of `modbus:` and must start
directly with the list of connections. Do not put another `modbus:` heading in
the included file:

```yaml
- name: apsystems_ecu
  type: tcp
  host: 192.168.20.45
  port: 502
  timeout: 5
  message_wait_milliseconds: 50

  sensors:
    - name: "APSystems solar power"
      unique_id: apsystems_solar_power
      slave: 1
      address: 84
      input_type: holding
      data_type: int16
      unit_of_measurement: W
      device_class: power
      state_class: measurement
      scan_interval: 30

    - name: "APSystems solar production"
      unique_id: apsystems_solar_production
      slave: 1
      address: 94
      input_type: holding
      data_type: uint32
      scale: 0.001
      precision: 3
      unit_of_measurement: kWh
      device_class: energy
      state_class: total_increasing
      scan_interval: 30
```

Additional Modbus connections go in the same included file as additional
top-level `- name: ...` list items.

`APSystems solar production` is the entity intended for the Energy dashboard.
It is the sum of all configured inverters. `APSystems solar power` is an
instantaneous W reading and is useful on ordinary dashboards, but it is not the
entity to select as an Energy source.

The recorded-energy counter starts when this firmware's energy history is
initialized; it is not the inverter's factory lifetime production counter. The
ECU stores finalized daily totals in flash and adds the current in-memory day to
the value served over Modbus.

## Per-inverter production

Add the following entries beneath the same `sensors:` key to expose power and
energy for three inverters. Change the display names as desired. The important
part is the `slave` value: 2 is inverter index 0, 3 is index 1, and 4 is index 2.

```yaml
      - name: "APSystems inverter 1 power"
        unique_id: apsystems_inverter_1_power
        slave: 2
        address: 84
        input_type: holding
        data_type: int16
        unit_of_measurement: W
        device_class: power
        state_class: measurement
        scan_interval: 30

      - name: "APSystems inverter 1 energy"
        unique_id: apsystems_inverter_1_energy
        slave: 2
        address: 94
        input_type: holding
        data_type: uint32
        scale: 0.001
        precision: 3
        unit_of_measurement: kWh
        device_class: energy
        state_class: total_increasing
        scan_interval: 30

      - name: "APSystems inverter 2 power"
        unique_id: apsystems_inverter_2_power
        slave: 3
        address: 84
        input_type: holding
        data_type: int16
        unit_of_measurement: W
        device_class: power
        state_class: measurement
        scan_interval: 30

      - name: "APSystems inverter 2 energy"
        unique_id: apsystems_inverter_2_energy
        slave: 3
        address: 94
        input_type: holding
        data_type: uint32
        scale: 0.001
        precision: 3
        unit_of_measurement: kWh
        device_class: energy
        state_class: total_increasing
        scan_interval: 30

      - name: "APSystems inverter 3 power"
        unique_id: apsystems_inverter_3_power
        slave: 4
        address: 84
        input_type: holding
        data_type: int16
        unit_of_measurement: W
        device_class: power
        state_class: measurement
        scan_interval: 30

      - name: "APSystems inverter 3 energy"
        unique_id: apsystems_inverter_3_energy
        slave: 4
        address: 94
        input_type: holding
        data_type: uint32
        scale: 0.001
        precision: 3
        unit_of_measurement: kWh
        device_class: energy
        state_class: total_increasing
        scan_interval: 30
```

The individual sensors are useful for comparing panels, graphs, and alerts. For
the Energy dashboard, normally add either the unit 1 site-total energy entity or
all of the individual energy entities, not both, or production will be counted
twice.

## Apply the configuration

1. Check the YAML with **Developer tools > YAML > Check configuration**.
2. Restart Home Assistant.
3. Confirm the new entities under **Settings > Devices & services > Entities**.
4. Open **Settings > Dashboards > Energy**.
5. Under **Solar panels**, add `sensor.apsystems_solar_production`.

Home Assistant polls every 30 seconds in these examples, while the ECU's default
inverter poll remains five minutes. Re-reading the cached values is harmless. A
longer `scan_interval` is also fine if faster dashboard updates are unnecessary.

## Troubleshooting

- Confirm the ECU web interface loads from the Home Assistant host's network.
- Confirm TCP port 502 is reachable and is not blocked between VLANs.
- Use a reserved DHCP address or static IP so the configured host does not move.
- Leave `input_type: holding` and `data_type: uint32` unchanged for the energy
  counter. Omit `swap` to use Home Assistant's default unswapped word order.
  Home Assistant automatically reads both registers for a `uint32`; do not add
  `count: 2`.
- If an individual entity is unavailable, verify the inverter's index in the ECU
  web interface and use unit ID `index + 2`.
- If values update in five-minute steps, that is expected with the ECU's default
  poll interval. The Home Assistant scan interval does not poll the inverters.

The authoritative Home Assistant configuration reference is the
[built-in Modbus integration documentation](https://www.home-assistant.io/integrations/modbus/).
