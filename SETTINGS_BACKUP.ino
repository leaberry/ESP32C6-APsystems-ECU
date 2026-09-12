#include "SETTINGS_FORMAT.h"
#include "SETTINGS_UI.h"
#include <nvs.h>

static const char SETTINGS_PENDING[]="/settings-restore.pending";
static const char SETTINGS_STAGED[]="/settings-restore.tmp";
static bool settingsBootOk=false;
static bool settingsStageBusy=false;
static portMUX_TYPE settingsStageMux=portMUX_INITIALIZER_UNLOCKED;

bool settingsBootReady() { return settingsBootOk; }

bool settingsHasRestoredIdentity() {
  nvs_handle_t handle;
  esp_err_t opened=nvs_open("aps-identity",NVS_READONLY,&handle);
  if(opened==ESP_ERR_NVS_NOT_FOUND) return false;
  if(opened!=ESP_OK) return true; // Do not rotate identity on a storage error.
  size_t size=0;
  esp_err_t result=nvs_get_str(handle,"radioIEEE",nullptr,&size);
  nvs_close(handle);
  return result!=ESP_ERR_NVS_NOT_FOUND;
}

String settingsBoardId() {
  uint8_t mac[6]={};
  if (esp_read_mac(mac,ESP_MAC_WIFI_STA)!=ESP_OK) return String();
  char text[13];
  for(int i=0;i<6;++i) snprintf(text+i*2,3,"%02X",mac[i]);
  return String(text);
}

// The stored bytes use the same order as esp_ieee802154_set_extended_address.
// Preserve the current hardware-derived identity until an explicit restore.
bool settingsRadioAddress(uint8_t address[8]) {
  nvs_handle_t handle;
  esp_err_t opened=nvs_open("aps-identity",NVS_READONLY,&handle);
  char stored[17]={};
  if(opened==ESP_OK) {
    size_t length=sizeof(stored);
    esp_err_t read=nvs_get_str(handle,"radioIEEE",stored,&length);
    nvs_close(handle);
    if(read!=ESP_OK && read!=ESP_ERR_NVS_NOT_FOUND) return false;
    if(read==ESP_OK && length!=sizeof(stored)) return false;
  } else if(opened!=ESP_ERR_NVS_NOT_FOUND) return false;
  String saved(stored);
  if(saved.isEmpty()) {
    uint8_t ieee[8];
    if(esp_read_mac(ieee,ESP_MAC_IEEE802154)!=ESP_OK) return false;
    for(int i=0;i<8;++i) address[i]=ieee[7-i];
    return true;
  }
  JsonDocument doc; doc["address"]=saved;
  if(!settingsHex(doc["address"],16) || saved=="0000000000000000" || saved=="FFFFFFFFFFFFFFFF") return false;
  for(int i=0;i<8;++i) {
    char byte[3]={saved[i*2],saved[i*2+1],0}; address[i]=strtoul(byte,nullptr,16);
  }
  return true;
}

void settingsNetworkDocument(JsonObject object) {
  String ssid,password,hostname,ip,netmask,gateway; bool dhcp;
  loadStoredWifiCredentials(ssid,password,hostname);
  loadStoredWifiAddressing(dhcp,ip,netmask,gateway);
  object["ssid"]=ssid; object["password"]=password; object["hostname"]=hostname;
  object["dhcp"]=dhcp; object["ip"]=ip; object["netmask"]=netmask; object["gateway"]=gateway;
}

void settingsAntennaDocument(JsonObject object,const AntennaSettings &s) {
  object["mode"]=s.mode; object["board"]=s.board; object["selectPin"]=s.selectPin;
  object["enablePin"]=s.enablePin; object["externalHigh"]=s.externalHigh;
  object["enableHigh"]=s.enableHigh;
}

bool settingsValidate(JsonDocument &doc) {
  if(!settingsFormatValid(doc,knop,led_onb)) return false;
  JsonVariantConst p=doc["payload"], n=p["network"];
  if(!ecuTimeZoneIsValid(p["timeSecurity"]["timeZoneId"])) return false;
  if(!n["dhcp"].as<bool>()) {
    IPAddress ip,mask,gateway;
    if(!ip.fromString(n["ip"].as<const char *>()) ||
       !mask.fromString(n["netmask"].as<const char *>()) ||
       !gateway.fromString(n["gateway"].as<const char *>())) return false;
  }
  return true;
}

