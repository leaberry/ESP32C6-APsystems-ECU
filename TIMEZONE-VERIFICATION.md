# Timezone switchover verification (issue #3)

The London, Berlin and Helsinki rules in `TIMEZONES.ino` match current European
seasonal time arrangements. The defect was when the clock applied those rules:
it advanced a saved local epoch until the next time refresh, normally after
03:00. In continuous operation that could delay London's spring adjustment by
two hours, Berlin's by one hour, and Helsinki's autumn adjustment by 23 hours.

`ECU_TIME.ino` now stores UTC against the 64-bit monotonic timer and derives
local time on reads, caching the result for the current second. UTC sync, zone
changes, TZ conversion and the cache use the same mutex. Changing the selected
zone preserves the UTC instant and subsecond base, including during the
repeated autumn hour. The existing local-epoch interface and stored history
format are retained.

Offset changes rebase the TimeLib compatibility clock and queue solar-window
recalculation. The main loop consumes that request before day/night decisions,
outside the clock mutex. This also works if a web request is the first clock
reader across the transition. NTP remains responsible for clock synchronization,
but a synchronized clock does not need another NTP response to apply DST.

The time-settings page labels the effective offset with the saved zone, removes
the unconditional Berlin example, and explains that edits take effect on Save.
London's UTC+01:00 in the September issue comment was already correct.

## Automated coverage

Run `python3 tools/test_timezones.py` on a host with `g++` and IANA tzdata.
The test compiles the actual clock, timezone-selection wrapper, and energy
accumulation/rollover functions; only platform/TimeLib primitives and flash
access are stubbed. It checks:

- 540 monthly and boundary samples for London, Berlin and Helsinki in
  2026–2035 against Python's IANA zoneinfo;
- winter/summer samples for every other named zone, including Denver;
- 60 running-clock DST transitions without another NTP sync, including
  offset/DST status, TimeLib updates and deferred solar recalculation;
- energy totals across skipped/repeated hours and one rollover at the next
  local midnight (the existing 24 buckets combine the repeated hour);
- immediate zone changes during the autumn overlap, retaining fractional
  seconds;
- fixed offsets from UTC-12 to UTC+14, including half-hour and negative offsets;
- unsynchronized startup and concurrent clock reads/zone changes.

The firmware build workflow runs this suite for both flash layouts. Host libc
tests do not substitute for an ESP32 hardware switchover test; that remains
pending. The curated rules follow current policies, not historical IANA changes
or every European jurisdiction.

References: [issue #3](https://github.com/leaberry/ESP32C6-APsystems-ECU/issues/3),
[UK clock changes](https://www.gov.uk/when-do-the-clocks-change), and
[EU summer-time arrangements](https://eur-lex.europa.eu/legal-content/EN/TXT/?uri=CELEX:32000L0084).
