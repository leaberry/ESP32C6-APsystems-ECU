# Third-party notices

This file summarizes directly inherited code, referenced protocol
implementations and build dependencies. It supplements, and does not replace,
the license text shipped by each dependency.

## patience4711 APsystems projects

This repository derives from
[`patience4711/ESP32-read-APS-inverters`](https://github.com/patience4711/ESP32-read-APS-inverters)
and its ESP8266/Raspberry Pi predecessors. The inherited application is MIT
licensed. Its notice is retained in the repository's main [LICENSE](LICENSE).

## OpenAPS

Protocol knowledge and implementation guidance from
[`bolkedebruin/openaps`](https://github.com/bolkedebruin/openaps) informed
inverter information/version decoding, the APsystems application transport and
grid-protection profiles/codecs.

OpenAPS is distributed under the MIT License:

```text
MIT License

Copyright (c) 2026 bolkedebruin

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

## Vendored compatibility header

`Async_TCP.h` carries its own header notice: copyright 2016 Hristo Gochkov and
GNU Lesser General Public License version 2.1 or later. The active ESP32-C6
build uses the separately installed ESP32Async `AsyncTCP` library; the retained
header remains part of the historical source tree and its notice must remain.

## Build dependencies

The build installs these external projects; consult the linked releases for the
exact license applying to the selected version:

- [Arduino-ESP32](https://github.com/espressif/arduino-esp32) — Espressif;
- [ArduinoJson](https://github.com/bblanchon/ArduinoJson) — Benoit Blanchon;
- [ESP Async WebServer](https://github.com/ESP32Async/ESPAsyncWebServer) and
  [AsyncTCP](https://github.com/ESP32Async/AsyncTCP) — ESP32Async contributors;
- [PubSubClient](https://github.com/knolleary/pubsubclient) — Nick O'Leary;
- [NTPClient](https://github.com/arduino-libraries/NTPClient) — Fabrice Weinberg
  and Arduino Libraries contributors;
- [Time](https://github.com/PaulStoffregen/Time) — Michael Margolis and Paul
  Stoffregen;
- [PSACrypto](https://github.com/machinefi/psa-crypto) — IoTeX; and
- [sunMoon](https://github.com/sfrwmaker/sunMoon) — sfrwmaker.

Generated firmware statically links applicable dependencies, so distributors
of binaries should retain the corresponding license materials with the release.
See [ACKNOWLEDGEMENTS.md](ACKNOWLEDGEMENTS.md) for non-license project credit.
