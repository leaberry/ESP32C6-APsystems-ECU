"""Compile actual validation, antenna startup and NTP logic with hardware stubs."""
from pathlib import Path
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
antenna = (root / 'ANTENNA.ino').read_text()
begin = antenna[antenna.index('void antennaBegin()'):antenna.index('void antennaPage')]
harness = r'''
#include <cassert>
#include <cstring>
#include <string>
#include <vector>
#include <iostream>
#include <ctime>
#include "DEVICE_SETTINGS.h"
#define F(x) x
#define HIGH 1
#define LOW 0
#define OUTPUT 1
#define portENTER_CRITICAL(x) ((void)(x))
#define portEXIT_CRITICAL(x) ((void)(x))
#define portMUX_INITIALIZER_UNLOCKED 0
using portMUX_TYPE = int;
struct String : std::string {
  using std::string::string;
  void toCharArray(char *out, size_t cap) const { snprintf(out, cap, "%s", c_str()); }
};
size_t strlcpy(char *out, const char *in, size_t cap) {
  snprintf(out, cap, "%s", in); return strlen(in);
}
struct { void println(const char *) {} } Serial;
AntennaSettings stored;
bool loadOk = true;
bool antennaLoad(AntennaSettings &out) { out = stored; return loadOk; }
std::vector<int> writes, modes;
void pinMode(int p, int) { modes.push_back(p); }
void digitalWrite(int p, int v) { writes.push_back(p * 2 + v); }
void delay(int) {}
uint32_t tick = 0;
uint32_t millis() { return tick; }
bool timeRetrieved = false;
int datum = 0, syncs = 0, solar = 0;
bool ecuSetLocalTimeFromUtc(time_t) { ++syncs; return true; }
time_t ecuNow() { return 1700000000; }
int ecuDay(time_t) { return 14; }
void sun_setrise() { ++solar; }
void Update_Log(int, const char *) {}
struct FakeNtp {
  bool response = true;
  unsigned long epoch = 1700000000;
  int requests = 0, starts = 0, ends = 0;
  std::string server;
  void setPoolServerName(const char *s) { server = s; }
  void begin() { ++starts; }
  void end() { ++ends; }
  bool forceUpdate() { ++requests; return response; }
  unsigned long getEpochTime() { return epoch; }
} timeClient;
'''
harness += begin + (root / 'TIJD_GET.ino').read_text()
harness += r'''
int main() {
  for (auto h : {"pool.ntp.org", "ntp", "ntp.lan.", "192.168.22.3", "time-1.example"})
    assert(validNtpServer(h));
  for (auto h : {"", ".", "a..b", "-ntp", "ntp-", "a-.b", "a.-b", "256.0.0.1",
                 "1.2.3", "1.2.3.4.5", "http://ntp", "ntp:123", "a b", "::1", "x/y"})
    assert(!validNtpServer(h));
  assert(!validNtpServer((std::string(64,'a')+".org").c_str()));
  assert(!validNtpServer(std::string(254,'a').c_str()));
  antennaBegin(); assert(writes.empty() && modes.empty());
  stored.mode = 2; antennaBegin();
  assert((writes == std::vector<int>{6,29})); // enable GPIO3 LOW; GPIO14 HIGH
  writes.clear(); modes.clear(); stored.mode = 1; antennaBegin();
  assert((writes == std::vector<int>{6,28}));
  stored.board = 1; stored.enablePin = -1; stored.externalHigh = false;
  writes.clear(); modes.clear(); antennaBegin();
  assert((writes == std::vector<int>{29}) && modes.size() == 1);
  stored.mode = 2; writes.clear(); antennaBegin(); assert(writes[0] == 28);
  loadOk = false; writes.clear(); modes.clear(); antennaBegin();
  assert(writes.empty() && modes.empty()); loadOk = true;
  for (int pin : {-1,0,2,4,8,9,12,13,15,16,17,24,30,99})
    assert(!antennaPinAllowed(pin,0,2));
  for (int pin : {1,3,5,6,7,10,11,14,18,19,20,21,22,23})
    assert(antennaPinAllowed(pin,0,2));
  assert(!antennaPinAllowed(14,14,2));
  stored.selectPin = 3; stored.enablePin = 3;
  assert(!validAntennaSettings(stored,0,2));
  stored.board = 0; antennaPreset(stored);
  assert(stored.selectPin == 14 && stored.enablePin == 3 && stored.externalHigh && !stored.enableHigh);
  assert(validAntennaSettings(stored,0,2));
  stored.mode = 3; assert(!validAntennaSettings(stored,0,2));
  ntpSetServer("192.168.22.3"); timeClient.response = false; getTijd();
  assert(!timeRetrieved && syncs == 0 && ntpNeedsSync());
  assert(timeClient.server == "192.168.22.3" && timeClient.starts == timeClient.ends);
  getTijd(); assert(timeClient.requests == 1); // No tight failure retry loop.
  tick += 60000; timeClient.response = true; getTijd();
  assert(timeRetrieved && syncs == 1 && solar == 1 && !ntpNeedsSync());
  tick += 60000; timeClient.response = false; getTijd();
  assert(timeRetrieved && syncs == 1 && ntpNeedsSync()); // Preserve UTC base.
  ntpSetServer("new.ntp.lan"); timeClient.response = true; getTijd();
  assert(syncs == 2 && timeClient.server == "new.ntp.lan"); // Save bypasses retry delay.
  tick += 60000; timeClient.epoch = 1; getTijd();
  assert(syncs == 2 && ntpNeedsSync()); // Reject pre-2020/invalid responses.
  std::cout << "PASS NTP validation, synchronization and failure recovery; antenna presets, pin guards and startup levels\n";
}
'''
with tempfile.TemporaryDirectory() as directory:
    cpp, binary = Path(directory) / 'test.cpp', Path(directory) / 'test'
    cpp.write_text(harness)
    subprocess.run([os.environ.get('CXX', 'g++'), '-std=c++11', '-Wall', '-Wextra',
                    '-I', str(root), str(cpp), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
