#include "HA_MODEL.h"
static WiFiClient haNetwork;
PubSubClient haClient(haNetwork);
String haBase,haSession;
static String haConnectedBroker;
static uint32_t haNextConnect=0,haNextWork=0,haNextState=0;
static bool haDiscoveryPending=true;
static int haDiscoveryCursor=-1,haStateCursor=-1,haControl=-1;
static int haControlValue=0;
static uint32_t haSampleAt[9]={};
static bool haHasSample[9]={};
static char haSampleSerial[9][13]={};
static String haControlStatus[9];
static char haControlSerial[13]={};
static JsonDocument haRegistry;
static bool haInventoryBootstrap=false,haInventoryCorrupt=false,haDisabledDone=false;
static uint32_t haInventorySequence=0,haInventoryAwaited=0,haInventorySentAt=0;

String haBrokerKey(){return String(Mqtt_Broker)+":"+Mqtt_Port;}
String haDeviceKey(int which){return "aps_ecu_"+String(ECU_ID)+(which<0?String():"_"+String(Inv_Prop[which].invSerial));}
String haStateTopic(int which){return haBase+(which<0?"/fleet/state":"/inverter/"+String(Inv_Prop[which].invSerial)+"/state");}
String haDiscoveryTopic(int which){return String(haDiscoveryPrefix)+"/device/"+haDeviceKey(which)+"/config";}

struct HaPublishWriter {
  PubSubClient &client;uint8_t buffer[256];size_t used=0;bool good=true;
  void flush(){if(used){good=good&&client.write(buffer,used)==used;used=0;}}
  size_t write(uint8_t c){buffer[used++]=c;if(used==sizeof(buffer))flush();return good?1:0;}
  size_t write(const uint8_t *p,size_t n){for(size_t i=0;i<n;++i)if(!write(p[i]))return i;return n;}
};

bool haPublishJson(const String &topic,JsonDocument &doc,bool retained=true) {
  if(doc.overflowed()||!haClient.connected())return false;
  size_t length=measureJson(doc);
  if(length>16384||!haClient.beginPublish(topic.c_str(),length,retained))return false;
  HaPublishWriter writer{haClient};size_t sent=serializeJson(doc,writer);writer.flush();
  return haClient.endPublish()&&writer.good&&sent==length;
}

bool haFresh(int which) {
  uint32_t allowed=max(60000UL,pollIntervalSeconds*2000UL+10000UL);
  return haHasSample[which]&&!strcmp(haSampleSerial[which],Inv_Prop[which].invSerial)&&
    (uint32_t)(millis()-haSampleAt[which])<=allowed;
}
void haTelemetry(int which) {
  if(!haEnabled||which<0||which>=9)return;
  haSampleAt[which]=millis();haHasSample[which]=true;
  strlcpy(haSampleSerial[which],Inv_Prop[which].invSerial,13);
}

void haAvailability(JsonObject component,const String &topic,const char *field) {
  JsonArray availability=component["avty"].to<JsonArray>();
  availability.add<JsonObject>()["t"]=haBase+"/status";
  JsonObject value=availability.add<JsonObject>();value["t"]=topic;
  value["val_tpl"]=String("{{ 'online' if value_json.")+field+" else 'offline' }}";
  component["avty_mode"]="all";
}

void haSensor(JsonObject components,int which,const char *key,const char *name,
              const char *deviceClass,const char *unit,const char *stateClass,
              const char *availabilityField,bool diagnostic=false) {
  JsonObject sensor=components[key].to<JsonObject>();
  sensor["p"]="sensor";sensor["name"]=name;sensor["uniq_id"]=haDeviceKey(which)+"_"+key;
  if(deviceClass&&deviceClass[0])sensor["dev_cla"]=deviceClass;
  if(unit&&unit[0])sensor["unit_of_meas"]=unit;
  if(stateClass&&stateClass[0])sensor["stat_cla"]=stateClass;
  sensor["val_tpl"]=String("{{ value_json.")+key+" }}";
  haAvailability(sensor,haStateTopic(which),availabilityField);
  if(diagnostic){sensor["ent_cat"]="diagnostic";sensor["enabled_by_default"]=false;}
}

