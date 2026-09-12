#pragma once
#include "DEVICE_SETTINGS.h"
#include "HA_MODEL.h"
#include <ArduinoJson.h>
#include <math.h>

// Versioned logical values, never raw structs, filesystem paths or NVS images.
static const size_t SETTINGS_MAX_BYTES = 32768;
inline bool settingsText(JsonVariantConst v, size_t max, size_t min = 0) {
  if (!v.is<const char *>()) return false;
  JsonString s = v.as<JsonString>();
  if (s.size() < min || s.size() > max || strlen(s.c_str()) != s.size()) return false;
  for (size_t i=0; i<s.size(); ++i) if ((uint8_t)s.c_str()[i]<32 || s.c_str()[i]==127) return false;
  return true;
}
inline bool settingsHex(JsonVariantConst v, size_t n) {
  if (!settingsText(v,n,n)) return false;
  for (const char *p=v.as<const char *>(); *p; ++p)
    if (!((*p>='0'&&*p<='9')||(*p>='A'&&*p<='F'))) return false;
  return true;
}
inline bool settingsInt(JsonVariantConst v, int low, int high) {
  return v.is<int>() && v.as<int>()>=low && v.as<int>()<=high;
}
inline bool settingsNumber(JsonVariantConst v, double low, double high) {
  return v.is<double>() && isfinite(v.as<double>()) && v.as<double>()>=low && v.as<double>()<=high;
}
inline AntennaSettings settingsAntenna(JsonVariantConst v) {
  AntennaSettings s;
  s.mode=v["mode"]; s.board=v["board"]; s.selectPin=v["selectPin"];
  s.enablePin=v["enablePin"]; s.externalHigh=v["externalHigh"]; s.enableHigh=v["enableHigh"];
  return s;
}
// CRC is an accidental-corruption check, not authentication or encryption.
struct SettingsCrcWriter {
  uint32_t crc=0xFFFFFFFF;
  size_t write(uint8_t b) {
    crc ^= b;
    for (int i=0;i<8;++i) crc=(crc>>1)^((crc&1)?0xEDB88320:0);
    return 1;
  }
  size_t write(const uint8_t *p,size_t n) { for(size_t i=0;i<n;++i) write(p[i]); return n; }
};
inline void settingsChecksum(JsonVariantConst payload, char out[9]) {
  SettingsCrcWriter writer; serializeJson(payload,writer);
  snprintf(out,9,"%08lX",(unsigned long)(writer.crc^0xFFFFFFFF));
}
inline void settingsSeal(JsonDocument &doc) {
  doc["format"]="aps-ecu-settings"; doc["version"]=1;
  char crc[9]; settingsChecksum(doc["payload"],crc); doc["crc32"]=crc;
}
inline bool settingsFormatValid(JsonDocument &doc, int button, int led) {
  if (doc.overflowed() || !doc.is<JsonObject>() ||
      doc["format"]!="aps-ecu-settings" || !settingsInt(doc["version"],1,1) ||
      !settingsHex(doc["crc32"],8) || !doc["payload"].is<JsonObject>()) return false;
  char crc[9]; settingsChecksum(doc["payload"],crc);
  if (strcmp(crc,doc["crc32"].as<const char *>())) return false;
  JsonVariantConst p=doc["payload"], b=p["basis"], w=p["timeSecurity"], m=p["mqtt"], n=p["network"], a=p["antenna"];
  if (!settingsHex(p["radioIEEE"],16) || p["radioIEEE"]=="0000000000000000" ||
      p["radioIEEE"]=="FFFFFFFFFFFFFFFF" || !settingsHex(p["sourceBoard"],12) ||
      !settingsHex(b["ECU_ID"],12) || !settingsText(b["fleetName"],48,1) ||
      !settingsText(b["userPwd"],32,1) || !settingsInt(b["schemaVersion"],3,3) ||
      !settingsInt(b["inverterCount"],0,9) || !settingsInt(b["pollOffset"],-86400,86400) ||
      !settingsInt(b["pollIntervalSeconds"],1,86400) || !b["Polling"].is<bool>() ||
      !b["sunspecEnabled"].is<bool>() || !b["flightRecorderEnabled"].is<bool>()) return false;
  if (!settingsText(w["pswd"],32,1) || !settingsText(w["gmtOffset"],4,1) ||
      !settingsText(w["timeZoneId"],23,1) || !settingsText(w["ntpServer"],253,1) ||
      !validNtpServer(w["ntpServer"]) || !settingsNumber(w["lati"],-90,90) ||
      !settingsNumber(w["longi"],-180,180) || !settingsInt(w["securityLevel"],0,9) ||
      !w["zomerTijd"].is<bool>() || !w["daylightPolling"].is<bool>() ||
      !w["locationConfigured"].is<bool>()) return false;
  const char *offset=w["gmtOffset"];
  char *end=nullptr; long minutes=strtol(offset,&end,10);
  if (!end || *end || minutes < -720 || minutes > 840) return false;
  if ((!m["haEnabled"].isNull()&&!m["haEnabled"].is<bool>()) ||
      (!m["haConfigured"].isNull()&&!m["haConfigured"].is<bool>()) ||
      (!m["haDiscoveryPrefix"].isNull()&&(!settingsText(m["haDiscoveryPrefix"],48,1)||!haPrefixValid(m["haDiscoveryPrefix"])))) return false;
  if (!settingsText(m["Mqtt_Broker"],29) || !settingsText(m["Mqtt_Port"],4,1) ||
      !settingsText(m["Mqtt_outTopic"],39) || !settingsText(m["Mqtt_Username"],25) ||
      !settingsText(m["Mqtt_Password"],25) || !settingsInt(m["Mqtt_Format"],0,5) ||
      !settingsInt(m["Mqtt_stateIDX"],0,65535)) return false;
  long port=strtol(m["Mqtt_Port"].as<const char *>(),&end,10);
  if (*end || port<1 || port>9999) return false;
  if (!settingsText(n["ssid"],32) || !settingsText(n["password"],63) ||
      !settingsText(n["hostname"],31,1) || !n["dhcp"].is<bool>() ||
      !settingsText(n["ip"],15) || !settingsText(n["netmask"],15) ||
      !settingsText(n["gateway"],15)) return false;
  const char *hostname=n["hostname"];
  size_t len=strlen(hostname);
  for(size_t i=0;i<len;++i) if (!((hostname[i]>='a'&&hostname[i]<='z')||
      (hostname[i]>='0'&&hostname[i]<='9')||(hostname[i]=='-'&&i&&i+1<len))) return false;
  if (!settingsInt(a["mode"],0,2) || !settingsInt(a["board"],0,1) ||
      !settingsInt(a["selectPin"],-1,30) || !settingsInt(a["enablePin"],-1,30) ||
      !a["externalHigh"].is<bool>() || !a["enableHigh"].is<bool>()) return false;
  AntennaSettings antenna=settingsAntenna(a);
  // Presets must contain their canonical GPIO values too.
  if (!antenna.board && (antenna.selectPin!=14 || antenna.enablePin!=3 ||
      !antenna.externalHigh || antenna.enableHigh)) return false;
  if (!validAntennaSettings(antenna,button,led)) return false;
  if (!p["inverters"].is<JsonArrayConst>() || p["inverters"].size()!=b["inverterCount"].as<size_t>() ||
      !p["peers"].is<JsonArrayConst>() || p["peers"].size()>64) return false;
  for (JsonVariantConst inv:p["inverters"].as<JsonArrayConst>()) {
    if (!settingsHex(inv["serial"],12) || !settingsHex(inv["id"],4) ||
        !settingsText(inv["name"],12) || !settingsInt(inv["type"],0,2) ||
        !settingsInt(inv["mqttIdx"],0,65535) || !settingsInt(inv["calibration"],-15,15) ||
        !settingsInt(inv["powerLimit"],-1,10000) || !inv["panels"].is<JsonArrayConst>() ||
        inv["panels"].size()!=4) return false;
    for(JsonVariantConst panel:inv["panels"].as<JsonArrayConst>()) if(!panel.is<bool>()) return false;
    int matches=0;
    for(JsonVariantConst other:p["inverters"].as<JsonArrayConst>())
      if(other["serial"]==inv["serial"]) ++matches;
    if(matches!=1) return false;
  }
  for(JsonVariantConst peer:p["peers"].as<JsonArrayConst>()) {
    if (!settingsHex(peer["serial"],12) || !settingsInt(peer["pan"],1,65534) ||
        !settingsInt(peer["source"],1,65527) || peer["source"]==0x1111) return false;
    int matches=0;
    for(JsonVariantConst other:p["peers"].as<JsonArrayConst>())
      if(other["serial"]==peer["serial"]) ++matches;
    if(matches!=1) return false;
  }
  return true;
}
