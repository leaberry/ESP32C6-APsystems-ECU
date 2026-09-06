# Issue #4: pairing command payload corruption

Report: https://github.com/leaberry/ESP32C6-APsystems-ECU/issues/4

The native `sendZB()` adapter consumed only one byte of the extended ZNP
`AF_DATA_REQUEST_EXT` length. That field is two bytes, little endian;
ordinary `AF_DATA_REQUEST` uses one byte. See Texas Instruments' CC2530 ZNP
Interface Specification, section 4.4.3 (pages 48–49):
https://e2e.ti.com/cfs-file/__key/communityserver-discussions-components-files/667/CC2530ZNP-Interface-Specification.pdf

For the report's command 1, the intended six-byte payload is
`70 40 00 00 77 19`. The old parser instead submitted
`00 70 40 00 00 77`: the high length byte became payload byte zero,
and the last serial byte was omitted. The relayed MAC frames in the report
contain exactly that malformed payload. Commands 0, 2 and 3 likewise lose
the final ECU identifier byte. Successful MAC transmission therefore did
not mean the inverter received a valid pairing request.

The fix consumes both length bytes and rejects extended requests whose
declared payload is incomplete, has extra bytes, or exceeds the parser buffer.
Normal one-byte-length requests and ambiguous-peer rejection are unchanged.
The report has multiple possible radio peers; choosing one by signal strength
would risk saving another inverter's address and would not fix this defect.

## Regression verification

Run `python3 tools/test_znp_pairing.py` with `g++` installed (or set `CXX`).
The test compiles the actual parser and captures its radio submissions.
All four commands copied from the issue fail their exact-payload checks on
the original parser. The corrected parser passes all 13 checks, including
ordinary requests, malformed lengths and a nonzero high length byte.
The firmware CI workflow runs the suite for each build layout.

## Hardware retest

The packet corruption is reproduced and fixed in software. Successful pairing
with the reporter's DS3 has not yet been verified on hardware.

1. Install the issue-4 test application image through the existing 8 MB ECU's
   OTA page. A first installation over USB uses the merged image instead.
2. Keep the configured inverter serial `704000007719` and retry pairing while
   the inverter is powered. No settings erase or peer-inference override is
   needed for this fix.
3. Confirm pairing obtains an ID and a subsequent poll receives telemetry.
4. If pairing still fails, download a new diagnostic report after the attempt.
   Relayed command `0x020C` should contain the full payload `704000007719`;
   command `0x0101` should contain the full six-byte ECU identifier. Those
   observations distinguish this corrected parser from other radio or reply
   handling problems.

Local test images retain the base firmware version string; use their build
information and SHA-256 hashes to identify them. They are development builds,
not a published release.
