"""Host regression tests for the actual ECU clock, using Python 3, tzdata and g++.

Run: python3 tools/test_timezones.py
Hardware timer/FreeRTOS/TimeLib are stubbed; clock and timezone code are compiled
from the sketch. IANA zoneinfo supplies independent expected local times.
"""
from pathlib import Path
import calendar
import datetime as dt
import os
import re
import subprocess
import tempfile
from zoneinfo import ZoneInfo

ROOT = Path(__file__).resolve().parents[1]
zones = (ROOT / "TIMEZONES.ino").read_text(encoding="utf-8")
clock = (ROOT / "ECU_TIME.ino").read_text(encoding="utf-8")
energy = (ROOT / "ENERGY_HISTORY.ino").read_text(encoding="utf-8")


def extract(text, signature):
    start = text.index(signature)
    end = text.index("{", start) + 1
    depth = 1
    while depth:
        depth += (text[end] == "{") - (text[end] == "}")
        end += 1
    return text[start:end]


cases = []
transitions = []
for name in re.findall(r'\{"(Europe/[^" ]+)"', zones):
    zone = ZoneInfo(name)
    for year in range(2026, 2036):
        instants = [dt.datetime(year, m, 15, 12, tzinfo=dt.timezone.utc)
                    for m in range(1, 13)]
        for month in (3, 10):
            last = calendar.monthrange(year, month)[1]
            day = last - (dt.date(year, month, last).weekday() + 1) % 7
            boundary = dt.datetime(year, month, day, 1, tzinfo=dt.timezone.utc)
            instants.extend(boundary + dt.timedelta(seconds=s) for s in (-1, 0, 1))
            transitions.append((name, int(boundary.timestamp())))
        for instant in instants:
            local = instant.astimezone(zone)
            cases.append('  {"%s", %dLL, %dLL, %d},' % (
                name, int(instant.timestamp()),
                int(local.replace(tzinfo=dt.timezone.utc).timestamp()),
                int(local.utcoffset().total_seconds() / 60)))

# The UTC clock is shared by all installations, including production in Denver.
for name in re.findall(r'\{"([^" ]+)", "[^"]+", "[^"]+"\}', zones):
    if name.startswith("Europe/"):
        continue
    for month in (1, 7):
        instant = dt.datetime(2026, month, 15, 12, tzinfo=dt.timezone.utc)
        local = instant.astimezone(ZoneInfo(name))
        cases.append('  {"%s", %dLL, %dLL, %d},' % (
            name, int(instant.timestamp()),
            int(local.replace(tzinfo=dt.timezone.utc).timestamp()),
            int(local.utcoffset().total_seconds() / 60)))

harness = r'''
#include <algorithm>
#include <atomic>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
using std::isfinite;
using SemaphoreHandle_t = std::mutex*;
constexpr int portMAX_DELAY = 0;
std::atomic<int64_t> micros{0};
int solarCalls = 0;
time_t compatibilityClock = 0;
int16_t currentUtcOffsetMinutes = 0;
int dst = 0;
char timeZoneId[24] = "UTC", gmtOffset[8] = "0";
SemaphoreHandle_t xSemaphoreCreateMutex() { static std::mutex m; return &m; }
void xSemaphoreTake(SemaphoreHandle_t m, int) { m->lock(); }
void xSemaphoreGive(SemaphoreHandle_t m) { m->unlock(); }
int64_t esp_timer_get_time() { return micros.load(); }
void setTime(time_t t) { compatibilityClock = t; }
struct tmElements_t { int Second, Minute, Hour, Day, Month, Year; };
#define CalendarYrToTm(y) ((y)-1970)
time_t makeTime(const tmElements_t &e) {
  tm t = {}; t.tm_sec=e.Second; t.tm_min=e.Minute; t.tm_hour=e.Hour;
  t.tm_mday=e.Day; t.tm_mon=e.Month-1; t.tm_year=e.Year+70;
  return timegm(&t);
}
template<typename T> T constrain(T v, T lo, T hi) {
  return std::min(std::max(v,lo),hi);
}
void sun_setrise();
'''
harness += clock[:clock.index("static tmElements_t ecuTimeElements")]
harness += zones[:zones.index("String ecuTimeZoneOptionsHtml")]
harness += extract(zones, "bool ecuSetLocalTimeFromUtc(")
harness += r'''
// Exercise the actual energy accumulation/rollover code, stubbing flash only.
constexpr int YC600_MAX_NUMBER_OF_INVERTERS=1;
bool timeRetrieved=true;
uint32_t energyDateKey=0, energyTodayWh[1]={}, energyHourlyWh[1][24]={};
float energyFractionWh[1]={};
int finalized=0;
std::string String(float v) { return std::to_string(v); }
void consoleOut(const std::string&) {}
void energyLoadCheckpoint() {}
void energyResetDailyStats() {}
bool energyFinalizeDay() { ++finalized; return true; }
int ecuYear(time_t t) { tm v={}; gmtime_r(&t,&v); return v.tm_year+1900; }
int ecuMonth(time_t t) { tm v={}; gmtime_r(&t,&v); return v.tm_mon+1; }
int ecuDay(time_t t) { tm v={}; gmtime_r(&t,&v); return v.tm_mday; }
int ecuHour(time_t t) { tm v={}; gmtime_r(&t,&v); return v.tm_hour; }
'''
harness += "void haEnergyCredit(int,float) {}\n"
for signature in ("static uint32_t energyLocalDateKey(", "void energyRecordDelta(",
                  "void energyHistoryLoop("):
    harness += extract(energy, signature)