void haDiscoveryDocument(JsonDocument &doc,int which) {
  doc.clear();JsonObject device=doc["dev"].to<JsonObject>();
  device["ids"]=haDeviceKey(which);device["name"]=which<0?String(fleetName):String(Inv_Prop[which].invLocation);
  device["mf"]="APsystems";device["mdl"]=which<0?"ESP32-C6 ECU":(Inv_Prop[which].invType==1?"QS1":Inv_Prop[which].invType==2?"DS3":"YC600");
  device["sw"]=VERSION;device["sn"]=which<0?ECU_ID:Inv_Prop[which].invSerial;
  device["cu"]="http://"+WiFi.localIP().toString()+"/";
  if(which>=0)device["via_device"]=haDeviceKey(-1);
  JsonObject origin=doc["o"].to<JsonObject>();origin["name"]="ESP32C6-APsystems-ECU";origin["sw"]=VERSION;
  origin["url"]="https://github.com/leaberry/ESP32C6-APsystems-ECU";
  doc["stat_t"]=haStateTopic(which);
  JsonObject components=doc["cmps"].to<JsonObject>();
  haSensor(components,which,"power_w","Solar power","power","W","measurement","power_valid");
  haSensor(components,which,"energy_kwh","Solar energy","energy","kWh","total_increasing","energy_valid");
  haSensor(components,which,"today_kwh","Production today","energy","kWh","","today_valid",true);
  if(which<0) {
    haSensor(components,which,"wifi_rssi","Wi-Fi signal","signal_strength","dBm","measurement","ecu_valid",true);
    haSensor(components,which,"uptime_s","Uptime","duration","s","measurement","ecu_valid",true);
    return;
  }
  haSensor(components,which,"temperature_c","Temperature","temperature","\xC2\xB0" "C","measurement","telemetry_valid");
  haSensor(components,which,"ac_voltage_v","AC voltage","voltage","V","measurement","telemetry_valid");
  haSensor(components,which,"frequency_hz","Frequency","frequency","Hz","measurement","telemetry_valid");
  haSensor(components,which,"radio_rssi","Radio signal","signal_strength","dBm","measurement","radio_valid",true);
  haSensor(components,which,"radio_lqi","Radio link quality","","","measurement","radio_valid",true);
  haSensor(components,which,"control_status","Power limit status","","","","ecu_valid",true);
  for(int panel=0;panel<4;++panel) {
    const char *suffixes[]={"power_w","voltage_v","current_a"};
    const char *classes[]={"power","voltage","current"};const char *units[]={"W","V","A"};
    for(int metric=0;metric<3;++metric) {
      char key[32],name[40];snprintf(key,sizeof(key),"panel%d_%s",panel+1,suffixes[metric]);
      snprintf(name,sizeof(name),"Panel %d %s",panel+1,metric==0?"power":metric==1?"voltage":"current");
      if(panel<(Inv_Prop[which].invType==1?4:2)&&Inv_Prop[which].conPanels[panel])
        haSensor(components,which,key,name,classes[metric],units[metric],"measurement",metric==0?"power_valid":"telemetry_valid",metric!=0);
      else components[key]["p"]="sensor"; // Explicit deletion on panel/model changes.
    }
  }
  JsonObject limit=components["power_limit"].to<JsonObject>();
  limit["p"]="number";limit["name"]="Per-input power limit";
  limit["uniq_id"]=haDeviceKey(which)+"_power_limit";
  limit["cmd_t"]=haBase+"/inverter/"+Inv_Prop[which].invSerial+"/limit/set";
  limit["cmd_tpl"]=String("{\"value\": {{ value | int }}, \"session\": \"")+haSession+"\"}";
  limit["val_tpl"]="{{ value_json.limit_w }}";
  limit["min"]=20;limit["max"]=500;limit["step"]=1;limit["mode"]="box";
  limit["unit_of_meas"]="W";limit["retain"]=false;limit["opt"]=false;
  haAvailability(limit,haStateTopic(which),"control_available");
}

