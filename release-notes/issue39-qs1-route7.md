# Issue 39: QS1 route-reply diagnostic

Firmware: `ESP32C6-ECU_v1_4_20-qs1-route7`

This diagnostic adds replies to the QS1 route requests found in the reporter's
external captures. The older controller handles these messages inside its radio
firmware. This ESP32 implementation must answer them itself.

The QS1 handshake listens for requests relayed through other networks, then
restores normal reception before verifying pairing. A route request alone does
not save a pairing. DS3 and QT2 retain their existing pairing and polling paths.

Changing the ECU ID through Settings now applies the ID and its PAN together
after a restart. Existing IDs are kept, including an explicitly selected default
ID. Keep your current ECU ID for this test.

This is an active ECU build with Wi-Fi, replacing the USB-only listener. It runs
one focused QS1 pairing attempt and collects the result in the existing
**Download QS1 test bundle** file. Successful operation on the reporter's
hardware is still unverified.

## Downloads and installation

- `qs1-route7-4mb-noota.zip`: 4 MB boards with the matching factory layout.
- `qs1-route7-8mb-ota.zip`: 8 MB boards with the matching dual-OTA layout.

Each ZIP includes the application, a partition table for comparison, source and
file checksums, and `TEST-INSTRUCTIONS.txt`. These are application-only upgrades.
Use the [settings-preserving update instructions](https://github.com/leaberry/ESP32C6-APsystems-ECU/blob/issue39-qs1-route7/README.md#upgrade-without-losing-settings-or-data).
For USB, compare the installed partition table and use the linked procedure for
your layout; the 8 MB layout requires writing both application slots. Do not erase
the board or replace its partition table for this test.

## Verification

Both named layouts compile with ESP32 Arduino core 3.3.8. All 34 Python and nine
JavaScript host suites pass, including DS3/QT2 regression tests. Replay recognizes
all six captured QS1 route requests; generated route replies match all 15 old
controller transmissions byte for byte before the FCS. Pairing failures,
identity-save failures and rejected/expired requests are covered by host tests.

Host tests do not establish radio reception or successful pairing on a real QS1.
The diagnostic bundle samples traffic and counts omissions. It does not replace
a complete external sniffer capture.
