# CC2530/CC2531 firmware security audit

## Conclusion

The custom binary was compiled with TI Z-Stack security *capability*, but the APsystems exchange used by this project is not protected by a Zigbee application link key and the captured ZNP frames are marked unsecured. No APsystems-specific install code or compile-time Zigbee network key was found. This does **not** mean every inverter payload is plaintext: newer inverter families can use a separate APsystems L1 AES envelope above Zigbee. That proprietary layer is implemented in `APS_CRYPTO.ino`.

Accordingly, the C6 port explicitly disables Zigbee network security and does not set the APS security transmit option. This matches the observed legacy behavior; enabling standard Zigbee 3.0 encryption would change the on-air frames and prevent these inverters from responding.

## Evidence

1. The firmware derives from [Koenkk/Z-Stack-firmware](https://github.com/Koenkk/Z-Stack-firmware). Its coordinator patch defines `SECURE 1`, `TC_LINKKEY_JOIN`, and key-management MT commands. Those are compile-time inclusion switches for TI's security-capable stack, not proof that a particular APS frame uses security.
2. Successful inverter replies decoded in the development issue show `SecurityUse: 0x00`: [DS3 poll capture](https://github.com/Koenkk/zigbee2mqtt/issues/4221#issuecomment-1063120874) and [second DS3 response](https://github.com/Koenkk/zigbee2mqtt/issues/4221#issuecomment-1079219851).
3. The firmware author stated that CC2530 and CC2531 used different compile options, then identified the relevant failure: CC2530's UART receive buffer was 128 bytes while CC2531's was 265 bytes ([explanation](https://github.com/Koenkk/zigbee2mqtt/issues/4221#issuecomment-1079735867)). The author posted a new CC2530 test image with the enlarged buffer specifically for DS3 ([firmware comment](https://github.com/Koenkk/zigbee2mqtt/issues/4221#issuecomment-1079758214)).
4. The author also clarified that the firmware was receiving and processing the radio data and that the remaining issue was serial handling ([comment](https://github.com/Koenkk/zigbee2mqtt/issues/4221#issuecomment-1079309085)).
5. Static inspection of all five supplied HEX images found neither ASCII `ZigBeeAlliance09` nor its reversed byte sequence. This cannot prove absence of every possible encoded key, but it rules out an obvious embedded standard key string.
6. The ESP host sends no ZNP command that provisions a network key, APS link key, install code, or security NV item. It configures logical type, channel, PAN IDs, endpoint, starts the coordinator, and sends raw AF payloads.

## Why `SECURE 1` is not contradictory

TI's stack can contain trust-center and cryptographic code while individual networks or APS requests run unsecured. The ZNP `AF_INCOMING_MSG` security field describes the received application data's security status. Here it is zero in successful captures, and the application's `AF_DATA_REQUEST` options do not request APS encryption.

## Proprietary APsystems L1 AES

[`patience4711/ESP32-read-APS-inverters` issue 55](https://github.com/patience4711/ESP32-read-APS-inverters/issues/55)
and the linked [`bolkedebruin/openaps`](https://github.com/bolkedebruin/openaps)
reverse engineering identify a second,
independent security mechanism used by inverter IDs whose second character is
`2`. It uses AES-128-ECB with a fresh six-byte nonce per frame. The key is:

`nonce[6] || inverter UID in six-byte BCD || 18 28 45 90`

The encrypted plaintext is `one-byte length || complete FB FB...FE FE frame ||
zero padding to a 16-byte boundary`. The inverter/sender UID remains clear so
the receiver can derive the key. The modem's `FC FC` receive header and
`AA AA AA AA ... A0/A1` transmit header are UART envelopes and are not emitted
through the ESP32-C6 raw APS API.

The implementation accepts plaintext and encrypted replies per inverter,
supports the modem-gate variant defensively, uses the serial-number rule for
outbound selection, and runs a fixed AES known-answer self-test at boot.

The algorithm and framing come from static reverse engineering; the upstream
authors explicitly note that they did not have a real encrypted-inverter golden
capture. Physical testing with a `?2??????????` inverter therefore remains
required before the encrypted transmit path can be called field-validated.

## Residual uncertainty

The posted HEX files are binaries, not a reproducible source tree for the author's exact test build. Static inspection therefore cannot prove every compiler macro. It does establish the observable behavior relevant to this port: successful APsystems frames were unsecured, no key is provisioned by the host, and the documented DS3 customization was buffer sizing.
