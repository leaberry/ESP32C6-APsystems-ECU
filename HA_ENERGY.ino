#include "HA_MODEL.h"
extern PubSubClient haClient;
extern String haBase,haSession;
bool haPublishJson(const String &topic,JsonDocument &doc,bool retained);
// RAM counters are recovered and checkpointed through retained MQTT messages.
// No telemetry, counter, or discovery journal is written to ECU flash.
static HaEnergyMeter haMeter;
static bool haMeterReady=false,haEnergyBootstrap=false,haEnergySeen=false,haEnergyCorrupt=false;
static String haEnergyProducer;
static uint32_t haEnergySequence=0,haEnergyAwaited=0,haEnergySentAt=0;

void haEnergyBegin() {
  haMeter=HaEnergyMeter();haMeterReady=false;
  char producer[17];snprintf(producer,sizeof(producer),"%08lX%08lX",(unsigned long)esp_random(),(unsigned long)esp_random());
  haEnergyProducer=producer;
}
void haEnergyReconnect() {
  haMeterReady=false;haEnergyBootstrap=false;haEnergySeen=false;haEnergyCorrupt=false;haEnergyAwaited=0;
}
void haEnergyCredit(int which,float deltaWh) {
  if(!haEnabled||which<0||which>=inverterCount||haEnergyCorrupt)return;
  if(!haMeter.credit(Inv_Prop[which].invSerial,deltaWh)){haEnergyCorrupt=true;haMeterReady=false;}
}

bool haEnergyPayloadValid(JsonDocument &doc) {
  if(doc["version"]!=1||doc["ecu"]!=ECU_ID||!doc["meters"].is<JsonArray>()||doc["meters"].size()>64||
     !doc["producer"].is<const char *>()||strlen(doc["producer"].as<const char *>())!=16||!doc["total_mwh"].is<uint64_t>()||!doc["sequence"].is<uint32_t>()||!doc["session"].is<const char *>())return false;
  uint64_t total=0;
  for(JsonVariant meter:doc["meters"].as<JsonArray>()) {
    if(!meter["serial"].is<const char *>()||!haHexId(meter["serial"])||!meter["mwh"].is<uint64_t>())return false;
    uint64_t value=meter["mwh"];if(UINT64_MAX-total<value)return false;total+=value;
    int matches=0;for(JsonVariant other:doc["meters"].as<JsonArray>())if(meter["serial"]==other["serial"])++matches;
    if(matches!=1)return false;
  }
  return total==doc["total_mwh"].as<uint64_t>();
}

void haEnergyReceive(uint8_t *payload,size_t length) {
  JsonDocument doc;
  if(length>8192||deserializeJson(doc,payload,length)){haEnergyCorrupt=true;haMeterReady=false;return;}
  if(!doc["probe"].isNull()) {
    if(doc["probe"]==haSession) {
      haEnergyBootstrap=!haEnergyCorrupt;
      haMeterReady=haEnergyBootstrap&&haEnergySeen;
    }
    return;
  }
  if(!haEnergyPayloadValid(doc)){haEnergyCorrupt=true;haMeterReady=false;return;}
  bool acknowledgement=doc["session"]==haSession&&doc["sequence"]==haEnergyAwaited&&haEnergyAwaited!=0;
  if(haEnergyBootstrap&&!acknowledgement)return; // Ignore old echoes after recovery.
  // Merge a recovered checkpoint with deltas already measured in this boot.
  // The checkpoint is never counted as newly generated energy.
  for(JsonVariant meter:doc["meters"].as<JsonArray>()) {
    int slot=haMeter.find(meter["serial"],true);
    if(slot<0){haEnergyCorrupt=true;haMeterReady=false;return;}
    auto &entry=haMeter.entries[slot];uint64_t recovered=meter["mwh"];
    if(recovered<=entry.committed)continue;
    uint64_t uncommitted=entry.pending-entry.committed;
    if(!acknowledgement&&UINT64_MAX-recovered<uncommitted){haEnergyCorrupt=true;haMeterReady=false;return;}
    entry.pending=(acknowledgement||doc["producer"]==haEnergyProducer)?max(entry.pending,recovered):recovered+uncommitted;
    entry.committed=recovered;
  }
  uint64_t aggregate=0;
  for(const auto &entry:haMeter.entries){if(UINT64_MAX-aggregate<entry.committed){haEnergyCorrupt=true;haMeterReady=false;return;}aggregate+=entry.committed;}
  haMeter.aggregate=aggregate;haEnergySeen=true;
  if(acknowledgement){haEnergyAwaited=0;haMeterReady=true;}
}

void haEnergyCommit() {
  if(!haEnabled||!haEnergyBootstrap||haEnergyCorrupt||!haClient.connected())return;
  if(haEnergyAwaited&&(uint32_t)(millis()-haEnergySentAt)<5000)return;
  bool dirty=!haEnergySeen;
  for(const auto &entry:haMeter.entries)dirty|=entry.pending!=entry.committed;
  if(!dirty)return;
  JsonDocument doc;doc["producer"]=haEnergyProducer;doc["version"]=1;doc["ecu"]=ECU_ID;doc["session"]=haSession;
  if(++haEnergySequence==0)++haEnergySequence;
  doc["sequence"]=haEnergySequence;
  JsonArray meters=doc["meters"].to<JsonArray>();uint64_t aggregate=0;
  for(const auto &entry:haMeter.entries)if(entry.serial[0]) {
    if(UINT64_MAX-aggregate<entry.pending){haEnergyCorrupt=true;haMeterReady=false;return;}
    aggregate+=entry.pending;JsonObject meter=meters.add<JsonObject>();meter["serial"]=entry.serial;meter["mwh"]=entry.pending;
  }
  doc["total_mwh"]=aggregate;
  if(haPublishJson(haBase+"/energy",doc,true)) {haEnergyAwaited=haEnergySequence;haEnergySentAt=millis();}
}
