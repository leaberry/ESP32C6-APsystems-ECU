#include "HA_CONFIG_UI.h"

void haConfigPage(AsyncWebServerRequest *request) {
  if(!settingsAuthorized(request))return;
  request->redirect("/mqtt#home-assistant");
}
void haConfigSave(AsyncWebServerRequest *request) {
  if(!settingsAuthorized(request))return;
  if(!request->hasParam("prefix",true)){request->send(400,"text/plain","Missing discovery prefix");return;}
  String prefix=request->getParam("prefix",true)->value();prefix.trim();
  bool enabled=request->hasParam("enabled",true);
  if(!haPrefixValid(prefix.c_str())||(enabled&&!Mqtt_Broker[0])){request->send(400,"text/plain","Invalid discovery prefix or missing MQTT broker");return;}
  JsonDocument doc;mqttConfigDocument(doc);
  doc["haEnabled"]=enabled;doc["haConfigured"]=true;doc["haDiscoveryPrefix"]=prefix;
  // Only an explicit settings save writes the existing configuration file.
  if(!settingsWriteJson("/mqttconfig.json",doc.as<JsonVariantConst>())) {
    request->send(500,"text/plain","Could not save Home Assistant settings. No restart scheduled.");return;
  }
  request->send(200,"text/plain","Home Assistant settings saved. Restarting; existing Domoticz settings are unchanged.");actionFlag=10;
}
void haRoutes() {
  server.on("/home-assistant/save",HTTP_POST,haConfigSave);
  server.on("/home-assistant",HTTP_GET,haConfigPage);
}