bool settingsNamespaceReadable(const char *name) {
  nvs_handle_t handle;
  esp_err_t opened=nvs_open(name,NVS_READONLY,&handle);
  if(opened==ESP_ERR_NVS_NOT_FOUND) return true;
  if(opened!=ESP_OK) return false;
  nvs_iterator_t iterator=nullptr;
  esp_err_t result=nvs_entry_find("nvs",name,NVS_TYPE_ANY,&iterator);
  bool good=true;
  while(result==ESP_OK) {
    nvs_entry_info_t info; nvs_entry_info(iterator,&info);
    if(info.type==NVS_TYPE_STR) {
      char value[512]; size_t length=sizeof(value);
      good=nvs_get_str(handle,info.key,value,&length)==ESP_OK;
    } else if(info.type==NVS_TYPE_U8) {
      uint8_t value; good=nvs_get_u8(handle,info.key,&value)==ESP_OK;
    } else if(info.type==NVS_TYPE_I32) {
      int32_t value; good=nvs_get_i32(handle,info.key,&value)==ESP_OK;
    } else good=false;
    if(!good) break;
    result=nvs_entry_next(&iterator);
  }
  nvs_release_iterator(iterator);nvs_close(handle);
  return good && result==ESP_ERR_NVS_NOT_FOUND;
}

bool settingsBuildBackup(JsonDocument &doc) {
  if(!settingsNamespaceReadable("aps-wifi") || !settingsNamespaceReadable("aps-antenna") ||
     !settingsNamespaceReadable("my_data")) return false;
  doc.clear(); JsonObject p=doc["payload"].to<JsonObject>();
  p["sourceBoard"]=settingsBoardId();
  uint8_t address[8]; if(!settingsRadioAddress(address)) return false;
  char ieee[17]; for(int i=0;i<8;++i) snprintf(ieee+i*2,3,"%02X",address[i]);
  p["radioIEEE"]=ieee;
  JsonDocument section;
  basisConfigDocument(section); p["basis"].set(section);
  wifiConfigDocument(section); p["timeSecurity"].set(section);
  mqttConfigDocument(section); p["mqtt"].set(section);
  settingsNetworkDocument(p["network"].to<JsonObject>());
  AntennaSettings antenna; if(!antennaLoad(antenna)) return false;
  settingsAntennaDocument(p["antenna"].to<JsonObject>(),antenna);
  JsonArray inverters=p["inverters"].to<JsonArray>();
  // Export persisted inverter configuration; a half-written record is an error.
  Preferences limits; bool haveLimits=limits.begin("my_data",true);
  bool good=true;
  for(int i=0;i<9 && good;++i) {
    String path="/Inv_Prop"+String(i)+".str";
    if(SPIFFS.exists(path+".pair") || SPIFFS.exists(path+".pair-old")) {good=false;break;}
    if(!SPIFFS.exists(path)) continue;
    // Keep slot order stable: an unresolved gap must be repaired before backup.
    if(inverters.size()!=(size_t)i) {good=false;break;}
    File file=SPIFFS.open(path,"r");
    auto inv=Inv_Prop[i];
    good=file && file.size()==sizeof(inv) && file.read((uint8_t *)&inv,sizeof(inv))==sizeof(inv);
    file.close();
    if(!good || !memchr(inv.invSerial,0,sizeof(inv.invSerial)) ||
       !memchr(inv.invLocation,0,sizeof(inv.invLocation)) || !memchr(inv.invID,0,sizeof(inv.invID))) {good=false;break;}
    JsonObject item=inverters.add<JsonObject>();
    for(int j=0;j<4;++j) if(*reinterpret_cast<const uint8_t *>(&inv.conPanels[j])>1) good=false;
    if(!good) break;
    item["serial"]=inv.invSerial; item["id"]=inv.invID; item["name"]=inv.invLocation;
    item["type"]=inv.invType; item["mqttIdx"]=inv.invIdx; item["calibration"]=inv.calib;
    String key="maxPwr"+String(i);
    item["powerLimit"]=haveLimits?limits.getInt(key.c_str(),-1):-1;
    JsonArray panels=item["panels"].to<JsonArray>();
    for(int j=0;j<4;++j) panels.add(inv.conPanels[j]);
  }
  if(haveLimits) limits.end();
  if(!good) return false;
  p["basis"]["inverterCount"]=inverters.size();
  JsonArray peers=p["peers"].to<JsonArray>();
  nvs_iterator_t iterator=nullptr;
  esp_err_t result=nvs_entry_find("nvs","apsradio",NVS_TYPE_ANY,&iterator);
  Preferences radio;
  if(result==ESP_OK && !radio.begin("apsradio",true)) {nvs_release_iterator(iterator);return false;}
  while(result==ESP_OK) {
    nvs_entry_info_t info; nvs_entry_info(iterator,&info);
    if(info.type!=NVS_TYPE_U32 || peers.size()>=64) {good=false;break;}
    uint32_t peer=radio.getUInt(info.key,0);
    JsonObject item=peers.add<JsonObject>();
    item["serial"]=info.key; item["pan"]=peer>>16; item["source"]=peer&0xFFFF;
    result=nvs_entry_next(&iterator);
  }
  nvs_release_iterator(iterator); radio.end();
  if(!good || result!=ESP_ERR_NVS_NOT_FOUND) return false;
  settingsSeal(doc);
  return measureJson(doc)<=SETTINGS_MAX_BYTES && settingsValidate(doc);
}

