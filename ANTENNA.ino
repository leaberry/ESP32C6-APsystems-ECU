#include "ANTENNA_UI.h"
// Saved as one NVS value, so a failed/interrupted save cannot mix pin settings.
bool antennaLoad(AntennaSettings &s) {
  s = AntennaSettings();
  Preferences prefs;
  if (!prefs.begin("aps-antenna", true)) return true; // first boot
  String saved = prefs.getString("config", "");
  prefs.end();
  if (saved.isEmpty()) return true;
  JsonDocument doc;
  if (deserializeJson(doc, saved) || doc["version"] != 1 ||
      !doc["mode"].is<int>() || !doc["board"].is<int>()) return false;
  s.mode = doc["mode"].as<int>();
  s.board = doc["board"].as<int>();
  if (s.board == 1 && s.mode != 0) {
    if (!doc["selectPin"].is<int>() || !doc["enablePin"].is<int>() ||
        !doc["externalHigh"].is<bool>() || !doc["enableHigh"].is<bool>()) return false;
    s.selectPin = doc["selectPin"].as<int>();
    s.enablePin = doc["enablePin"].as<int>();
    s.externalHigh = doc["externalHigh"].as<bool>();
    s.enableHigh = doc["enableHigh"].as<bool>();
  }
  antennaPreset(s);
  return validAntennaSettings(s, knop, led_onb);
}

bool antennaSave(const AntennaSettings &s) {
  JsonDocument doc;
  doc["version"] = 1;
  doc["mode"] = s.mode;
  doc["board"] = s.board;
  doc["selectPin"] = s.selectPin;
  doc["enablePin"] = s.enablePin;
  doc["externalHigh"] = s.externalHigh;
  doc["enableHigh"] = s.enableHigh;
  String json;
  serializeJson(doc, json);
  Preferences prefs;
  if (!prefs.begin("aps-antenna", false)) return false;
  bool saved = prefs.putString("config", json) == json.length();
  prefs.end();
  return saved;
}

void antennaBegin() {
  AntennaSettings s;
  if (!antennaLoad(s)) {
    Serial.println(F("Invalid antenna settings; leaving RF switch unmanaged"));
    return;
  }
  if (!s.mode) return; // No pinMode or digitalWrite in unmanaged mode.
  if (s.enablePin >= 0) {
    pinMode(s.enablePin, OUTPUT);
    digitalWrite(s.enablePin, s.enableHigh ? HIGH : LOW);
    delay(100); // Seeed's switch enable settling interval.
  }
  pinMode(s.selectPin, OUTPUT);
  digitalWrite(s.selectPin, antennaSelectLevel(s) ? HIGH : LOW);
  Serial.println(s.mode == 1 ? F("Antenna: internal") : F("Antenna: external"));
}


void antennaPage(AsyncWebServerRequest *request) {
  AntennaSettings s;
  bool valid = antennaLoad(s);
  if (!valid) s = AntennaSettings();
  String page = ecuPageStart(F("Antenna"), F("Select the antenna used for Wi-Fi and inverter communication."));
  if (!valid) page += F("<p class=\"alert\">Saved antenna settings are invalid. The antenna is unmanaged. Save corrected settings below.</p>");
  page += F("<p>These are saved settings. Restart after saving to apply them. Connect a suitable antenna before selecting External.</p>");
  String form = FPSTR(ANTENNA_FORM);
  for (int i = 0; i < 3; ++i) form.replace("{mode" + String(i) + "}", s.mode == i ? "selected" : "");
  for (int i = 0; i < 2; ++i) {
    form.replace("{board" + String(i) + "}", s.board == i ? "selected" : "");
    form.replace("{external" + String(i) + "}", s.externalHigh == bool(i) ? "selected" : "");
    form.replace("{enable" + String(i) + "}", s.enableHigh == bool(i) ? "selected" : "");
  }
  form.replace("{selectPin}", String(s.selectPin));
  form.replace("{enablePin}", String(s.enablePin >= 0 ? s.enablePin : 3));
  form.replace("{useEnable}", s.enablePin >= 0 ? "checked" : "");
  page += form;
  page += ecuPageEnd();
  request->send(200, "text/html", page);
}

bool antennaFormInt(AsyncWebServerRequest *request, const char *key, int &out) {
  if (!request->hasParam(key, true)) return false;
  String value = request->getParam(key, true)->value();
  if (value.isEmpty() || value.length() > 2) return false;
  for (unsigned i = 0; i < value.length(); ++i)
    if (value[i] < '0' || value[i] > '9') return false;
  out = value.toInt();
  return true;
}

void antennaHandleSave(AsyncWebServerRequest *request) {
  AntennaSettings s;
  bool valid = antennaFormInt(request, "mode", s.mode);
  if (s.mode != 0) valid = valid && antennaFormInt(request, "board", s.board);
  if (valid && s.mode != 0 && s.board == 1) {
    int external = 0, enable = 0;
    valid = antennaFormInt(request, "selectPin", s.selectPin) &&
        antennaFormInt(request, "externalHigh", external) && external <= 1;
    s.externalHigh = external == 1;
    s.enablePin = -1;
    if (request->hasParam("useEnable", true)) {
      valid = valid && antennaFormInt(request, "enablePin", s.enablePin) &&
          antennaFormInt(request, "enableHigh", enable) && enable <= 1;
      s.enableHigh = enable == 1;
    }
  }
  antennaPreset(s); // Presets never trust posted Advanced GPIO/polarity fields.
  if (!valid || !validAntennaSettings(s, knop, led_onb)) {
    request->send(400, "text/plain", "Invalid antenna settings: select a supported board or distinct, unreserved GPIOs in Advanced");
    return;
  }
  if (!antennaSave(s)) {
    request->send(500, "text/plain", "Antenna settings could not be saved; no pins were changed");
    return;
  }
  String page = ecuPageStart(F("Antenna settings saved"), F("Restart the ECU to apply your antenna selection."));
  page += F("<div class=\"actions\"><a class=\"button\" href=\"/reboot\">Restart ECU</a><a class=\"button secondary\" href=\"/antenna\">Back to antenna settings</a></div>");
  page += ecuPageEnd();
  request->send(200, "text/html", page);
}
