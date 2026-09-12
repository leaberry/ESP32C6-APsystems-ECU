# Repository guidance

## Project and scope

This repository is an Arduino ESP32-C6 application for APsystems microinverters.
Read README.md for user workflows, DEVELOPMENT.md for builds, and
BUILD-VERIFIED.md and LIMITATIONS.md for the boundary between host tests and
hardware evidence. HomeAssistant.md is the single user guide for both MQTT
and Modbus integration; SUNSPEC.md owns the register map.

Keep changes focused. Preserve existing Domoticz MQTT payloads, topics and
command behavior unless the user explicitly requests a compatibility change.
Use a separate client and namespace for Home Assistant. Do not add mDNS.
Do not add automatic flash writes for telemetry, counters or discovery metadata.
Home Assistant counter recovery uses retained broker messages and RAM.

## Identity and storage

- Preserve ECU_ID, radio IEEE identity, learned peers and inverter slot order.
  An ECU_ID is an installation identity, not a password or a Wi-Fi MAC address.
- Settings backup and production-history backup are separate formats. Do not
  silently combine them or imply that either contains broker checkpoints.
- Current-day totals and hourly statistics have different durability. Describe
  exactly what a manual save, restart and restore preserve or reset.
- Treat merged images and partition-map changes as potentially destructive.
  Do not flash, erase or change a connected device without authorization.
  Application-only updates must match the installed partition map. For the
  supported 8 MB layout, USB recovery must account for both OTA slots.
- Keep generated binaries, credentials, settings backups and device logs out of
  commits. Do not put personal names, machine paths or local network details in
  agent guidance. Use clearly identified example values in user instructions.

## Build and tests

The Arduino sketch folder must be named ESP32C6-APsystems-ECU. Dependency
versions and installation commands are in .github/workflows/build.yml; avoid
creating a second, conflicting version list. The checked-in partitions.csv is
8 MB. Stage the 4 MB build separately or restore the default after a build.

For firmware changes, build both named layouts with the pinned ESP32 core.
For docs or host-test-only edits, run the affected checks; a firmware rebuild
is unnecessary unless the compiled firmware changed.

Host tests use Python 3, g++ and (where needed) ArduinoJson headers. Set
ARDUINOJSON_INCLUDE to the library's src directory when it is not installed at
~/Arduino/libraries/ArduinoJson/src. JavaScript UI tests use Node.js. Use the
commands in CI and inspect failures before changing test expectations.

C++ test harnesses must include the headers for the types and functions they
use. Do not depend on incidental includes from a particular standard library
(e.g. include cstdint for uint32_t). Read and write source as UTF-8. Avoid
unrelated line-ending changes; this repository contains both LF and CRLF files.

Use actual production functions in host tests where practical. Validate failure
paths that can lose data or report a false success. Host fakes do not establish
radio behavior, MQTT broker disk durability or physical power-loss recovery.
Record untested hardware behavior honestly in BUILD-VERIFIED.md.

## Documentation and delivery

Write user steps in plain language. Include exact menu labels, filenames,
commands, starting directory and placeholders to replace. Distinguish first
installation from same-layout upgrades. Explain which backup restores which
data, and never describe -SkipErase plus a merged image as preserving settings.

Check commands against the scripts, partition files and release packaging.
Check local Markdown links and keep Home Assistant instructions consolidated.
Move developer detail to DEVELOPMENT.md instead of interrupting user procedures.

Use a codex/ branch by default. Respect the user's publishing boundary: when
asked for a local commit, do not push, open a PR, merge or rerun remote jobs.
Inspect the final diff, tests and working tree before reporting completion.