bool settingsWriteBytes(const char *path,const uint8_t *bytes,size_t size) {
  File file=SPIFFS.open(path,"w");
  if(!file) return false;
  bool good=file.write(bytes,size)==size; file.flush(); file.close();
  if(!good) return false;
  file=SPIFFS.open(path,"r");
  good=file && file.size()==size;
  uint8_t buffer[128]; size_t offset=0;
  while(good && offset<size) {
    size_t count=min(sizeof(buffer),size-offset);
    good=file.read(buffer,count)==count && !memcmp(buffer,bytes+offset,count); offset+=count;
  }
  file.close(); return good;
}

bool settingsWriteJson(const char *path,JsonVariantConst value) {
  String text; if(!serializeJson(value,text) || text.length()!=measureJson(value)) return false;
  return settingsWriteBytes(path,(const uint8_t *)text.c_str(),text.length());
}

bool settingsRemove(const String &path) { return !SPIFFS.exists(path) || SPIFFS.remove(path); }

// Replay from a complete durable manifest. No radio, web server or polling is
// allowed until every store has been written and read back successfully.
bool settingsApply(JsonDocument &doc) {
  JsonVariantConst p=doc["payload"], n=p["network"];
  if(!settingsWriteJson("/basisconfig.json",p["basis"]) ||
     !settingsWriteJson("/wificonfig.json",p["timeSecurity"]) ||
     !settingsWriteJson("/mqttconfig.json",p["mqtt"])) return false;
  if(!saveStoredWifiConfiguration(n["ssid"].as<const char *>(),n["password"].as<const char *>(),
      n["hostname"].as<const char *>(),n["dhcp"],n["ip"].as<const char *>(),
      n["netmask"].as<const char *>(),n["gateway"].as<const char *>())) return false;
  AntennaSettings antenna=settingsAntenna(p["antenna"]);
  if(!antennaSave(antenna)) return false;
  AntennaSettings check;
  if(!antennaLoad(check) || check.mode!=antenna.mode || check.board!=antenna.board ||
     check.selectPin!=antenna.selectPin || check.enablePin!=antenna.enablePin ||
     check.externalHigh!=antenna.externalHigh || check.enableHigh!=antenna.enableHigh) return false;
  Preferences identity;
  if(!identity.begin("aps-identity",false)) return false;
  bool good=wifiSaveString(identity,"radioIEEE",p["radioIEEE"].as<const char *>());
  identity.end(); if(!good) return false;
  Preferences radio;
  if(!radio.begin("apsradio",false)) return false;
  good=radio.clear();
  for(JsonVariantConst peer:p["peers"].as<JsonArrayConst>()) {
    uint32_t value=(peer["pan"].as<uint32_t>()<<16)|peer["source"].as<uint32_t>();
    const char *serial=peer["serial"];
    if(good) good=radio.putUInt(serial,value)==4 && radio.getUInt(serial,0)==value;
  }
  radio.end(); if(!good) return false;
  Preferences limits;
  if(!limits.begin("my_data",false)) return false;
  for(int i=0;i<9 && good;++i) {
    String path="/Inv_Prop"+String(i)+".str", key="maxPwr"+String(i);
    if((size_t)i<p["inverters"].size()) {
      JsonVariantConst item=p["inverters"][i];
      inverters inv;
      strlcpy(inv.invSerial,item["serial"],sizeof(inv.invSerial));
      strlcpy(inv.invID,item["id"],sizeof(inv.invID));
      strlcpy(inv.invLocation,item["name"],sizeof(inv.invLocation));
      inv.invType=item["type"]; inv.invIdx=item["mqttIdx"]; inv.calib=item["calibration"];
      for(int j=0;j<4;++j) inv.conPanels[j]=item["panels"][j];
      inv.encrypted=apsSerialDefaultsToEncrypted(inv.invSerial);
      int power=item["powerLimit"];
      good=settingsWriteBytes(path.c_str(),(const uint8_t *)&inv,sizeof(inv)) &&
          limits.putInt(key.c_str(),power)==4 && limits.getInt(key.c_str(),-2)==power;
    } else {
      good=settingsRemove(path) && (!limits.isKey(key.c_str()) || limits.remove(key.c_str()));
    }
    if(good) good=settingsRemove(path+".pair") && settingsRemove(path+".pair-old");
  }
  limits.end();
  return good && settingsRemove("/basisconfig.identity.old") && settingsRemove("/basisconfig.identity.tmp");
}

