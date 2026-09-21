# Issue #24 diagnostics

The polling fixes and diagnostic recorder are now integrated. See
[Polling diagnostics](POLL-DIAGNOSTICS.md) for enabling recording, downloading
logs, and freeing their flash space.

The integrated candidate identifies itself as
`ESP32C6-ECU_v1_4_16-poll-fix2`. Follow the standard
[matching-layout upgrade instructions](README.md#upgrade-without-losing-settings-or-data)
using the application image. Preserve the matching ELF for any new crash dump.

YC600 reliability still needs confirmation on the reporter's hardware.
