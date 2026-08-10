struct EcuTimeZone {
  const char *id;
  const char *label;
  const char *rule;
};

// A compact curated set avoids shipping the IANA database while preserving
// the actual daylight-saving rules needed by common installations.
static const EcuTimeZone ECU_TIME_ZONES[] = {
  {"UTC", "UTC (no daylight saving)", "UTC0"},
  {"Pacific/Honolulu", "Hawaii — Honolulu", "HST10"},
  {"America/Anchorage", "Alaska — Anchorage", "AKST9AKDT,M3.2.0,M11.1.0"},
  {"America/Los_Angeles", "Pacific Time — Los Angeles", "PST8PDT,M3.2.0,M11.1.0"},
  {"America/Phoenix", "Mountain Time — Arizona", "MST7"},
  {"America/Denver", "Mountain Time — Denver", "MST7MDT,M3.2.0,M11.1.0"},
  {"America/Chicago", "Central Time — Chicago", "CST6CDT,M3.2.0,M11.1.0"},
  {"America/New_York", "Eastern Time — New York", "EST5EDT,M3.2.0,M11.1.0"},
  {"America/Halifax", "Atlantic Time — Halifax", "AST4ADT,M3.2.0,M11.1.0"},
  {"America/St_Johns", "Newfoundland Time — St. John's", "NST3:30NDT,M3.2.0,M11.1.0"},
  {"America/Sao_Paulo", "Brasilia Time — São Paulo", "BRT3"},
  {"Europe/London", "United Kingdom — London", "GMT0BST,M3.5.0/1,M10.5.0"},
  {"Europe/Berlin", "Central Europe — Berlin", "CET-1CEST,M3.5.0,M10.5.0/3"},
  {"Europe/Helsinki", "Eastern Europe — Helsinki", "EET-2EEST,M3.5.0/3,M10.5.0/4"},
  {"Africa/Johannesburg", "South Africa — Johannesburg", "SAST-2"},
  {"Asia/Dubai", "Gulf Time — Dubai", "GST-4"},
  {"Asia/Kolkata", "India — Kolkata", "IST-5:30"},
  {"Asia/Bangkok", "Indochina Time — Bangkok", "ICT-7"},
  {"Asia/Shanghai", "China — Shanghai", "CST-8"},
  {"Asia/Tokyo", "Japan — Tokyo", "JST-9"},
  {"Australia/Adelaide", "Australia Central — Adelaide", "ACST-9:30ACDT,M10.1.0,M4.1.0/3"},
  {"Australia/Sydney", "Australia Eastern — Sydney", "AEST-10AEDT,M10.1.0,M4.1.0/3"},
  {"Pacific/Auckland", "New Zealand — Auckland", "NZST-12NZDT,M9.5.0,M4.1.0/3"},
  {"Custom", "Custom fixed UTC offset", nullptr},
};

static const size_t ECU_TIME_ZONE_COUNT =
    sizeof(ECU_TIME_ZONES) / sizeof(ECU_TIME_ZONES[0]);

const EcuTimeZone *ecuTimeZoneById(const char *id) {
  for (size_t i = 0; i < ECU_TIME_ZONE_COUNT; ++i)
    if (!strcmp(ECU_TIME_ZONES[i].id, id)) return &ECU_TIME_ZONES[i];
  return &ECU_TIME_ZONES[0];
}

bool ecuTimeZoneIsValid(const char *id) {
  for (size_t i = 0; i < ECU_TIME_ZONE_COUNT; ++i)
    if (!strcmp(ECU_TIME_ZONES[i].id, id)) return true;
  return false;
}

String ecuTimeZoneOptionsHtml() {
  String options;
  options.reserve(2400);
  for (size_t i = 0; i < ECU_TIME_ZONE_COUNT; ++i) {
    options += F("<option value=\"");
    options += ECU_TIME_ZONES[i].id;
    options += '"';
    if (!strcmp(ECU_TIME_ZONES[i].id, timeZoneId)) options += F(" selected");
    options += '>';
    options += ECU_TIME_ZONES[i].label;
    options += F("</option>");
  }
  return options;
}

bool ecuSetLocalTimeFromUtc(time_t utcEpoch) {
  const EcuTimeZone *zone = ecuTimeZoneById(timeZoneId);
  if (!strcmp(zone->id, "Custom")) {
    currentUtcOffsetMinutes = constrain(atoi(gmtOffset), -720, 840);
    setTime(utcEpoch + (time_t)currentUtcOffsetMinutes * 60);
    dst = 0;
    return true;
  }

  setenv("TZ", zone->rule, 1);
  tzset();
  struct tm local = {};
  if (!localtime_r(&utcEpoch, &local)) return false;

  tmElements_t elements = {};
  elements.Second = local.tm_sec;
  elements.Minute = local.tm_min;
  elements.Hour = local.tm_hour;
  elements.Day = local.tm_mday;
  elements.Month = local.tm_mon + 1;
  elements.Year = CalendarYrToTm(local.tm_year + 1900);
  time_t localEpoch = makeTime(elements);
  currentUtcOffsetMinutes = (int16_t)((localEpoch - utcEpoch) / 60);
  dst = strchr(zone->rule, ',') ? (local.tm_isdst > 0 ? 1 : 2) : 0;
  setTime(localEpoch);
  return true;
}

const char *ecuTimeZoneLabel() {
  return ecuTimeZoneById(timeZoneId)->label;
}