void haStateDocument(JsonDocument &doc,int which) {
  doc.clear();bool night=pollingNightModeActive();
  doc["ecu_valid"]=true;doc["energy_valid"]=haMeterReady;doc["today_valid"]=timeRetrieved;
  if(which<0) {
    float watts=0;uint64_t today=0;bool valid=inverterCount>0;
    for(int i=0;i<inverterCount;++i){watts+=Inv_Data[i].pw_total;today+=energyTodayWhFor(i);valid=valid&&haFresh(i)&&polled[i];}
    doc["power_valid"]=night||valid;doc["power_w"]=night?0:round1(watts);
    if(haMeterReady)doc["energy_kwh"]=(double)haMeter.aggregate/1000000.0;else doc["energy_kwh"]="None";
    doc["today_kwh"]=(double)today/1000.0;
    doc["wifi_rssi"]=WiFi.RSSI();doc["uptime_s"]=millis()/1000;
    return;
  }
  bool fresh=haFresh(which)&&polled[which]&&!night;
  doc["telemetry_valid"]=fresh;doc["power_valid"]=fresh||night;
  doc["radio_valid"]=fresh&&Inv_Data[which].radioMetricsValid;
  doc["power_w"]=night?0:round1(Inv_Data[which].pw_total);
  int slot=haMeter.find(Inv_Prop[which].invSerial);
  if(haMeterReady)doc["energy_kwh"]=slot<0?0:(double)haMeter.entries[slot].committed/1000000.0;else doc["energy_kwh"]="None";
  doc["today_kwh"]=(double)energyTodayWhFor(which)/1000.0;
  doc["temperature_c"]=Inv_Data[which].heath;doc["ac_voltage_v"]=Inv_Data[which].acv;
  doc["frequency_hz"]=Inv_Data[which].freq;doc["radio_rssi"]=Inv_Data[which].radioRssi;
  doc["radio_lqi"]=Inv_Data[which].radioLqi;
  // Unknown is not an optimistic confirmation of the last command.
  doc["control_available"]=fresh&&haControl<0;
  if(desiredThrottle[which]>=20&&desiredThrottle[which]<=500)doc["limit_w"]=desiredThrottle[which];
  else doc["limit_w"]="None";
  doc["control_status"]=haControlStatus[which];
  for(int panel=0;panel<4;++panel) {
    char key[32];snprintf(key,sizeof(key),"panel%d_power_w",panel+1);doc[key]=night?0:Inv_Data[which].power[panel];
    snprintf(key,sizeof(key),"panel%d_voltage_v",panel+1);doc[key]=Inv_Data[which].dcv[panel];
    snprintf(key,sizeof(key),"panel%d_current_a",panel+1);doc[key]=Inv_Data[which].dcc[panel];
  }
}

void haReceive(char *topic,uint8_t *payload,unsigned int length) {
  if(String(topic)==haBase+"/inventory"){haRegistryReceive(payload,length);return;}
  if(String(topic)==haBase+"/energy"){haEnergyReceive(payload,length);return;}
  if(!haEnabled)return;
  if(String(topic)==String(haDiscoveryPrefix)+"/status") {
    if(length==6&&!memcmp(payload,"online",6)){haDiscoveryPending=true;haDiscoveryCursor=-1;}
    return;
  }
  if(length>160)return;
  int which=-1;
  for(int i=0;i<inverterCount;++i)if(String(topic)==haBase+"/inverter/"+Inv_Prop[i].invSerial+"/limit/set"){which=i;break;}
  if(which<0)return;
  JsonDocument command;
  if(deserializeJson(command,payload,length)||!command["value"].is<int>()||
     command["session"]!=haSession||command["value"].as<int>()<20||command["value"].as<int>()>500) {
    haControlStatus[which]="rejected: invalid or stale command";return;
  }
  if(haControl>=0||actionFlag||!haFresh(which)||!polled[which]||pollingNightModeActive()) {
    haControlStatus[which]="rejected: busy or inverter unavailable";return;
  }
  haControl=which;haControlValue=command["value"];haControlStatus[which]="pending";strlcpy(haControlSerial,Inv_Prop[which].invSerial,13);
}

bool haOwnDiscoveryTopic(const String &topic) {
  String marker="/device/"+haDeviceKey(-1);
  int at=topic.indexOf(marker);if(at<=0)return false;
  String prefix=topic.substring(0,at);if(!haPrefixValid(prefix.c_str()))return false;
  String tail=topic.substring(at+marker.length());
  return tail=="/config"||(tail.length()==20&&tail[0]=='_'&&tail.endsWith("/config")&&haHexId(tail.substring(1,13).c_str()));
}