harness += r'''
// Re-enter the clock just as the real solar helpers do, detecting lock recursion.
void sun_setrise() { assert(ecuNow() != 0); ++solarCalls; }
struct Case { const char *zone; time_t utc, local; int offset; };
const Case cases[] = {
'''
harness += "\n".join(cases) + "\n};\n"
harness += r'''
struct Transition { const char *zone; time_t utc; };
const Transition transitions[] = {
'''
harness += "\n".join('  {"%s", %dLL},' % t for t in transitions) + "\n};\n"
harness += r'''
void select(const char *zone, time_t utc) {
  strcpy(timeZoneId,zone);
  assert(ecuSetLocalTimeFromUtc(utc));
}
int main() {
  ecuTimeBegin();
  select("Europe/London", 0);
  assert(ecuNow() == 0);
  ecuTimeLoop(); assert(solarCalls == 0);
  for (const auto &c : cases) {
    select(c.zone,c.utc);
    assert(ecuNow() == c.local && currentUtcOffsetMinutes == c.offset);
  }
  std::cout << "PASS 540 European samples plus winter/summer samples for all other zones\n";
  for (const auto &t : transitions) {
    select(t.zone,t.utc-1);
    const time_t before=ecuNow();
    const int offset=currentUtcOffsetMinutes;
    const int oldDst=dst;
    energyDateKey=energyLocalDateKey();
    energyTodayWh[0]=0;
    memset(energyHourlyWh,0,sizeof(energyHourlyWh));
    energyRecordDelta(0,10);
    const int previousFinalized=finalized;
    ecuTimeLoop();
    int count=solarCalls;
    micros += 1000000; // No NTP or zone update at the transition.
    const time_t after=ecuNow(); // A web reader can be first across the boundary.
    assert(std::abs(currentUtcOffsetMinutes-offset) == 60 && oldDst != dst);
    assert(after-before == 1+(currentUtcOffsetMinutes-offset)*60);
    assert(compatibilityClock == after);
    ecuTimeLoop(); assert(solarCalls == count+1);
    ecuTimeLoop(); assert(solarCalls == count+1);
    // European transitions do not finalize a local day. Spring skips a bucket;
    // autumn's repeated hour accumulates into the same existing 24-hour bucket.
    tm b={},a={}; gmtime_r(&before,&b); gmtime_r(&after,&a);
    assert(a.tm_mday == b.tm_mday);
    energyHistoryLoop();
    energyRecordDelta(0,20);
    assert(finalized == previousFinalized && energyTodayWh[0] == 30);
    if (oldDst == 1) assert(energyHourlyWh[0][a.tm_hour] == 30);
    else assert(energyHourlyWh[0][b.tm_hour+1] == 0);
    // The next local midnight must still finalize exactly once.
    micros += (24-a.tm_hour)*3600LL*1000000;
    energyHistoryLoop();
    assert(finalized == previousFinalized+1 && energyTodayWh[0] == 0);
    energyHistoryLoop(); assert(finalized == previousFinalized+1);
  }
  std::cout << "PASS 60 autonomous DST transitions, solar/TimeLib updates and energy rollovers\n";

  // Switching zones during the repeated hour must preserve UTC and fractions.
  const time_t repeatedHour=1792891800; // 2026-10-25 01:30 UTC
  select("Europe/London",repeatedHour);
  micros += 750000;
  select("Europe/Berlin",0);
  assert(ecuNow() == repeatedHour+3600);
  micros += 250000;
  assert(ecuNow() == repeatedHour+3601);
  select("Europe/London",0);
  assert(ecuNow() == repeatedHour+1);
  for (int offset : {-720,-420,0,330,840}) {
    snprintf(gmtOffset,sizeof(gmtOffset),"%d",offset);
    select("Custom",repeatedHour);
    micros += 86400LL*1000000;
    assert(ecuNow() == repeatedHour+86400+offset*60);
    assert(dst == 0 && currentUtcOffsetMinutes == offset);
  }
  select("Europe/Berlin",repeatedHour);
  assert(ecuNow() == repeatedHour+3600);
  std::cout << "PASS live zone changes, subsecond continuity and fixed offsets\n";

  // Clock conversion and process-wide TZ changes share the same mutex.
  std::thread reader([&] {
    for (int i=0;i<10000;++i) {
      const time_t now=ecuNow();
      assert(now==repeatedHour || now==repeatedHour+3600);
    }
  });
  for (int i=0;i<1000;++i) select(i%2 ? "Europe/London":"Europe/Berlin",0);
  reader.join();
  std::cout << "PASS concurrent clock reads and timezone changes\n";
}
'''

with tempfile.TemporaryDirectory(prefix="ecu-timezones-") as directory:
    cpp = Path(directory) / "test.cpp"
    binary = Path(directory) / "test"
    cpp.write_text(harness, encoding="utf-8")
    subprocess.run([os.environ.get("CXX", "g++"), "-std=c++11", "-pthread",
                    "-Wall", "-Wextra", "-Werror", str(cpp), "-o", str(binary)],
                   check=True)
    subprocess.run([str(binary)], check=True, timeout=30)
