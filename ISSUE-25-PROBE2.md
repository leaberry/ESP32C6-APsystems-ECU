# Encrypted inverter read-only test build

Version: `ESP32C6-ECU_v1_4_16-probe2`.

## Reporter instructions

1. Install the application image matching the existing flash layout. On an
   existing 8 MB OTA installation, use `ESP32C6_ECU-8MB-OTA-probe2.bin` through
   **Firmware update**. Do not use a merged image for an upgrade. The 4 MB
   application requires an application-only USB update; see
   [README.md](README.md#upgrade-without-losing-settings-or-data).
2. Check **System information** for the version above. Keep the other ECU off.
   Run in daylight and wait for the working inverters to finish one poll round.
3. Under **Polling and access**, enable **persistent flight recorder** and save.
4. Open the saved DS3-H inverter and click **Run encrypted tests** once.
   Use this new button, not **Pair inverter**. Keep the ECU powered and wait
   up to five minutes. Normal polling pauses while the tests run.
5. When the page says **Tests finished** or **Tests incomplete**, click
   **Download encrypted test log** on that page. Download before restarting
   or running another test: the packet capture is in RAM.
6. From **Diagnostic snapshot**, also download the **report**, **poll log**, and
   **flight recorder**. Let the other inverters poll again and note whether
   they resume normally. This suite intentionally leaves pairing unchanged;
   the DS3-H remaining unpaired does not mean the test failed to run.
7. Put the files in a ZIP and attach it to issue #25. Include the test time,
   time zone, completion message, and whether the other inverters resumed.
   Turn the flight recorder off and save after collecting the files.

The test-log link is also available at `/diagnostics/encrypted-test` on the ECU.
Log downloads need the administrator login. Wait until a download finishes
before starting another test. After saving the files, **Clear recorded logs**
on **Diagnostic snapshot** frees the existing recorder flash files. The new
packet capture uses bounded RAM and creates no additional flash files.

## Questions answered by the suite

The last capture established that the inverter heard the proposed assignment
but did not verify on the operating network. More retries of that assignment
would not distinguish the remaining encryption and delivery hypotheses.

This suite sends only discovery queries and the existing read-only DC firmware
version request. It does not send network assignment, commit, power-control,
AES-disable, identity-change or configuration commands. No discovered route,
firmware response or encryption flag is saved into inverter settings.

1. Two discovery queries on FFFF must obtain a fresh serial-matched direct
   response. A cached address is not enough. Conflicting discovery sources
   stop the suite before DC requests.
2. A saved unencrypted inverter is queried before and after the target matrix,
   when available. This is a control for the same read-only DC request and
   direct delivery path. Missing control replies make negative results harder
   to interpret; they do not themselves establish a radio failure.
3. The freshly discovered DS3-H short address is tested on FFFF and on the
   configured operating PAN. The latter address is a hypothesis: the inverter
   could use a different short address if it has moved networks.
4. Each network gets two requests for each of six envelope layouts:

   | Mode | Request layout | Key UID |
   | --- | --- | --- |
   | 0 | ECU UID + plaintext DC | None |
   | 1 | ECU UID + nonce + ciphertext | Target serial |
   | 2 | ECU UID + A1 + nonce + ciphertext | Target serial |
   | 3 | A1 + nonce + ciphertext | Target serial |
   | 4 | ECU UID + A0 + nonce + ciphertext | All zeros |
   | 5 | A0 + nonce + ciphertext | All zeros |

   These requests use radio unicast with MAC ACK requested. A0 is tested as
   an application-envelope hypothesis while retaining direct radio delivery.
   All encrypted variants use the existing AES primitive and length-prefixed
   DC body; fresh nonces are generated for each request.
5. Modes 1 and 2 are repeated using broadcast radio delivery on both networks,
   with the target-specific key. This isolates delivery from the marker choice.
6. Discovery is repeated afterward to check whether the target still responds.
   The ECU restores its operating PAN and resumes normal scheduling.

The suite has 20 bounded phases and at most 40 requests, normally around
2 minutes 20 seconds plus transmit time. A local setup error can stop it early.
Missing unicast ACKs are logged and do not skip remaining layouts.

## Reading the evidence

A successful radio transmission/ACK is not proof of an accepted command.
An actual version reply that decrypts and matches an existing DC response
format is much stronger evidence for that envelope. Logs preserve raw frames
and assembled replies even when they cannot be decoded, including packets
without the visible serial number. No result is automatically marked paired.

If discovery continues to work but every DC format is silent, the device may
require a prior setup exchange or a different application command. Silence
alone cannot prove which AES layout is correct. Late replies can cross phase
boundaries; timestamps and a focused repeat of a promising variant would be
needed before treating one as a confirmed solution.

Raw capture retains the first three and latest three related frames per phase;
assembled data retains the first and latest response. Omitted counts are
explicit. Samples are not sorted, so use timestamps. Both direct-source
packets and serial-bearing frames are retained; relay echoes are not treated
as application replies. Only the active source/PAN enters diagnostic APS
reassembly. Fragment ACKs and requested unfragmented APS ACKs are sent, but
responses never enter telemetry or learned-peer persistence.

The download streams one line at a time from a completed capture. A reader
lease prevents a new test from clearing it during download. This avoids
constructing the full capture as one large String. The existing pairing-log
export is also reduced to three recent audits to reduce memory pressure;
its persistent on-flash record format is unchanged.

## Verification boundary

Host tests run the actual query builder, key/envelope assembly, orchestration,
radio frame builder, decoder, capture bounds, restoration and streamed export.
They cover no reply, no ACK, conflicting identities, wrong PAN/source, stale
frames, invalid input, disabled recorder and downloads during a running test.
The AES primitive is replaced by a reversible test double in those host tests;
this checks framing/key selection, not cryptographic interoperability. The
production AES implementation and its startup known-answer self-test are
unchanged. Hardware replies and interoperability remain unverified.
