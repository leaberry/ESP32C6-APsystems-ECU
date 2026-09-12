#include "MQTT_CONFIG_UI.h"
#include "HA_CONFIG_UI.h"

void zendPageMQTTconfig(AsyncWebServerRequest *request) {
  String page = ecuPageStart(F("MQTT"), F("Configure the shared broker, then enable Home Assistant, Domoticz, or both."));
  String form = FPSTR(MQTT_CONFIG_UI);
  form.replace("{broker}", webEscape(Mqtt_Broker));
  form.replace("{port}", Mqtt_Port[0] ? webEscape(Mqtt_Port) : String("1883"));
  form.replace("{user}", webEscape(Mqtt_Username));
  form.replace("{out}", webEscape(Mqtt_outTopic));
  form.replace("{idx}", String(Mqtt_stateIDX));
  form.replace("{prefix}", webEscape(haDiscoveryPrefix));
  form.replace("{haEnabled}", haEnabled ? "checked" : "");
  form.replace("{legacyEnabled}", Mqtt_Format ? "checked" : "");
  String formats;
  for (int i=1; i<=5; ++i) {
    formats += "<option value=\"" + String(i) + "\"";
    if (i == (Mqtt_Format ? Mqtt_Format : Mqtt_savedFormat)) formats += " selected";
    formats += ">Format " + String(i) + "</option>";
  }
  form.replace("{formats}", formats);
  page += form;
  String help = FPSTR(HA_CONFIG_UI);
  page += help.substring(help.indexOf("<section class=\"card section\"><h2>Solar energy"));
  page += ecuPageEnd();
  request->send(200, "text/html; charset=utf-8", page);
}

// Validate a candidate before saving; disabled fieldsets are deliberately absent.
void mqttConfigSaveCombined(AsyncWebServerRequest *request) {
  if (!settingsAuthorized(request)) return;
  JsonDocument doc; mqttConfigDocument(doc);
  const char *fields[] = {"mqtAdres", "mqtPort", "mqtUser", "mqtPas"};
  const char *keys[] = {"Mqtt_Broker", "Mqtt_Port", "Mqtt_Username", "Mqtt_Password"};
  const size_t limits[] = {29, 4, 25, 25};
  for (size_t i=0; i<4; ++i) {
    if (!request->hasParam(fields[i],true)) { request->send(400,"text/plain","Missing broker settings"); return; }
    String value=request->getParam(fields[i],true)->value();
    if (value.length()>limits[i]) { request->send(400,"text/plain","Broker setting too long"); return; }
    if (i!=3 || value.length()) doc[keys[i]]=value;
  }
  bool ha=request->hasParam("haEnabled",true), legacy=request->hasParam("legacyEnabled",true);
  String port=doc["Mqtt_Port"].as<String>();
  char *end=nullptr; long portNumber=strtol(port.c_str(),&end,10);
  if (!port.length() || *end || portNumber<1 || portNumber>9999 ||
      ((ha||legacy) && !doc["Mqtt_Broker"].as<String>().length())) {
    request->send(400,"text/plain","Enter a broker address and port from 1 to 9999"); return;
  }
  if (ha) {
    if (!request->hasParam("prefix",true)) { request->send(400,"text/plain","Missing discovery prefix"); return; }
    String prefix=request->getParam("prefix",true)->value(); prefix.trim();
    if (!haPrefixValid(prefix.c_str())) { request->send(400,"text/plain","Invalid discovery prefix"); return; }
    doc["haDiscoveryPrefix"]=prefix;
  }
  if (legacy) {
    for (const char *name : {"fm","mqidx","mqtoutTopic"}) {
      if (!request->hasParam(name,true)) { request->send(400,"text/plain","Missing Domoticz settings"); return; }
    }
    String format=request->getParam("fm",true)->value(), idx=request->getParam("mqidx",true)->value();
    String topic=request->getParam("mqtoutTopic",true)->value();
    long number=strtol(idx.c_str(),&end,10);
    if (format.length()!=1 || format[0]<'1' || format[0]>'5' || !idx.length() || *end || number<0 || number>65535 || topic.length()>39) {
      request->send(400,"text/plain","Invalid Domoticz format, device ID or topic"); return;
    }
    doc["Mqtt_savedFormat"]=format.toInt();
    doc["Mqtt_stateIDX"]=number; doc["Mqtt_outTopic"]=topic;
  }
  doc["Mqtt_Format"]=legacy ? doc["Mqtt_savedFormat"].as<int>() : 0;
  doc["haEnabled"]=ha; doc["haConfigured"]=haConfigured || ha;
  if (!settingsWriteJson("/mqttconfig.json",doc.as<JsonVariantConst>())) {
    request->send(500,"text/plain","Could not save MQTT settings. No restart scheduled."); return;
  }
  request->send(200,"text/plain","MQTT settings saved. Restarting ECU."); actionFlag=10;
}