void haRegistryReceive(uint8_t *payload,size_t length) {
  JsonDocument doc;
  if(length>8192||deserializeJson(doc,payload,length)){haInventoryCorrupt=true;return;}
  if(!doc["probe"].isNull()) {
    if(doc["probe"]==haSession)haInventoryBootstrap=!haInventoryCorrupt;
    return;
  }
  if(doc["version"]!=1||doc["ecu"]!=ECU_ID||!doc["topics"].is<JsonArray>()||doc["topics"].size()>32||
     !doc["sequence"].is<uint32_t>()||!doc["session"].is<const char *>()){haInventoryCorrupt=true;return;}
  for(JsonVariant item:doc["topics"].as<JsonArray>())
    if(!item.is<const char *>()||!haOwnDiscoveryTopic(item.as<String>())){haInventoryCorrupt=true;return;}
  bool acknowledgement=haInventoryAwaited&&doc["session"]==haSession&&doc["sequence"]==haInventoryAwaited;
  if(haInventoryBootstrap&&!acknowledgement)return;
  haRegistry.set(doc);if(acknowledgement)haInventoryAwaited=0;
}

bool haRegistrySave(JsonDocument &doc) {
  doc["version"]=1;doc["ecu"]=ECU_ID;doc["session"]=haSession;
  if(++haInventorySequence==0)++haInventorySequence;
  doc["sequence"]=haInventorySequence;
  if(!haPublishJson(haBase+"/inventory",doc))return false;
  haInventoryAwaited=haInventorySequence;haInventorySentAt=millis();return true;
}

bool haCleanupOne() {
  JsonArray topics=haRegistry["topics"].as<JsonArray>();

  for(size_t i=0;i<topics.size();++i) {
    String topic=topics[i].as<String>();bool keep=false;
    if(haEnabled)for(int unit=-1;unit<inverterCount;++unit)keep|=topic==haDiscoveryTopic(unit);
    if(keep)continue;
    if(!haClient.publish(topic.c_str(),"",true))return false;
    topics.remove(i);haRegistrySave(haRegistry);return false; // Pace one deletion per pass.
  }
  return true;
}

bool haDiscoverOne() {
  if(!haInventoryBootstrap||haInventoryCorrupt)return false;
  if(haInventoryAwaited){
    if((uint32_t)(millis()-haInventorySentAt)>=5000)haRegistrySave(haRegistry);
    return false;
  }
  if(!haCleanupOne())return false;
  if(!haEnabled)return true;
  if(haDiscoveryCursor>=inverterCount)return true;
  String topic=haDiscoveryTopic(haDiscoveryCursor);
  JsonArray topics=haRegistry["topics"].as<JsonArray>();bool known=false;
  for(JsonVariant item:topics)known|=item==topic;
  if(!known){if(topics.size()>=32)return false;topics.add(topic);if(!haRegistrySave(haRegistry))topics.remove(topics.size()-1);return false;}
  JsonDocument doc;haDiscoveryDocument(doc,haDiscoveryCursor);
  if(!haPublishJson(topic,doc))return false;
  // Follow component deletions with a retained config containing active entities only.
  JsonObject components=doc["cmps"].as<JsonObject>();
  bool removed=false;
  for(auto it=components.begin();it!=components.end();) {
    auto current=it; ++it;
    if(current->value().size()==1){components.remove(current);removed=true;}
  }
  if(removed&&!haPublishJson(topic,doc))return false;
  ++haDiscoveryCursor;return haDiscoveryCursor>=inverterCount;
}

void haBegin() {

  for(auto &status:haControlStatus)status="idle";
  haBase="aps_ecu/"+String(ECU_ID);
  haRegistry["topics"].to<JsonArray>();haEnergyBegin();
  haClient.setCallback(haReceive);haClient.setKeepAlive(30);haClient.setSocketTimeout(2);
  if(haEnabled||haConfigured)haClient.setBufferSize(8192);
}

