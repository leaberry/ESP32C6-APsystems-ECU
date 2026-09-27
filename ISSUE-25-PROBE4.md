# DS3-H assignment and encrypted communication investigation

Version: `ESP32C6-ECU_v1_4_16-probe4`. This supersedes probe3.

## What one run should answer

The target previously answered plaintext firmware queries on FFFF but never
verified on the ECU operating network. This build keeps the plaintext power
test and follows it with instrumented assignment/commit candidates. It stops
network-changing commands as soon as serial-matched operating-network discovery
succeeds, then tests plaintext and encrypted communication at the new address.

A successful firmware query is a communication control, not evidence of pairing
or proof that encryption is unnecessary. The user's production plaintext DS3s
on 5.456 also show why version alone cannot decide encryption mode.

## Reporter instructions

1. Download **ESP32C6-APsystems-ECU-8mb-ota** from the successful GitHub build.
   Unzip it and install **ESP32C6_ECU-8MB-OTA.bin** through **Firmware update**
   on the existing 8 MB OTA installation. Do not use the `.merged.bin` image.
   Check **System information** for the version above.
2. Keep the other ECU off and test in daylight. Wait for a normal polling round.
3. Enable **persistent flight recorder** under **Polling and access**, then save.
4. Open the saved, unpaired DS3-H. Click **Run pairing investigation** once.
   Do not use **Pair inverter** first. This run may change the target inverter's
   network. It does not save a local pairing or change grid/power settings.
5. Allow ten minutes, keeping the ECU powered on. Normal polling pauses during
   testing. When it says **Tests finished** or **Tests incomplete**, use
   **Download encrypted test log**. Save it before restarting or running again.
   Do not run a second test until these results have been reviewed.
6. Let the working inverters complete another polling round. Download the
   **report**, **poll log**, and **flight recorder** from **Diagnostic snapshot**.
   ZIP all four files and attach them to issue #25. Include the test time/time
   zone, completion message, and whether normal polling resumed. Even if the
   inverter changes networks, the saved pairing status remains unchanged.
7. Turn the recorder off and save after collecting the files. **Clear recorded
   logs** can free flash after the downloads are saved.

## Evidence and remaining uncertainty

The user-supplied ECU-R-PRO 2.1.27 ARM host executable calls plaintext modem
builders for directed PAN set (0F), prepare (11), and commit (22). Prepare uses
the operating PAN; commit uses FFFF, three transmissions one second apart,
then a ten-second wait. The host does not AES-wrap these commands. Its radio
module firmware is absent, so these modem opcodes cannot be equated with raw
APS clusters. The C6's established sequence uses 020D, 020C, 010F, and 0101.

This run therefore labels the following as hypotheses, not confirmed mappings:

- Existing 010F prepare followed by three 0101 commits, now with observations
  between prepare and commit and after settling. Fresh FFFF discovery avoids
  an unnecessary initial reset-to-FFFF command.
- The existing directed 020D payload with its requested PAN set to the configured
  operating PAN, sent on FFFF. This tests the separate directed-set path suggested
  by the original host, first without commit, then with the same commit candidate.

No invented opcodes, channel sweep, AES-disable, identity change, grid settings,
or power-control commands are used. Trials are sequential and may influence
later state; a later success needs a focused reproduction before a production fix.

## Sequence and decisions

| Phases | Purpose |
| --- | --- |
| 0-2 | Up to three two-query windows for a fresh FFFF serial/address |
| 3 | Working plaintext inverter firmware control, when available |
| 4-8 | Target DC firmware controls around direct/broadcast plaintext BB power queries |
| 9 | Operating-network discovery before any assignment |
| 10-13 | Prepare candidate, FFFF and operating discovery, FFFF DC control |
| 14-19 | First commit plus two repeats, ten-second settle, both networks, DC/BB controls |
| 20-24 | Directed operating-PAN candidate, both networks, DC/BB controls |
| 25-30 | Commit candidate after directed PAN, settle, both networks, DC/BB controls |
| 31/32/34/35/36 | If operating discovery succeeded: plaintext DC/BB, native encrypted DC/BB, A1 encrypted BB |
| 33 | Final known working inverter firmware control, when the suite has no error |

Two application/discovery requests per phase; network-changing candidates are
sent once, except the explicitly logged commit repetitions. Fresh discovery on
the operating network supplies the address for subsequent queries, including
when the address changes. Conflicting serial-matched sources abort. A missing
FFFF target after the prepare/commit trial prevents the directed-PAN trial.
A missing target after directed PAN prevents further commit guesses. A radio
setup/transmission error in a pairing command aborts, but NO_ACK on read-only
queries keeps the receive window open, as the reporter already received replies
after this error. The ECU radio is restored on every exit path.

There are at most 37 phase slots. Unused phases remain explicitly not-run. A full
no-migration run takes about 229 seconds plus radio processing; allow ten minutes.
Raw capture retains first two/latest two related frames per phase and first/latest
assembled ASDU, with omission counts. Replies never enter production telemetry
or peer storage. Logs stream from RAM and cannot be replaced while downloading.
`finished=1` means the planned experiments completed, not that pairing succeeded.
`operating_source` means serial-matched discovery there, not proof of usable AES.

## What the results decide

- Operating response immediately after prepare: investigate deferred commit assumptions.
- Response only after commit: focus on commit timing/translation.
- Response after directed 020D: focus on directed PAN-set compatibility.
- Operating discovery plus valid encrypted telemetry: candidate complete communication path.
- Operating discovery but no usable telemetry: migration and AES are separate remaining problems.
- No migration while FFFF DC still works: narrow the remaining problem to assignment/commit
  semantics or prerequisites, without blaming radio silence or a guessed AES marker.

The test does not automatically persist experimental pairing. Actual hardware
behavior remains unverified until the reporter returns the capture.
