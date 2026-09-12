# ESP32-C6 APsystems ECU

This project turns an ESP32-C6 board into a local controller (ECU) for up to
nine APsystems YC600, QS1 or DS3 solar microinverters. It reads production over
the board's built-in radio and shows it in a web page. No separate Zigbee module
or cloud account is needed.

Use an **8 MB ESP32-C6** board if possible. It supports updates over Wi-Fi (OTA).
A **4 MB ESP32-C6** also works, but updates require USB. Check the board's flash
size; the chip name alone does not tell you the size.

Plaintext DS3 pairing and polling have been tested on hardware. YC600, QS1,
encrypted communication and replacement-board pairing still need more field
testing. See [LIMITATIONS.md](LIMITATIONS.md).

## Find the instructions you need

- [Install on a new board](#install-on-a-new-board)
- [Connect Wi-Fi and set up the ECU](#connect-wi-fi-and-set-up-the-ecu)
- [Add and pair inverters](#add-and-pair-inverters)
- [Back up settings and production data](#back-up-settings-and-production-data)
- [Upgrade without losing settings or data](#upgrade-without-losing-settings-or-data)
- [Restore settings or production data](#restore-settings-or-production-data)
- [Replace a failed ECU board](#replace-a-failed-ecu-board)
- [Understand ECU_ID](#understand-ecu_id)
- [Connect Home Assistant](#connect-home-assistant)
- [Build from source](DEVELOPMENT.md)

## Install on a new board

These steps are for a blank board or a deliberate fresh start. **They erase the
board's old settings, pairing and production data.** For an existing ECU, use
[the upgrade instructions](#upgrade-without-losing-settings-or-data) instead.

### 1. Download the right files

Open [GitHub Releases](https://github.com/leaberry/ESP32C6-APsystems-ECU/releases)
and choose the release you want. Download and unzip the bundle for your board:

| Board | Bundle | First-install file inside |
|---|---|---|
| 8 MB | `ESP32C6-APsystems-ECU-8mb-ota.zip` | `ESP32C6_ECU-8MB-OTA.merged.bin` |
| 4 MB | `ESP32C6-APsystems-ECU-4mb-noota.zip` | `ESP32C6_ECU-4MB-noOTA.merged.bin` |

If a change is not in a release yet, signed-in GitHub users can download the
matching artifact from a **successful** [Actions build](https://github.com/leaberry/ESP32C6-APsystems-ECU/actions).
Check `BUILD-INFO.txt` for its version and commit. Keep the bundle; it also
contains the application image needed for later updates.

### 2. Set up the USB tool

The commands below use **Windows PowerShell** and Python 3. Install Python from
[python.org](https://www.python.org/downloads/) if `py --version` does not work.
Open PowerShell in the folder containing the unzipped firmware files, then run:

```powershell
py -m pip install "esptool>=5,<6"
py -m serial.tools.list_ports
```

Connect the board with a USB **data** cable. Use the port shown for your board.
The examples use `COM7`; replace it everywhere with your actual port. Close any
serial monitor before using the port. On Linux or macOS, use `python3` instead
of `py` and your device path instead of `COM7` (for example `/dev/ttyACM0`).

Check the detected flash size:

```powershell
py -m esptool --chip esp32c6 --port COM7 flash-id
```

If it cannot connect, hold **BOOT**, press and release **RESET**, then release
**BOOT** and retry. Some boards need a manual RESET after flashing too.

### 3. Flash the new board

For an **8 MB** board:

```powershell
py -m esptool --chip esp32c6 --port COM7 erase-flash
py -m esptool --chip esp32c6 --port COM7 --baud 460800 write-flash 0x0 ESP32C6_ECU-8MB-OTA.merged.bin
```

For a **4 MB** board, use these commands instead:

```powershell
py -m esptool --chip esp32c6 --port COM7 erase-flash
py -m esptool --chip esp32c6 --port COM7 --baud 460800 write-flash 0x0 ESP32C6_ECU-4MB-noOTA.merged.bin
```

Wait for a successful write and verification, then reset the board. If transfers
fail, try `--baud 115200`. Never put the 8 MB image on a 4 MB board.

For a graphical Windows tool, use Espressif's
[Flash Download Tool](https://docs.espressif.com/projects/esp-test-tools/en/latest/esp32c6/production_stage/tools/flash_download_tool.html):
select **ESP32-C6**, **Develop**, the merged file at address `0x0`, **DoNotChgBin**,
and your COM port. This is also a first-install procedure.

## Connect Wi-Fi and set up the ECU

1. Connect your phone or computer to the ECU's open Wi-Fi network,
   named `aps-ecu-xxxxxx`.
2. Open `http://192.168.4.1/` if the setup page does not appear.
3. Enter your **2.4 GHz** Wi-Fi name and password. You can type a hidden name.
4. Choose a hostname, such as `solar-ecu`. Leave **DHCP** enabled unless you
   already know the static IP address, netmask and gateway you need.
5. Set an administrator password and save. The ECU restarts.
6. Rejoin your normal Wi-Fi. Find the ECU's IP address in your router's device
   list, then open that address in a browser, for example `http://192.168.1.50/`.
7. Sign in as `admin` with the password you chose. In **Menu > Polling and
   access**, also change the read-only `user` password. Factory defaults are
   `admin` / `0000` and `user` / `1111`. New passwords must have 8–32 printable
   characters with no spaces, and the two passwords must differ.
8. In **Menu > Time and location**, enter your latitude, longitude and time
   zone. Leave the NTP server as `pool.ntp.org`, or enter your own server's
   hostname or IPv4 address without `http://` or a port.
9. Leave automatic polling at **300 seconds** while checking your installation.
   Correct time and location let the ECU stop polling overnight.

Reserve the ECU's IP address in your router so bookmarks keep working. The ECU
requests your hostname, but the router controls its displayed name and local
DNS records. Use the IP address if the name does not work. There is no mDNS
(`.local`) service. Static IP mode does not send a DHCP hostname.

Keep the board near the inverters, away from metal, dry and out of direct sun.
Use a stable USB power supply. If Wi-Fi cannot reconnect, the ECU can return to
its setup access point.

### Choose an antenna, if needed

**Menu > Antenna** starts at **Unmanaged**, which leaves antenna pins alone.
For a supported switched-antenna board, select **Internal** or **External**, then
choose its board name. The page includes a photo link to help identify it.

The **Seeed Studio XIAO ESP32-C6** preset uses GPIO3 LOW to enable the switch,
and GPIO14 LOW for internal or HIGH for external. **Advanced** lets you enter
chip GPIO numbers, polarity and an optional enable pin. Use that only with the
board's schematic. Save and restart. Connect an external antenna before
selecting External. Do not copy GPIO settings from a different board.

## Add and pair inverters

Pair during daylight, when the inverter is powered. Place the ECU close enough
for a strong radio signal.

1. Open **Menu > Inverters > Add inverter**.
2. Enter the inverter's printed **12-digit serial number**.
3. Select its model and a useful name, such as `Garage roof`.
4. Mark only the PV inputs that have panels connected. DS3 has two inputs.
5. Leave calibration at its default unless you have a verified correction.
   The Domoticz index is only needed for the legacy Domoticz MQTT format.
6. Select **Save inverter** first, then **Pair inverter**. Wait for the result.
7. Check the dashboard after a polling cycle. Look for a recent poll time and
   sensible readings. The first reading after reboot establishes a baseline
   and can show zero power; wait for the next cycle.
8. Repeat for the other inverters, then download a settings backup.

If pairing fails, check the serial, model, inverter power and distance. See the
pairing status and **Menu > Diagnostics** before erasing anything. Do not erase
settings or change ECU_ID as a routine pairing fix.

## Back up settings and production data

There are **two different ECU backups**. Save both before an upgrade, and save
settings again after adding or pairing an inverter. Keep dated copies.

| Download | What it saves | What it does not save |
|---|---|---|
| Settings `.json` | ECU_ID, radio identity, inverter list and pairing, saved limits, network, login, MQTT, antenna, time and polling settings | Production history and live data |
| Restorable history `.bin` | Finished daily production records | Today's unfinished total, hourly readings, settings and pairing |
| CSV | Readable production data, including today | It cannot be uploaded to restore history |

**Settings contain passwords in plain text.** Keep them private. Neither ECU
backup contains Home Assistant's broker-retained counters. Back up the MQTT
broker separately if you use [Home Assistant MQTT](HomeAssistant.md#mqtt-backups-and-counter-recovery).

### Download settings

1. Sign in as `admin`.
2. Open **Menu > Settings backup**.
3. Select **Download settings backup (.json)**.
4. Check that a nonempty `.json` file was saved to your computer.

The settings file also includes the original radio IEEE address and learned
peer records needed when replacing the board. Inverter grid-protection settings,
diagnostic logs and crash dumps are not included.

### Download production history

1. Open **Menu > Energy history**.
2. Select **Download restorable backup** and keep the `.bin` file.
3. Also select **Download CSV** if you want a readable copy of today's readings.

Immediately before a planned restart or power-off, select **Save today to flash
now** if you want to preserve today's total on the **same board**. This manual
save does not finish the day or add it to the downloadable history backup.
After restart, the ECU reloads the saved total; hourly charts and detailed
operating statistics start again. Production since that manual save can be lost.

For a board replacement, today's checkpoint is not transferred by the two web
backups. If possible, wait until the day is finalized, then download history
again. Do not change the clock to force a day to finish.

## Upgrade without losing settings or data

Use these steps only when keeping the **same partition layout**. A partition
layout is the map that separates firmware from settings and history in flash.
Changing that map requires backups and a fresh installation followed by restore.

First download both backups above. For USB upgrades, save today's total just
before flashing. The OTA page offers the automatic save described below.
Keep your administrator password, ECU IP address and firmware bundle handy.

### OTA: update an 8 MB board over Wi-Fi

1. Open **Menu > System information** and check **OTA available: Yes**.
2. Download `ESP32C6_ECU-8MB-OTA.bin` for the new release. This is the
   **application** file; its name does **not** contain `.merged`, `.bootloader`
   or `.partitions`.
3. Open **Menu > Firmware update** and choose that file. Leave **Save current
   day totals from ram** checked, then select **Install firmware**. The ECU
   saves today's production before writing the image. A failed save stops the
   update; fix the reported problem and retry. A day with no production needs
   no save. Uncheck the box only if you accept losing unsaved totals.
   This saves daily totals, not the hourly chart. On older firmware without
   this checkbox, use **Energy history > Save today to flash now**
   before updating.
4. Keep power and Wi-Fi connected until the page reports success. Select
   **Restart ECU** when offered.
5. Reopen the ECU and check its version, inverter list, polling and history.

OTA writes a spare firmware slot and keeps settings, pairing and history.
It cannot change the partition layout and is unavailable on 4 MB boards.

### USB: update either board size while keeping its data

Do **not** run `erase-flash`, use `--erase-all`, or flash a `.merged.bin` for
this procedure. Even without a separate erase command, a merged file can write
across settings areas. The repository's `Flash-Firmware.ps1` helper writes a
merged image; `-SkipErase` does **not** make it a data-preserving upgrade tool.

Use the [USB tool setup](#2-set-up-the-usb-tool) above. Download and unzip the
new release bundle, then open PowerShell in that folder. Close serial monitors.
Replace `COM7` with your board's port.

Optionally save a complete emergency image of this board before writing:

```powershell
py -m esptool --chip esp32c6 --port COM7 read-flash 0 ALL ecu-before-upgrade.bin
```

Keep this file private; it contains settings and passwords. It is a same-board
recovery image, not the normal settings backup for a replacement board.

**Check the partition map before upgrading.** Read the 3,072-byte table:

```powershell
py -m esptool --chip esp32c6 --port COM7 read-flash 0x8000 0xC00 installed-partitions.bin
```

For **8 MB**, compare it to the new release's table:

```powershell
(Get-FileHash .\installed-partitions.bin -Algorithm SHA256).Hash
(Get-FileHash .\ESP32C6_ECU-8MB-OTA.partitions.bin -Algorithm SHA256).Hash
```

For **4 MB**, use `ESP32C6_ECU-4MB-noOTA.partitions.bin` in the second command.
The two hashes must match. If they differ, stop: use the partition-change
procedure below instead. Do not write a new partition table over existing data.

For an **8 MB** board with the matching layout, write the application to both
firmware slots. This covers whichever slot the bootloader currently selects:

```powershell
py -m esptool --chip esp32c6 --port COM7 --baud 460800 write-flash 0x10000 ESP32C6_ECU-8MB-OTA.bin 0x310000 ESP32C6_ECU-8MB-OTA.bin
```

For a **4 MB** board with the matching layout, write its single application:

```powershell
py -m esptool --chip esp32c6 --port COM7 --baud 460800 write-flash 0x10000 ESP32C6_ECU-4MB-noOTA.bin
```

These addresses come from this repository's partition files. The commands
leave settings, pairing and history areas alone. The 8 MB command replaces both
firmware copies, so it does not retain the old version as an OTA fallback.
Keep power stable and wait for successful verification. Reset, then check the
version, inverter list and production history. If an update fails, reconnect in
BOOT mode and retry the same application-only command.

On Linux use `sha256sum` for the two table files; on macOS use `shasum -a 256`.
The esptool command syntax is described in
[Espressif's reference](https://docs.espressif.com/projects/esptool/en/latest/esp32c6/esptool/basic-commands.html).
No USB write or power-loss test was performed as part of this documentation edit.

### Change layouts, or recover a board that needs a fresh installation

1. Download settings and restorable history while the old firmware still works.
2. Keep a CSV too if you need a record of the unfinished day.
3. Follow [Install on a new board](#install-on-a-new-board) with the correct
   merged image. This step erases data.
4. Set up Wi-Fi, then restore settings and history using the next section.

If you cannot reach the web interface, a complete USB read can preserve an
emergency copy before an erase. Do not assume a raw image can be moved to a
different board or layout. Ask for help before erasing the only copy of your data.

## Restore settings or production data

Restore **settings first**, then the matching production history. History stores
values by inverter slot, so keep the original inverter order when restoring it.
Settings restore does not restore history. History restore does not restore
settings, pairing, or broker-retained Home Assistant counters.

### Restore settings

1. Sign in as `admin` and open **Menu > Settings backup**.
2. Choose the settings `.json` file (maximum 32 KB).
3. Leave **Restore Wi-Fi credentials, hostname and IP settings** unchecked to
   keep the board's current connection. Check it only if you want the saved
   network settings and know how to reconnect afterward.
4. Leave **Restore antenna board and GPIO settings** unchecked unless the
   saved wiring matches this board.
5. Select **Validate and preview**. Check the ECU_ID, inverter count and radio
   address. Preview does not change settings.
6. Select **Restore settings and restart**, then accept the confirmation.
7. Wait for restart. Sign in with the **administrator password from the backup**.
   That password is restored even when the network checkbox is off.
8. Check the inverter list, time, antenna choice and polling.

If validation fails, keep the original file and read the error. Do not edit the
checksum to bypass validation. If power fails during restore, the ECU keeps a
pending restore file and retries at boot. If startup stops, save the serial
error and troubleshoot storage; do not erase the pending restore.

### Restore production history

1. Open **Menu > Energy history** and download a backup of any history currently
   on the destination board that you want to keep.
2. In **Restore history**, choose the **restorable `.bin` backup**, not the CSV,
   settings JSON, or firmware `.bin`.
3. Submit the restore and accept its confirmation. Wait for the success message.
4. Check several dates and inverter totals in the daily table.

Restore **replaces** finished daily records; it does not merge two histories.
It also resets today's counters, hourly chart and operating statistics. The ECU
checks the uploaded records before replacing its history. Keep the backup if an
upload fails and check available storage, especially when moving from 8 MB to
4 MB. **Permanently wipe history** is not part of a normal restore or upgrade.

## Replace a failed ECU board

1. If the old ECU still works, download fresh settings and history backups.
   Keep the MQTT broker's retained data too if you use Home Assistant MQTT.
2. **Turn the old ECU off.** Two powered ECUs must not use the same installation
   identity at the same time.
3. Install firmware on the replacement board and set up its Wi-Fi. It is okay
   if the fresh board creates a temporary new ECU_ID.
4. Restore the old settings JSON. This replaces the temporary ECU_ID with the
   **saved original ECU_ID** and restores the original radio IEEE address and
   pairing records. Keep network and antenna restore unchecked unless you
   specifically want those saved settings on this board.
5. Sign in using the old backup's password. Check the previewed/restored ECU_ID
   and inverter list. The replacement board keeps its own Wi-Fi MAC address,
   so the router may give it a different IP address.
6. Restore the matching history `.bin`. Keep the original inverter slot order.
7. Check recent polling in daylight. Pairing records are restored, but continuity
   on every hardware/inverter combination has not yet been field-tested. If an
   inverter does not respond, inspect diagnostics and pair that inverter again.
8. For Home Assistant MQTT, keep the original broker and its retained energy
   checkpoint. The restored ECU_ID lets the new board recover those counters
   and use the same discovery identities. See [counter recovery](HomeAssistant.md#mqtt-backups-and-counter-recovery).

Without a settings backup, a new board cannot automatically recover the old
ECU_ID, radio identity or pairing records. A history backup alone is not enough;
you will need to configure and pair the inverters again. Do not copy a complete
flash image to a different board as a substitute for settings restore.

## Understand ECU_ID

ECU_ID is this installation's 12-character hexadecimal identifier. It is used
in inverter pairing, polling and control, and in Home Assistant MQTT identities.
It is separate from the board's Wi-Fi MAC address and its radio IEEE address.

On a fresh installation, firmware replaces the old default `D8A3011B9780` with
a random ID and saves it. It only does this when **no pairing records exist**.
Existing custom IDs and paired installations keep their current ID. Reboots,
OTA and same-layout application-only USB updates keep the saved ID.

Settings backup includes ECU_ID. Settings restore puts that value back before
the radios start, including on a replacement board. Do not manually change an
ID on a working paired installation. A full erase without a settings restore
can create a new installation identity. The legacy MQTT client ID still follows
the board MAC; Home Assistant's separate client ID follows ECU_ID.

## Connect Home Assistant

Use the consolidated [Home Assistant guide](HomeAssistant.md) for MQTT discovery,
Energy dashboard setup, power limits, broker backups and the read-only Modbus
alternative. It includes copy-ready Modbus YAML and troubleshooting.

For existing Domoticz users, its MQTT formats and command topic are unchanged.
Home Assistant can run alongside it on the same broker. Configure the broker
in the existing MQTT page; the separate Home Assistant page enables discovery.

## Troubleshooting and other features

- **No fresh readings:** check daylight, time/location, signal, inverter power
  and the last poll time. The default poll interval is five minutes.
- **Night Mode:** power shows zero overnight; energy is kept. Invalid time or
  location makes polling continue all day rather than stop unexpectedly.
- **Output limit:** the control is 20–500 W **per connected input**, not a
  whole-inverter limit or a percentage. A 100 W request on two inputs is about
  200 W total. The 500 W setting requests normal maximum output.
- **Diagnostics:** download the current report and any crash dump before
  updating. Keep the matching firmware `.elf` for crash analysis. The optional
  flight recorder writes once a minute while enabled; leave it off if you do
  not need it. Live trace and hourly statistics stay in RAM.
- **Grid protection:** these settings change inverter protection limits, not
  firmware. Use only utility-approved values. YC600 writes are disabled.
- **Remote access:** use a trusted network or VPN. Do not expose the ECU directly
  to the Internet. Web passwords do not secure Modbus or the MQTT broker.

## Developer and reference documents

- [AGENTS.md](AGENTS.md): repository guidance for coding agents.
- [DEVELOPMENT.md](DEVELOPMENT.md): source builds, Windows helpers, architecture
  and release packaging. Dependency versions and CI commands live in
  [.github/workflows/build.yml](.github/workflows/build.yml).
- [BUILD-VERIFIED.md](BUILD-VERIFIED.md): completed checks and hardware limitations.
- [SUNSPEC.md](SUNSPEC.md): Modbus register map.
- [LIMITATIONS.md](LIMITATIONS.md), [DEFERRED-WORK.md](DEFERRED-WORK.md): known gaps.
- [PORTING-NOTES.md](PORTING-NOTES.md), [SECURITY-AUDIT.md](SECURITY-AUDIT.md): radio
  implementation and protocol analysis.

## Credits and license

This independent community project started from
[`patience4711/ESP32-read-APS-inverters`](https://github.com/patience4711/ESP32-read-APS-inverters)
at commit `7b0ff63`. It is not affiliated with APsystems or Espressif.
See [ACKNOWLEDGEMENTS.md](ACKNOWLEDGEMENTS.md), [UPSTREAM.md](UPSTREAM.md) and
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) for provenance and dependencies.
The application uses the inherited MIT [LICENSE](LICENSE).
