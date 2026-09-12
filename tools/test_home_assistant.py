"""Compile the real HA MQTT modules with an in-memory retained broker (no flash API)."""
from pathlib import Path
import os, subprocess, tempfile
root=Path(__file__).resolve().parents[1]
include=os.environ.get('ARDUINOJSON_INCLUDE',str(Path.home()/'Arduino/libraries/ArduinoJson/src'))
harness=r'''
#include <ArduinoJson.h>
#include <cassert>
#include <string>
#include <vector>
#include <map>
#include <deque>
#include <algorithm>
#include <iostream>
#include <cstring>
#include <cmath>
#include "HA_MODEL.h"
using std::max;
struct String:std::string {
 using std::string::string;
 String(const std::string &s):std::string(s){}
 String(int n):std::string(std::to_string(n)){}
 int indexOf(const String &s)const{auto n=find(s);return n==npos?-1:(int)n;}
 String substring(size_t a,size_t b=std::string::npos)const{return substr(a,b==npos?npos:b-a);}
 bool endsWith(const char *s)const{size_t n=strlen(s);return size()>=n&&compare(size()-n,n,s)==0;}
};
namespace ArduinoJson {
 template<> struct Converter<String> {
  static void toJson(const String &s,JsonVariant v){v.set(std::string(s));}
  static String fromJson(JsonVariantConst v){return v.as<std::string>();}
  static bool checkJson(JsonVariantConst v){return v.is<const char*>();}
 };
}
uint32_t tick=1000,randomValue=0;
uint32_t millis(){return tick;}
uint32_t esp_random(){return ++randomValue;}
size_t strlcpy(char *d,const char *s,size_t n){snprintf(d,n,"%s",s);return strlen(s);}
struct WiFiClient{};
struct IPAddress{String toString(){return "192.0.2.1";}};
const int WL_CONNECTED=3;
struct {int status(){return WL_CONNECTED;} int RSSI(){return -50;} IPAddress localIP(){return {};}}WiFi;
struct Message{std::string topic,payload;bool retained;};
struct PubSubClient {
 bool up=false,dropEcho=false,failPublish=false;std::string current,data;bool retain;
 std::map<std::string,std::string> retained;
 std::vector<Message> published;std::deque<Message> incoming;
 std::vector<std::string> subscriptions;
 void (*callback)(char*,uint8_t*,unsigned int)=nullptr;
 PubSubClient(WiFiClient&){}
 bool connected(){return up;}
 void disconnect(){up=false;incoming.clear();subscriptions.clear();}
 void setCallback(decltype(callback) f){callback=f;}
 void setKeepAlive(int){} void setSocketTimeout(int){} bool setBufferSize(int){return true;}
 void setServer(const char*,int){}
 bool connect(const char*,const char*,const char*,const char*,int,bool,const char*){up=true;return true;}
 bool subscribe(const char *t){subscriptions.push_back(t);if(retained.count(t))incoming.push_back({t,retained[t],true});return true;}
 bool publish(const char *t,const char *p,bool r=false){
  if(!up||failPublish)return false;
  published.push_back({t,p,r});if(r){if(*p)retained[t]=p;else retained.erase(t);}
  if(!dropEcho&&std::find(subscriptions.begin(),subscriptions.end(),t)!=subscriptions.end())incoming.push_back({t,p,r});
  return true;
 }
 bool beginPublish(const char *t,size_t,bool r){current=t;data.clear();retain=r;return up&&!failPublish;}
 size_t write(const uint8_t *p,size_t n){data.append((const char*)p,n);return n;}
 bool endPublish(){return publish(current.c_str(),data.c_str(),retain);}
 void loop(){if(!incoming.empty()){auto m=incoming.front();incoming.pop_front();callback(m.topic.data(),(uint8_t*)m.payload.data(),m.payload.size());}}
};
bool haEnabled=true,haConfigured=true,timeRetrieved=true;
char ECU_ID[13]="123456789ABC",haDiscoveryPrefix[49]="homeassistant",fleetName[33]="Solar";
char Mqtt_Broker[65]="broker",Mqtt_Port[8]="1883",Mqtt_Username[33]="",Mqtt_Password[33]="";
const char *VERSION="test";
int inverterCount=2,actionFlag=0,desiredThrottle[9];unsigned long pollIntervalSeconds=30;
struct {char invSerial[13];char invLocation[33];int invType;bool conPanels[4];}Inv_Prop[9];
struct {float pw_total,heath,acv,freq,power[4],dcv[4],dcc[4];bool radioMetricsValid;int radioRssi,radioLqi;}Inv_Data[9];
bool polled[9],night=false,applySuccess=true;int commands=0;
bool pollingNightModeActive(){return night;}
uint64_t energyTodayWhFor(int){return 42;}
float round1(float v){return roundf(v*10)/10;}
bool setMaxPower(int){++commands;return applySuccess;}
void haRegistryReceive(uint8_t*,size_t);
'''
harness+=(root/'HA_ENERGY.ino').read_text(encoding='utf-8')
harness+=(root/'HOME_ASSISTANT.ino').read_text(encoding='utf-8')
harness+=r'''
void drain(){while(!haClient.incoming.empty())haClient.loop();}
void probe(){JsonDocument d;d["probe"]=haSession;haPublishJson(haBase+"/energy",d,false);drain();}
void energyReconnect(){haEnergyReconnect();haClient.incoming.clear();haClient.subscriptions.clear();haSession=String(++randomValue);haClient.subscribe((haBase+"/energy").c_str());probe();}
void ack(){haEnergyCommit();drain();}
void command(const std::string &body){String t=haBase+"/inverter/"+Inv_Prop[0].invSerial+"/limit/set";haReceive(t.data(),(uint8_t*)body.data(),body.size());}
int main(){
 strcpy(Inv_Prop[0].invSerial,"408000000001");strcpy(Inv_Prop[1].invSerial,"408000000002");
 for(int i=0;i<2;++i){strcpy(Inv_Prop[i].invLocation,"Roof");Inv_Prop[i].conPanels[0]=true;polled[i]=true;desiredThrottle[i]=-1;}
 haBegin();haClient.up=true;haSession="first";energyReconnect();assert(!haMeterReady);ack();assert(haMeterReady&&haMeter.aggregate==0);
 haEnergyCredit(0,1.25);haEnergyCredit(1,2.5);assert(haMeter.aggregate==0);ack();assert(haMeter.aggregate==3750);
 // Broker accepted a checkpoint but its echo was lost. Same RAM must not double count.
 haEnergyCredit(0,1);haClient.dropEcho=true;haEnergyCommit();assert(haMeter.aggregate==3750);
 haClient.dropEcho=false;energyReconnect();assert(haMeterReady&&haMeter.aggregate==4750);assert(haMeter.entries[0].pending==2250);ack();assert(haMeter.aggregate==4750);
 // Fresh boot also measured an interval before retained recovery.
 haEnergyBegin();haEnergyCredit(0,0.5);energyReconnect();assert(haMeter.aggregate==4750);assert(haMeter.entries[0].pending==2750);ack();assert(haMeter.aggregate==5250);
 // Reconnect while broker did not accept the new interval.
 haEnergyCredit(0,1);haClient.failPublish=true;haEnergyCommit();haClient.failPublish=false;energyReconnect();ack();assert(haMeter.aggregate==6250);
 // Removed inverters remain in aggregate; midnight/raw resets never decrement it.
 inverterCount=1;haEnergyCredit(0,-100);haEnergyCredit(0,NAN);ack();assert(haMeter.aggregate==6250);
 JsonDocument state;haStateDocument(state,-1);assert(state["energy_kwh"].as<double>()==0.00625);
 haEnergyReconnect();haStateDocument(state,-1);assert(state["energy_kwh"]=="None"&&!state["energy_valid"].as<bool>());
 // Malformed retained data fails closed, without zeroing or overwriting the broker.
 std::string bad="{}";haEnergyReceive((uint8_t*)bad.data(),bad.size());probe();auto n=haClient.published.size();ack();assert(!haMeterReady&&haClient.published.size()==n);
 energyReconnect();assert(haMeterReady);
 JsonDocument discovery;haDiscoveryDocument(discovery,0);
 auto c=discovery["cmps"];
 assert(c["energy_kwh"]["dev_cla"]=="energy"&&c["energy_kwh"]["stat_cla"]=="total_increasing"&&c["energy_kwh"]["unit_of_meas"]=="kWh");
 assert(c["today_kwh"]["stat_cla"].isNull());
 assert(c["power_limit"]["opt"]==false&&c["power_limit"]["retain"]==false);
 assert(c["panel4_power_w"].size()==1&&c["panel4_power_w"]["p"]=="sensor");
 assert(measureJson(discovery)<16384);auto id=c["energy_kwh"]["uniq_id"].as<std::string>();
 std::swap(Inv_Prop[0],Inv_Prop[1]);haDiscoveryDocument(discovery,1);assert(discovery["cmps"]["energy_kwh"]["uniq_id"]==id);std::swap(Inv_Prop[0],Inv_Prop[1]);
 haTelemetry(0);command("{\"value\":100,\"session\":\"old\"}");assert(haControl<0);
 auto valid=std::string("{\"value\":100,\"session\":\"")+haSession+"\"}";
 command(valid);assert(haControl==0);haConnectedBroker=haBrokerKey();haLoop();assert(commands==1&&desiredThrottle[0]==100&&haControlStatus[0]=="applied");
 applySuccess=false;command(valid);haLoop();assert(commands==2&&desiredThrottle[0]==-1);
 tick+=100000;command(valid);assert(haControl<0&&commands==2);
 assert(haOwnDiscoveryTopic(haDiscoveryTopic(-1))&&!haOwnDiscoveryTopic("homeassistant/device/some_other_ecu/config"));
 // Inventory checkpoint must be acknowledged before emitting a new discovery topic.
 haInventoryBootstrap=true;haInventoryCorrupt=false;haInventoryAwaited=0;haDiscoveryCursor=-1;haRegistry.clear();haRegistry["topics"].to<JsonArray>();
 haClient.subscribe((haBase+"/inventory").c_str());assert(!haDiscoverOne());assert(haInventoryAwaited);drain();assert(!haInventoryAwaited);haDiscoverOne();assert(haClient.retained.count(haDiscoveryTopic(-1)));
 // Disable removes only our registered discovery and never the retained energy.
 haEnabled=false;while(!haDiscoverOne())drain();assert(!haClient.retained.count(haDiscoveryTopic(-1)));assert(haClient.retained.count(haBase+"/energy"));
 assert(haPrefixValid("homeassistant")&&haPrefixValid("test/ha")&&!haPrefixValid("bad/+")&&!haPrefixValid("bad//ha"));
 std::cout<<"HA retained recovery, lost echo, reboot, failures, discovery, cleanup and control tests passed\n";
}
'''
with tempfile.TemporaryDirectory() as tmp:
 p=Path(tmp);(p/'test.cpp').write_text(harness,encoding='utf-8')
 subprocess.run(['g++','-std=c++17','-I'+str(root),'-I'+include,str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
