#pragma once
#include <cstring>

// Return destinations must be pages, never API calls, actions or /back itself.
// Older firmware may have persisted an obsolete or incomplete URL in NVS.
inline const char *webReturnDestination(const char *candidate, int count) {
  static const char *const pages[] = {
    "/", "/menu", "/basicconfig", "/mqtt", "/time", "/journal", "/inverters",
    "/system", "/network", "/antenna", "/energy", "/settings", "/grid-profile",
    "/console", "/diagnostics", "/fleet-name", "/home-assistant"
  };
  if (!candidate) return "/";
  for (const char *page : pages) if (!strcmp(candidate, page)) return candidate;
  const char *prefixes[] = {"/inverter-details?inv=", "/inverter/select?welke="};
  for (const char *prefix : prefixes) {
    const size_t length = strlen(prefix);
    if (strncmp(candidate, prefix, length)) continue;
    const char *index = candidate + length;
    if (index[0] >= '0' && index[0] <= '8' && index[1] == '\0' && index[0] - '0' < count)
      return candidate;
    if (!strcmp(prefix, "/inverter/select?welke=") && !strcmp(index, "99") && count < 9)
      return candidate;
  }
  return "/";
}
