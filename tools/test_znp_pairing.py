"""Run the real sendZB parser on a host C++ compiler (python3 + g++).

The radio submission is captured, so no ESP32 or radio transmissions are needed.
Run from any directory: python3 tools/test_znp_pairing.py
Optionally pass a transport source path to check an older revision.
"""

from pathlib import Path
import os
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[1]
source = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "ZIGBEE_A_TRANSPORT.ino"
text = source.read_text(encoding="utf-8")


def function(signature):
    start = text.index(signature)
    opening = text.index("{", start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (text[end] == "{") - (text[end] == "}")
        end += 1
    return text[start:end]


harness = r'''
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>
using std::min;
int apsExpectedWhich = 7;
int calls = 0;
uint16_t destination, clusterId;
uint8_t destEp, sourceEp, hopRadius, txOptions;
std::vector<uint8_t> sent;
void diagnosticsAppend(const char *) {}
bool submitRawAps(uint16_t dst, uint8_t dep, uint8_t sep, uint16_t cluster,
                  const uint8_t *payload, uint16_t length,
                  uint8_t radius, uint8_t options) {
  ++calls;
  destination = dst; destEp = dep; sourceEp = sep; clusterId = cluster;
  hopRadius = radius; txOptions = options;
  sent.assign(payload, payload + length);
  return true;
}
'''

for signature in ("static uint8_t hexNibble(", "static uint8_t hexByte(",
                  "static uint16_t hexLe16(", "bool sendZB("):
    harness += "\n" + function(signature) + "\n"

harness += r'''
int failures = 0;
void check(bool ok, const char *name) {
  std::cout << (ok ? "PASS " : "FAIL ") << name << '\n';
  failures += !ok;
}
std::vector<uint8_t> unhex(const std::string &s) {
  std::vector<uint8_t> result;
  for (size_t i = 0; i < s.size(); i += 2)
    result.push_back(hexByte(s.c_str() + i));
  return result;
}
void submit(std::string command) {
  calls = 0; sent.clear(); apsExpectedWhich = 7;
  sendZB(&command[0]);
}
int main() {
  // Exact four commands from GitHub issue #4. Expected bytes are the
  // complete serial/ECU fields, with no length-byte prefix or lost tail.
  const char *commands[] = {
    "24020FFFFFFFFFFFFFFFFF14FFFF140D0200000F1100704000007719FFFF10FFFF80971B01A3D8",
    "24020FFFFFFFFFFFFFFFFF14FFFF140C0201000F0600704000007719",
    "24020FFFFFFFFFFFFFFFFF14FFFF140F0102000F1100704000007719A3D810FFFF80971B01A3D8",
    "24020FFFFFFFFFFFFFFFFF14FFFF14010103000F060080971B01A3D8"
  };
  const char *payloads[] = {
    "704000007719FFFF10FFFF80971B01A3D8", "704000007719",
    "704000007719A3D810FFFF80971B01A3D8", "80971B01A3D8"
  };
  const uint16_t clusters[] = {0x020D, 0x020C, 0x010F, 0x0101};
  for (int i = 0; i < 4; ++i) {
    submit(commands[i]);
    check(calls == 1 && sent == unhex(payloads[i]) &&
          clusterId == clusters[i] && destination == 0xFFFF &&
          destEp == 0x14 && sourceEp == 0x14 && hopRadius == 15 &&
          txOptions == 0 && apsExpectedWhich == -1,
          ("issue #4 pairing command " + std::to_string(i)).c_str());
  }
  submit("240134121414060001000F06704000007719");
  check(calls == 1 && sent == unhex("704000007719") &&
        destination == 0x1234 && clusterId == 6 && apsExpectedWhich == 7,
        "ordinary AF_DATA_REQUEST keeps its one-byte length");

  const std::string header = "24020FFFFFFFFFFFFFFFFF14FFFF140C0201000F";
  submit(header + "0000");
  check(calls == 1 && sent.empty(), "complete empty extended request");
  submit(header + "06007040000077");
  check(calls == 0, "reject truncated payload");
  submit(header + "0500704000007719");
  check(calls == 0, "reject payload longer than declared");
  submit(header + "06");
  check(calls == 0, "reject incomplete length field");
  submit(header + "060070400000771");
  check(calls == 0, "reject odd hex length");
  submit(header + "0601704000007719");
  check(calls == 0, "do not ignore nonzero high length byte");
  submit(header + "0001" + std::string(512, 'A'));
  check(calls == 1 && sent == std::vector<uint8_t>(256, 0xAA),
        "decode full uint16 length without uint8 narrowing");
  submit(header + "2D01" + std::string(602, 'A'));
  check(calls == 0, "reject length exceeding parser buffer");
  return failures ? 1 : 0;
}
'''

with tempfile.TemporaryDirectory(prefix="znp-pairing-") as directory:
    cpp = Path(directory) / "test.cpp"
    binary = Path(directory) / "test"
    cpp.write_text(harness, encoding="utf-8")
    subprocess.run([os.environ.get("CXX", "g++"), "-std=c++11", "-Wall",
                    "-Wextra", "-Werror", str(cpp), "-o", str(binary)], check=True)
    sys.exit(subprocess.run([str(binary)]).returncode)
