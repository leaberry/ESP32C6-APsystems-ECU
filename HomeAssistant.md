# Home Assistant

Choose one connection method for your Energy dashboard:

| Method | Setup | Controls | Where the energy counter is kept |
|---|---|---|---|
| MQTT discovery | Enable MQTT and let HA find the devices | Per-input power limit | Retained MQTT broker data, with pending changes in ECU RAM |
| Modbus | Add YAML below; no MQTT broker needed | Read-only | ECU daily history plus today's RAM total |

MQTT is the simplest option if you already have a broker. Both methods read
cached telemetry and do not add inverter radio polls. You can enable both,
but **do not add both copies of production to Energy**. Their counters start
from different records and need not match.

- [MQTT setup](#mqtt-setup)
- [Energy dashboard](#energy-dashboard-with-mqtt)
- [Power limits](#mqtt-power-limits)
- [MQTT backup and recovery](#mqtt-backups-and-counter-recovery)
- [Modbus setup and YAML](#modbus-setup-and-yaml)

## MQTT setup

1. Set up a broker reachable by Home Assistant and the ECU. If you use Home
   Assistant OS, the Mosquitto broker app is one option. Follow the
   [official MQTT setup guide](https://www.home-assistant.io/integrations/mqtt/).
2. Enable broker persistence so retained messages survive a broker restart.
   Use the broker's backup procedure too; retained messages are not a substitute
   for a backup.
3. In Home Assistant, open **Settings > Devices & services**, add **MQTT** if
   needed, and connect it to your broker. Leave discovery enabled.
4. In the ECU's **Menu > MQTT** page, under **Shared broker**, enter the same broker address,
   port and a permitted username/password. For ordinary local MQTT the port is
   usually `1883`. The ECU uses plain MQTT/TCP, not TLS or WebSockets.
   Select **Test broker connection** to check the values above before saving.
   A blank password uses the saved password. The result confirms whether the
   broker accepted the connection and login. It does not test permission to
   publish or subscribe to topics. The test works with either mode disabled
   and does not save settings or send messages to your automation topics.
5. Turn on **Enable/Disable Home Assistant**. Leave the discovery prefix at
   `homeassistant` unless you changed it in HA too.
6. For HA alone, leave **Enable/Disable Domoticz** off. To use both, turn it on
   and keep your existing message format, topic and device ID. Each switch
   reveals its mode's settings. Turning a mode off keeps its saved settings.
   Select **Save and restart ECU**. Broker credentials are shared; a blank
   password keeps the current password. The old Home Assistant page redirects
   here.
7. In HA, open the MQTT integration and look for the fleet device and its
   inverter devices. Allow time for discovery and a normal inverter poll.

The existing Domoticz MQTT payloads, topics and handler are unchanged. HA uses
a separate client ID, `aps-ha-<ECU_ID>`, and topics under `aps_ecu/<ECU_ID>/`.
Only one running ECU may use an ECU_ID. Both modes can share a broker without
using the same MQTT client ID. On-device load with both active still needs
hardware testing; HA sends a paced state cycle every 15 seconds.

Discovery follows Home Assistant's
[device discovery format](https://www.home-assistant.io/integrations/mqtt/#mqtt-discovery).
It supplies a fleet device and one device per inverter serial number. It
publishes solar power and energy, temperature, AC voltage/frequency and
connected-panel power. Optional diagnostic entities include today's production,
radio signal, panel voltage/current, ECU Wi-Fi signal and uptime. Some diagnostics
are disabled by default; enable the ones you need in the device's entity list.

In v1.4.14, a reporter confirmed that both DS3s appeared separately in HA using
this mode. Their screenshot shows separate power, energy, temperature, voltage,
frequency and panel readings. They also reported successful YC600 pairing and
operation. See the [field verification record](BUILD-VERIFIED.md#issue-17-reporter-verification-2026-09-16-v1414)
for the evidence and limits. Throttling was explicitly not tested; displaying
the power-limit control does not confirm that an inverter accepts its commands.

## Energy dashboard with MQTT

1. Open **Settings > Dashboards > Energy** in Home Assistant.
2. Add a **Solar production** source under the solar section.
3. Choose the fleet's **Solar energy** sensor. If the page offers a power
   sensor field, choose that fleet's **Solar power** sensor too.
4. Save and allow time for HA to collect statistics. It cannot fill in energy
   from before the sensor started reporting.

Alternatively, select the individual inverter **Solar energy** sensors.
Choose either the fleet or its individual inverters, **not both**, or the
same production will be counted twice. Do not also add the Modbus total.

Solar energy uses kWh, device class `energy`, and state class `total_increasing`,
which meet the [Energy sensor requirements](https://www.home-assistant.io/docs/energy/faq/).
Discovery makes the sensor eligible; it does not choose your Energy dashboard
sources automatically. **Production today** is a separate diagnostic counter
that resets daily and is not the suggested Energy source.

The MQTT counter measures production observed after HA mode is enabled. It
starts at zero for a new installation and does not import old ECU history.
It does not reset at midnight, on inverter reordering or when an inverter is
removed. Raw inverter counters that reset are handled by the existing telemetry
baseline logic. Production while the ECU is off, including the first baseline
interval after reboot, cannot be reconstructed.

## MQTT power limits

Each inverter has a **Per-input power limit** number. It accepts **20–500 W per
connected PV input**, not a whole-inverter total or a percentage. For example,
100 W on each of two DS3 inputs requests about 200 W total. The existing
calibration is applied; 500 W requests normal maximum output.

A command requires recent inverter telemetry. The ECU checks the reply before
reporting success. A failed command leaves the limit unknown; the diagnostic
**Power limit status** explains rejection or failure. Commands from an older
MQTT connection are rejected so an old retained command cannot replay at boot.

HA limit commands change **RAM only** and do not save a new boot-time limit to
ECU flash. They are not a durable schedule or a promise that an inverter resets
its own limit when the ECU restarts. Use an automation to deliberately reapply
a desired target when the inverter is available. Real inverter controls still
need model-specific hardware testing.

## MQTT backups and counter recovery

Keep the broker's data as well as the ECU's
[settings and history backups](README.md#back-up-settings-and-production-data).
They are separate:

| Data | Location | How it is used |
|---|---|---|
| ECU_ID and HA configuration | ECU settings JSON backup | Preserves discovery identity on a replacement board |
| MQTT solar totals | Retained `aps_ecu/<ECU_ID>/energy` | Recovers cumulative counters after restart |
| Discovery inventory | Retained `aps_ecu/<ECU_ID>/inventory` | Tracks this ECU's discovery topics for later cleanup |
| HA Energy statistics | Home Assistant database/backup | Stores HA's own charts and long-term statistics |

For Mosquitto, check its
[persistence settings](https://mosquitto.org/man/mosquitto-conf-5.html) and back up
its data using your installation's supported procedure. For a Home Assistant
broker app, include that app's data in your [Home Assistant backup](https://www.home-assistant.io/common-tasks/general/#backups) and verify that it is selected.
A separate broker needs its own backup. Test your backup procedure before you
need it; a firmware upgrade cannot restore missing broker data.

At startup the ECU recovers its retained checkpoint before publishing energy.
New deltas stay in RAM until the broker echoes a checkpoint. A boot identifier
prevents counting the same interval twice when a checkpoint arrived but its echo
was lost. This adds **no automatic counter or discovery writes to ECU flash**.
Explicit configuration saves still write settings; existing daily history
writes are unchanged.

An echo proves broker receipt, not that its disk has been flushed. An unexpected
power loss can lose pending production. Losing both broker checkpoints and ECU
RAM loses the baseline. Keep the original broker data and restore the original
ECU_ID when [replacing the ECU](README.md#replace-a-failed-ecu-board). An old broker
backup may contain an older counter, so inspect HA statistics after recovering
an older backup. Do not edit or publish a zero checkpoint to fix an unavailable
sensor.

Disabling HA removes this ECU's registered discovery entries but keeps its
energy checkpoint. Before changing brokers, disable HA while the **old** broker
is still reachable so cleanup can finish. A prefix change cleans up old discovery
topics on the current broker. Avoid deleting its inventory by hand.

The retained counter archive holds 64 inverter serials, including removed ones,
so removing an inverter does not reduce the fleet total. Invalid checkpoints or
an exhausted archive make energy unavailable rather than silently resetting it.

## MQTT troubleshooting

- **No devices:** check broker address, credentials and port on both systems;
  check that HA mode and discovery are enabled and the prefixes match.
- **Devices appear but energy stays unavailable:** check broker persistence and
  permissions. The ECU must read and write `aps_ecu/<ECU_ID>/#`, write its topics
  under `<prefix>/device/aps_ecu_<ECU_ID>.../config`, and read `<prefix>/status`.
  Discovery inventory and counter recovery both need incoming broker messages.
- **Control unavailable:** wait for a fresh successful poll during daylight.
  Busy or unavailable inverters reject commands.
- **Readings change in five-minute steps:** the default inverter poll is five
  minutes. Publishing cached readings faster does not generate fresh samples.
- **Energy seems doubled:** check that you did not select fleet plus individual
  inverters, or MQTT plus Modbus, as sources in the Energy dashboard.

## Modbus setup and YAML

Home Assistant can read the ECU directly with its built-in Modbus integration.
HACS and a separate SunSpec integration are not required.

The SunSpec/Modbus TCP service is enabled by default under **Menu > Polling and
access**. While enabled, the ECU listens on port `502`. Home Assistant only
reads telemetry already cached by the ECU, so its persistent TCP connection and
scan interval do not cause extra radio traffic or change the inverter polling
rate.

### Unit IDs and registers

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

### Basic site-total configuration

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

#### Using a separate include file

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

An administrator restore or wipe on the ECU's Energy history page can move or
reset this counter. Home Assistant's `total_increasing` handling normally treats
a decrease as a new meter cycle, but review the Energy dashboard after an
intentional history operation. A restorable backup contains finalized records
only; the volatile current day begins again after restore.

### Per-inverter production

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

### Apply the configuration

1. Check the YAML with **Developer tools > YAML > Check configuration**.
2. Restart Home Assistant.
3. Confirm the new entities under **Settings > Devices & services > Entities**.
4. Open **Settings > Dashboards > Energy**.
5. Under **Solar panels**, add `sensor.apsystems_solar_production`.

Home Assistant polls every 30 seconds in these examples, while the ECU's default
inverter poll remains five minutes. Re-reading the cached values is harmless. A
longer `scan_interval` is also fine if faster dashboard updates are unnecessary.

### Troubleshooting

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