bool settingsRestoreBoot() {
  settingsBootOk=false;
  if(!SPIFFS.exists(SETTINGS_PENDING)) {settingsBootOk=true;return true;}
  File file=SPIFFS.open(SETTINGS_PENDING,"r"); JsonDocument doc;
  bool good=file && file.size()<=SETTINGS_MAX_BYTES && !deserializeJson(doc,file);
  file.close();
  if(!good || !settingsValidate(doc) || !settingsApply(doc) || !SPIFFS.remove(SETTINGS_PENDING)) {
    Serial.println(F("Settings restore incomplete. Radios are disabled. Reset to retry the saved restore."));
    return false;
  }
  settingsBootOk=true;
  Serial.println(F("Settings restore complete; production history was not changed."));
  return true;
}

bool settingsStageRestore(JsonDocument &doc) {
  if(SPIFFS.exists(SETTINGS_PENDING)) return false;
  // Leave headroom for replacing configuration files while retaining the journal.
  size_t required=measureJson(doc)*2+8192;
  if(SPIFFS.totalBytes()-SPIFFS.usedBytes()<required) return false;
  if(!settingsWriteJson(SETTINGS_STAGED,doc.as<JsonVariantConst>())) return false;
  File file=SPIFFS.open(SETTINGS_STAGED,"r"); JsonDocument check;
  bool good=file && !deserializeJson(check,file); file.close();
  if(!good || !settingsValidate(check)) return false;
  return SPIFFS.rename(SETTINGS_STAGED,SETTINGS_PENDING);
}

bool settingsAuthorized(AsyncWebServerRequest *request) {
  if(checkRemote(request->client()->remoteIP().toString())) {request->send(403,"text/plain","Local access required");return false;}
  if(!request->authenticate("admin",pswd)) {request->requestAuthentication();return false;}
  return true;
}

void settingsDownload(AsyncWebServerRequest *request) {
  if(!settingsAuthorized(request)) return;
  JsonDocument doc;
  if(!settingsBuildBackup(doc)) {request->send(409,"text/plain","Could not create a complete, valid settings backup. Check configuration and pairing storage.");return;}
  AsyncResponseStream *response=request->beginResponseStream("application/json");
  response->addHeader("Content-Disposition",String("attachment; filename=\"ecu-settings-")+ECU_ID+".json\"");
  response->addHeader("Cache-Control","no-store");
  serializeJson(doc,*response); request->send(response);
}

void settingsPage(AsyncWebServerRequest *request) {
  if(!settingsAuthorized(request)) return;
  String page=ecuPageStart(F("Settings backup and restore"),F("Save ECU configuration or move an installation to a replacement board. Production history has its own backup and restore."));
  page+=FPSTR(SETTINGS_UI); page+=ecuPageEnd();
  AsyncWebServerResponse *response=request->beginResponse(200,"text/html; charset=utf-8",page);
  response->addHeader("Cache-Control","no-store");request->send(response);
}

struct SettingsUpload { size_t received; size_t total; bool failed; char data[1]; };