void haLoop() {
  if((!haEnabled&&!haConfigured)||haDisabledDone)return;
  uint32_t now=millis();
  if(haClient.connected()&&haConnectedBroker!=haBrokerKey()) {
    haClient.publish((haBase+"/status").c_str(),"offline",true);haClient.disconnect();
  }
  if(!haClient.connected()) {
    if(WiFi.status()!=WL_CONNECTED||((int32_t)(now-haNextConnect)<0))return;
    haNextConnect=now+30000;haClient.setServer(Mqtt_Broker,atoi(Mqtt_Port));
    char session[17];snprintf(session,sizeof(session),"%08lX%08lX",(unsigned long)esp_random(),(unsigned long)esp_random());haSession=session;
    String clientId="aps-ha-"+String(ECU_ID),status=haBase+"/status";
    if(!haClient.connect(clientId.c_str(),Mqtt_Username,Mqtt_Password,status.c_str(),1,true,"offline"))return;
    haConnectedBroker=haBrokerKey();
    haDiscoveryPending=true;haDiscoveryCursor=-1;haStateCursor=-1;
    haInventoryBootstrap=false;haInventoryCorrupt=false;haInventoryAwaited=0;
    haRegistry.clear();haRegistry["topics"].to<JsonArray>();haEnergyReconnect();
    haClient.subscribe((haBase+"/inventory").c_str());
    if(haEnabled)haClient.subscribe((haBase+"/energy").c_str());
    // Non-retained same-topic probes establish a receive barrier after retained
    // recovery. They never replace the broker's stored checkpoints.
    JsonDocument probe;probe["probe"]=haSession;
    haPublishJson(haBase+"/inventory",probe,false);
    if(haEnabled)haPublishJson(haBase+"/energy",probe,false);
    if(haEnabled){haClient.subscribe((String(haDiscoveryPrefix)+"/status").c_str());haClient.subscribe((haBase+"/inverter/+/limit/set").c_str());}
    haClient.publish(status.c_str(),haEnabled?"online":"offline",true);
  }
  haClient.loop();
  haEnergyCommit();
  if(haControl>=0&&!actionFlag) {
    int which=haControl;haControl=-1;
    // Serial identity is rechecked against the last observed telemetry slot.
    if(which>=inverterCount||strcmp(haControlSerial,Inv_Prop[which].invSerial)||!haFresh(which)||!polled[which]){haControlStatus[which]="failed: inverter changed or unavailable";return;}
    desiredThrottle[which]=haControlValue;
    bool applied=setMaxPower(which);
    if(!applied)desiredThrottle[which]=-1;
    haControlStatus[which]=applied?"applied":"failed: inverter did not confirm";
    haNextState=0;
  }
  if((int32_t)(now-haNextWork)<0)return;
  haNextWork=now+250;
  if(haDiscoveryPending){haDiscoveryPending=!haDiscoverOne();return;}
  if(!haEnabled){haClient.disconnect();haDisabledDone=true;return;}
  if((int32_t)(now-haNextState)<0)return;
  if(haStateCursor>=inverterCount)haStateCursor=-1;
  JsonDocument state;haStateDocument(state,haStateCursor);
  if(haPublishJson(haStateTopic(haStateCursor),state)) {
    if(++haStateCursor>=inverterCount){haStateCursor=-1;haNextState=now+15000;}
  }
  // Refresh device/panel inventory and names without flooding discovery each poll.
  static uint32_t inventoryAt=0;
  static uint32_t inventoryHash=0;
  if(now-inventoryAt>=30000){
    inventoryAt=now;uint32_t hash=2166136261u;
    for(const char *c=fleetName;*c;++c)hash=(hash^(uint8_t)*c)*16777619u;
    for(int i=0;i<inverterCount;++i){
      for(const char *c=Inv_Prop[i].invSerial;*c;++c)hash=(hash^(uint8_t)*c)*16777619u;
      for(const char *c=Inv_Prop[i].invLocation;*c;++c)hash=(hash^(uint8_t)*c)*16777619u;
      hash=(hash^Inv_Prop[i].invType)*16777619u;
      for(bool connected:Inv_Prop[i].conPanels)hash=(hash^connected)*16777619u;
    }
    if(hash!=inventoryHash){inventoryHash=hash;haDiscoveryPending=true;haDiscoveryCursor=-1;}
  }
}