void settingsBody(AsyncWebServerRequest *request,uint8_t *data,size_t len,size_t index,size_t total) {
  if(!request->authenticate("admin",pswd) || checkRemote(request->client()->remoteIP().toString()) ||
     !request->hasHeader("X-ECU-Settings") || request->contentType()!="application/json") return;
  if(!index && !request->_tempObject && total && total<=SETTINGS_MAX_BYTES) {
    // ESPAsyncWebServer frees _tempObject on disconnect, including aborted uploads.
    auto upload=(SettingsUpload *)calloc(1,sizeof(SettingsUpload)+total);
    if(upload) {upload->total=total;request->_tempObject=upload;}
  }
  auto upload=(SettingsUpload *)request->_tempObject;
  if(!upload || upload->failed) return;
  if(index!=upload->received || total!=upload->total || index>total || len>total-index) {upload->failed=true;return;}
  memcpy(upload->data+index,data,len);upload->received+=len;
}

void settingsReceive(AsyncWebServerRequest *request,bool apply) {
  if(!settingsAuthorized(request)) return;
  auto upload=(SettingsUpload *)request->_tempObject;
  if(!upload || upload->failed || upload->received!=upload->total) {
    request->send(400,"text/plain","Missing, incomplete or oversized settings upload (maximum 32 KB).");return;
  }
  JsonDocument doc;
  if(deserializeJson(doc,upload->data,upload->total) || !settingsValidate(doc)) {
    request->send(400,"text/plain","Invalid settings backup: format, checksum or configuration validation failed. Nothing was changed.");return;
  }
  if(!apply) {
    JsonDocument preview;
    preview["ecuId"]=doc["payload"]["basis"]["ECU_ID"];
    preview["fleet"]=doc["payload"]["basis"]["fleetName"];
    preview["inverters"]=doc["payload"]["inverters"].size();
    preview["replacement"]=doc["payload"]["sourceBoard"]!=settingsBoardId();
    preview["radioIEEE"]=doc["payload"]["radioIEEE"];
    AsyncResponseStream *response=request->beginResponseStream("application/json");
    response->addHeader("Cache-Control","no-store");serializeJson(preview,*response);request->send(response);return;
  }
  if(!request->hasHeader("X-ECU-Restore") || request->getHeader("X-ECU-Restore")->value()!="confirmed") {
    request->send(400,"text/plain","Restore confirmation is required.");return;
  }
  portENTER_CRITICAL(&settingsStageMux);
  bool acquired=!settingsStageBusy; if(acquired) settingsStageBusy=true;
  portEXIT_CRITICAL(&settingsStageMux);
  if(!acquired) {request->send(409,"text/plain","A restore is already being staged.");return;}
  JsonObject p=doc["payload"];
  if(!request->hasHeader("X-ECU-Network") || request->getHeader("X-ECU-Network")->value()!="restore") {
    p.remove("network");settingsNetworkDocument(p["network"].to<JsonObject>());
  }
  bool good=settingsNamespaceReadable("aps-wifi") && settingsNamespaceReadable("aps-antenna");
  if(!request->hasHeader("X-ECU-Antenna") || request->getHeader("X-ECU-Antenna")->value()!="restore") {
    AntennaSettings antenna; good=good && antennaLoad(antenna);
    p.remove("antenna");settingsAntennaDocument(p["antenna"].to<JsonObject>(),antenna);
  }
  settingsSeal(doc);
  good=good && settingsValidate(doc) && settingsStageRestore(doc);
  if(!good) {
    portENTER_CRITICAL(&settingsStageMux);settingsStageBusy=false;portEXIT_CRITICAL(&settingsStageMux);
    request->send(500,"text/plain","Could not stage the restore. Current settings remain active; check storage and retry.");return;
  }
  request->send(200,"text/plain","Settings restore staged. Restarting to apply it. Sign in using the administrator password from the backup. Production history is unchanged.");
  actionFlag=10;
}

void settingsRoutes() {
  server.on("/settings/backup",HTTP_GET,settingsDownload);
  server.on("/settings/validate",HTTP_POST,[](AsyncWebServerRequest *r){settingsReceive(r,false);},nullptr,settingsBody);
  server.on("/settings/restore",HTTP_POST,[](AsyncWebServerRequest *r){settingsReceive(r,true);},nullptr,settingsBody);
  server.on("/settings",HTTP_GET,settingsPage);
}
