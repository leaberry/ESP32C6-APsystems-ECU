"""Compile actual settings export/restore with ArduinoJson and fault-injected stores."""
from pathlib import Path
import os, subprocess, tempfile
root=Path(__file__).resolve().parents[1]
include=Path(os.environ.get('ARDUINOJSON_INCLUDE',Path.home()/'Arduino/libraries/ArduinoJson/src'))
source=(root/'SETTINGS_BACKUP.ino').read_text()
source=source.replace('#include "SETTINGS_UI.h"','').replace('#include <nvs.h>','')

def function(s, signature):
    start=s.index(signature); end=s.index('{',start)+1; depth=1
    while depth:
        depth+=(s[end]=='{')-(s[end]=='}'); end+=1
    return s[start:end]+'\n'

harness=r'''
#include <ArduinoJson.h>
#include <cassert>
#include <map>
#include <string>
#include <iostream>
#include <algorithm>
#include <cstring>
#include "DEVICE_SETTINGS.h"
#define F(x) x
#define portMUX_INITIALIZER_UNLOCKED 0
using portMUX_TYPE=int;
using std::min;
struct String:std::string {
 using std::string::string;
 String(const std::string &s):std::string(s){}
 String(int n):std::string(std::to_string(n)){}
 bool isEmpty() const{return empty();}
 void trim(){auto a=find_first_not_of(" \r\n\t");*this=a==npos?"":substr(a,find_last_not_of(" \r\n\t")-a+1);}
 void toLowerCase(){for(char &c:*this)if(c>='A'&&c<='Z')c+=32;}
 bool endsWith(const char *s)const{return !empty()&&back()==s[0];}
 void remove(size_t pos){erase(pos);}
 size_t write(uint8_t c){push_back(c);return 1;}
 size_t write(const uint8_t *p,size_t n){append((const char*)p,n);return n;}
};
namespace ArduinoJson {
 template<> struct Converter<String> {
  static void toJson(const String &s,JsonVariant v){v.set(std::string(s));}
  static String fromJson(JsonVariantConst v){return v.as<std::string>();}
  static bool checkJson(JsonVariantConst v){return v.is<const char*>();}
 };
}
struct {void println(const char*){}} Serial;
std::map<std::string,std::string> files;
std::map<std::string,std::map<std::string,std::string>> nvs;
int mutations=0,failAt=0;
bool fault(){return ++mutations==failAt;}
size_t freeBytes=1000000;
struct File {
 std::string name;size_t pos=0;
 operator bool()const{return !name.empty();}
 size_t size()const{return name.empty()?0:files.at(name).size();}
 int read(){return pos<size()?(uint8_t)files[name][pos++]:-1;}
 size_t read(uint8_t *out,size_t n){if(!*this)return 0;size_t take=min(n,size()-pos);memcpy(out,files[name].data()+pos,take);pos+=take;return take;}
 size_t readBytes(char *out,size_t n){return read((uint8_t*)out,n);}
 size_t write(const uint8_t *p,size_t n){if(!*this||fault())return 0;files[name].append((const char*)p,n);return n;}
 size_t write(uint8_t c){return write(&c,1);}
 void close(){} void flush(){}
};
struct {
 bool exists(const String &p){return files.count(p);}
 File open(const String &p,const char *mode){
  if(mode[0]=='w'){if(fault())return {};files[p].clear();}
  if(!files.count(p))return {};
  File f;f.name=p;return f;
 }
 bool remove(const String &p){if(fault())return false;return files.erase(p);}
 bool rename(const String &a,const String &b){if(fault()||!files.count(a)||files.count(b))return false;files[b]=files[a];files.erase(a);return true;}
 size_t totalBytes(){return 1000000;}size_t usedBytes(){return 1000000-freeBytes;}
} SPIFFS;
struct Preferences {
 std::string ns;
 bool begin(const char *s,bool ro){if(!ro&&fault())return false;if(ro&&!nvs.count(s))return false;ns=s;nvs[ns];return true;}
 void end(){}
 bool isKey(const char *k){return !ns.empty()&&nvs[ns].count(k);}
 String getString(const char *k,const String &fallback){return isKey(k)?String(nvs[ns][k]):fallback;}
 size_t putString(const char *k,const String &s){if(fault())return 0;nvs[ns][k]=s;return s.length();}
 bool getBool(const char *k,bool fallback){return isKey(k)?nvs[ns][k]=="1":fallback;}
 size_t putBool(const char *k,bool v){return putString(k,v?"1":"0");}
 int getInt(const char *k,int fallback){return isKey(k)?std::stoi(nvs[ns][k]):fallback;}
 size_t putInt(const char *k,int v){if(fault())return 0;nvs[ns][k]=std::to_string(v);return 4;}
 uint32_t getUInt(const char *k,uint32_t fallback){return isKey(k)?std::stoul(nvs[ns][k]):fallback;}
 size_t putUInt(const char *k,uint32_t v){if(fault())return 0;nvs[ns][k]=std::to_string(v);return 4;}
 bool clear(){if(fault())return false;nvs[ns].clear();return true;}
 bool remove(const char *k){if(fault())return false;return nvs[ns].erase(k);}
};
using esp_err_t=int;
const int ESP_OK=0,ESP_ERR_NVS_NOT_FOUND=1,NVS_TYPE_ANY=0,NVS_TYPE_U32=4,NVS_TYPE_STR=5,NVS_TYPE_U8=6,NVS_TYPE_I32=7,NVS_READONLY=1;
const int ESP_MAC_WIFI_STA=1,ESP_MAC_IEEE802154=2;
using nvs_handle_t=int;
std::string openNamespace;
int nvs_open(const char *s,int,nvs_handle_t *h){openNamespace=s;*h=1;return nvs.count(s)?ESP_OK:ESP_ERR_NVS_NOT_FOUND;}
int nvs_get_str(nvs_handle_t,const char *k,char *buf,size_t *size){
 auto &ns=nvs[openNamespace];if(!ns.count(k))return ESP_ERR_NVS_NOT_FOUND;
 auto v=ns[k];if(buf){if(*size<v.size()+1)return 2;memcpy(buf,v.c_str(),v.size()+1);}*size=v.size()+1;return ESP_OK;
}
void nvs_close(nvs_handle_t){}
int nvs_get_u8(nvs_handle_t,const char *k,uint8_t *v){*v=std::stoi(nvs[openNamespace][k]);return ESP_OK;}
int nvs_get_i32(nvs_handle_t,const char *k,int32_t *v){*v=std::stoi(nvs[openNamespace][k]);return ESP_OK;}
int esp_read_mac(uint8_t *out,int kind){for(int i=0;i<(kind==1?6:8);++i)out[i]=i+1;return ESP_OK;}
struct Iterator {std::map<std::string,std::string>::iterator current;std::string ns;};
using nvs_iterator_t=Iterator*;
struct nvs_entry_info_t {char key[16];int type;};
int nvs_entry_find(const char*,const char *ns,int,nvs_iterator_t *it){
 if(nvs[ns].empty())return ESP_ERR_NVS_NOT_FOUND;
 *it=new Iterator{nvs[ns].begin(),ns};return ESP_OK;
}
void nvs_entry_info(nvs_iterator_t it,nvs_entry_info_t *info){snprintf(info->key,16,"%s",it->current->first.c_str());info->type=it->ns=="apsradio"?NVS_TYPE_U32:it->ns=="my_data"?NVS_TYPE_I32:it->current->first=="dhcp"?NVS_TYPE_U8:NVS_TYPE_STR;}
int nvs_entry_next(nvs_iterator_t *it){return ++(*it)->current==nvs[(*it)->ns].end()?ESP_ERR_NVS_NOT_FOUND:ESP_OK;}
void nvs_release_iterator(nvs_iterator_t it){delete it;}
struct IPAddress {bool fromString(const char *s){unsigned a,b,c,d;char extra;return sscanf(s,"%u.%u.%u.%u%c",&a,&b,&c,&d,&extra)==4&&a<256&&b<256&&c<256&&d<256;}};
int knop=0,led_onb=2;
size_t strlcpy(char *out,const char *in,size_t n){snprintf(out,n,"%s",in);return strlen(in);}
bool ecuTimeZoneIsValid(const char *s){return !strcmp(s,"UTC");}
bool apsSerialDefaultsToEncrypted(const char *s){return s[1]=='2';}
String defaultWifiHostname(){return "aps-ecu-test";}
'''
includes=(root/'AAA_INCLUDES.h').read_text();end=includes.index('} inverters;')+len('} inverters;');start=includes.rfind('typedef struct',0,end)
harness+=includes[start:end]+'\ninverters Inv_Prop[9];\n'
portal=(root/'PORTAL_WIFI.ino').read_text()
harness+=portal[portal.index('constexpr const char *WIFI_PREFS_NAMESPACE'):portal.index('constexpr uint32_t PORTAL_REBOOT_DELAY_MS')]
for name in ['String normalizeWifiHostname(', 'void loadStoredWifiCredentials(', 'void loadStoredWifiAddressing(', 'bool wifiHostnameInputValid(', 'bool wifiSaveString(', 'bool saveStoredWifiConfiguration(']:harness+=function(portal,name)
antenna=(root/'ANTENNA.ino').read_text()
for name in ['bool antennaLoad(', 'bool antennaSave(']:harness+=function(antenna,name)
harness+=r'''
JsonDocument sample;
void basisConfigDocument(JsonDocument &doc){doc.set(sample["payload"]["basis"]);}
void wifiConfigDocument(JsonDocument &doc){doc.set(sample["payload"]["timeSecurity"]);}
void mqttConfigDocument(JsonDocument &doc){doc.set(sample["payload"]["mqtt"]);}
'''
harness+=r'''
#define PROGMEM
#define FPSTR(x) x
#define HTTP_GET 0
#define HTTP_POST 1
#define portENTER_CRITICAL(x) ((void)(x))
#define portEXIT_CRITICAL(x) ((void)(x))
#include "SETTINGS_UI.h"
bool denied=false;
int actionFlag=0;
char pswd[33]="0000",ECU_ID[13]="123456789ABC";
bool checkRemote(const String &){return denied;}
String ecuPageStart(const String &,const String &){return "page";}
String ecuPageEnd(){return "end";}
struct AsyncWebServerResponse {
 String text;std::map<std::string,String> headers;
 void addHeader(const char *key,const String &value){headers[key]=value;}
};
struct AsyncResponseStream:AsyncWebServerResponse {
 size_t write(uint8_t c){text.push_back(c);return 1;}
 size_t write(const uint8_t *p,size_t n){text.append((const char*)p,n);return n;}
};
struct Header {String text;String value(){return text;}};
struct AsyncWebServerRequest {
 void *_tempObject=nullptr;bool authenticated=true;int status=0;
 String message,type="application/json";std::map<std::string,Header> headers;
 AsyncResponseStream response;
 struct Client {struct IP {String toString(){return "192.168.1.10";}};IP remoteIP(){return {};}} peer;
 ~AsyncWebServerRequest(){free(_tempObject);}
 Client *client(){return &peer;}
 bool authenticate(const char*,const char*){return authenticated;}
 void requestAuthentication(){status=401;}
 bool hasHeader(const char *name){return headers.count(name);}
 Header *getHeader(const char *name){return &headers[name];}
 String contentType(){return type;}
 void send(int code,const char*,const String &text){status=code;message=text;}
 void send(AsyncWebServerResponse *r){status=200;message=r->text;}
 AsyncResponseStream *beginResponseStream(const char*){return &response;}
 AsyncWebServerResponse *beginResponse(int,const char*,const String &text){response.text=text;return &response;}
};
struct {template<class... T>void on(T...){}} server;
'''
harness+=source
harness+=r'''
const char *fixture=R"JSON({"payload":{
 "sourceBoard":"010203040506","radioIEEE":"0807060504030201",
 "basis":{"ECU_ID":"123456789ABC","fleetName":"Test fleet","userPwd":"1111","schemaVersion":3,"inverterCount":1,"pollOffset":0,"pollIntervalSeconds":300,"Polling":true,"sunspecEnabled":true,"flightRecorderEnabled":false},
 "timeSecurity":{"pswd":"0000","gmtOffset":"0","timeZoneId":"UTC","ntpServer":"pool.ntp.org","lati":40.12,"longi":-105.5,"securityLevel":6,"zomerTijd":false,"daylightPolling":true,"locationConfigured":true},
 "mqtt":{"Mqtt_Broker":"broker","Mqtt_Port":"1883","Mqtt_outTopic":"domoticz/in","Mqtt_Username":"mqtt-user","Mqtt_Password":"secret","Mqtt_Format":1,"Mqtt_stateIDX":123},
 "network":{"ssid":"wifi","password":"wifi-secret","hostname":"ecu-test","dhcp":true,"ip":"","netmask":"255.255.255.0","gateway":""},
 "antenna":{"mode":0,"board":0,"selectPin":14,"enablePin":3,"externalHigh":true,"enableHigh":false},
 "inverters":[{"serial":"123456789012","id":"1234","name":"Roof","type":2,"mqttIdx":123,"calibration":0,"powerLimit":600,"panels":[true,false,true,true]}],
 "peers":[{"serial":"123456789012","pan":13330,"source":13330},{"serial":"999999999999","pan":23,"source":42}]
}})JSON";
void init(){
 files.clear();nvs.clear();failAt=mutations=0;settingsStageBusy=false;actionFlag=0;denied=false;freeBytes=1000000;settingsBootOk=false;
 assert(!deserializeJson(sample,fixture));settingsSeal(sample);assert(settingsValidate(sample));
 files["/energy-days.bin"]="PRODUCTION-HISTORY";files["/energy-today.bin"]="CHECKPOINT";
 files["/basisconfig.json"]="old configuration";files["/Inv_Prop8.str"]="old inverter";
 files["/Inv_Prop8.str.pair-old"]="old pairing";nvs["apsradio"]["888888888888"]="1234";
 nvs["my_data"]["maxPwr8"]="42";
}
void unchangedHistory(){assert(files["/energy-days.bin"]=="PRODUCTION-HISTORY"&&files["/energy-today.bin"]=="CHECKPOINT");}
void restored(){
 assert(settingsBootReady()&&!files.count(SETTINGS_PENDING));unchangedHistory();
 assert(!files.count("/Inv_Prop8.str")&&!files.count("/Inv_Prop8.str.pair-old"));
 assert(!nvs["my_data"].count("maxPwr8")&&nvs["my_data"]["maxPwr0"]=="600");
 assert(nvs["apsradio"].size()==2&&!nvs["apsradio"].count("888888888888"));
 assert(nvs["aps-identity"]["radioIEEE"]=="0807060504030201");
 assert(nvs["aps-wifi"]["hostname"]=="ecu-test");
 inverters inv;auto bytes=files["/Inv_Prop0.str"];assert(bytes.size()==sizeof(inv));memcpy(&inv,bytes.data(),sizeof(inv));
 assert(!strcmp(inv.invSerial,"123456789012")&&!strcmp(inv.invID,"1234")&&inv.conPanels[0]&&!inv.conPanels[1]);
 uint8_t address[8];assert(settingsRadioAddress(address));for(int i=0;i<8;++i)assert(address[i]==8-i);
 assert(settingsHasRestoredIdentity());
}
int main(){
 init();uint8_t address[8];assert(settingsRadioAddress(address)&&!settingsHasRestoredIdentity());
 assert(settingsStageRestore(sample));assert(files["/basisconfig.json"]=="old configuration");
 assert(settingsRestoreBoot());restored();
 JsonDocument exported;assert(settingsBuildBackup(exported));assert(settingsValidate(exported));
 assert(exported["payload"]["peers"].size()==2&&exported["payload"]["inverters"][0]["powerLimit"]==600);
 std::string encoded;serializeJson(exported,encoded);JsonDocument roundtrip;assert(!deserializeJson(roundtrip,encoded));assert(settingsValidate(roundtrip));
 // Refuse incomplete inverter records and unreadable NVS values on export.
 auto inverterFile=files["/Inv_Prop0.str"];files["/Inv_Prop0.str"]="short";
 JsonDocument rejected;assert(!settingsBuildBackup(rejected));files["/Inv_Prop0.str"]=inverterFile;
 nvs["aps-wifi"]["password"]=std::string(600,'x');assert(!settingsBuildBackup(rejected));
 nvs["aps-wifi"]["password"]="wifi-secret";
 // Every persistent mutation can fail: preserve the complete manifest, then replay.
 init();assert(settingsStageRestore(sample));mutations=0;assert(settingsRestoreBoot());int operations=mutations;
 for(int failure=1;failure<=operations;++failure){
  init();assert(settingsStageRestore(sample));mutations=0;failAt=failure;
  assert(!settingsRestoreBoot()&&!settingsBootReady()&&files.count(SETTINGS_PENDING));unchangedHistory();
  failAt=0;assert(settingsRestoreBoot());restored();
 }
 init();assert(settingsStageRestore(sample));int staging=mutations;
 for(int failure=1;failure<=staging;++failure){
  init();failAt=failure;assert(!settingsStageRestore(sample));assert(!files.count(SETTINGS_PENDING));
  assert(files["/basisconfig.json"]=="old configuration");unchangedHistory();
 }
 init();freeBytes=0;assert(!settingsStageRestore(sample));assert(!files.count(SETTINGS_PENDING));
 init();files[SETTINGS_PENDING]="truncated";assert(!settingsRestoreBoot()&&!settingsBootReady());unchangedHistory();
 init();sample["payload"]["basis"]["ECU_ID"]="ABCDEF123456";assert(!settingsValidate(sample));
 settingsSeal(sample);assert(settingsValidate(sample));
 for(int bad=0;bad<12;++bad){
  init();JsonVariant p=sample["payload"];
  if(bad==0)sample["version"]=2;
  if(bad==1)p["inverters"][0]["serial"]="../bad";
  if(bad==2)p["antenna"]["mode"]=2,p["antenna"]["board"]=1,p["antenna"]["selectPin"]=12;
  if(bad==3)p["network"]["hostname"]=std::string(32,'a');
  if(bad==4)p["network"]["dhcp"]=false,p["network"]["ip"]="999.1.2.3";
  if(bad==5)p["inverters"][0]["panels"][0]=2;
  if(bad==6)p["inverters"].as<JsonArray>().add(p["inverters"][0]),p["basis"]["inverterCount"]=2;
  if(bad==7)p["peers"].as<JsonArray>().add(p["peers"][0]);
  if(bad==8)p["radioIEEE"]="0000000000000000";
  if(bad==9)p["timeSecurity"]["ntpServer"]="http://invalid";
  if(bad==10)p["timeSecurity"]["timeZoneId"]="Unknown";
  if(bad==11)p["basis"]["inverterCount"]=2;
  char crc[9];settingsChecksum(p,crc);sample["crc32"]=crc;
  assert(!settingsValidate(sample));
 }
 init();assert(settingsStageRestore(sample));assert(settingsRestoreBoot());nvs["aps-identity"]["radioIEEE"]="bad";
 assert(!settingsRadioAddress(address));

 // HTTP uploads are request-local, bounded, authenticated, and require JSON headers.
 for(int mode=0;mode<5;++mode){
  init();AsyncWebServerRequest r;std::string text;serializeJson(sample,text);
  r.headers["X-ECU-Settings"].text="1";
  if(mode==0)r.authenticated=false;
  if(mode==1)r.type="text/plain";
  if(mode==2)r.headers.clear();
  if(mode==3)denied=true;
  size_t total=mode==4?SETTINGS_MAX_BYTES+1:text.size();
  settingsBody(&r,(uint8_t*)text.data(),text.size(),0,total);
  assert(!r._tempObject);settingsReceive(&r,true);
  assert(r.status==400||r.status==401||r.status==403);assert(!files.count(SETTINGS_PENDING));
 }
 init();{
  AsyncWebServerRequest r;r.headers["X-ECU-Settings"].text="1";std::string text;serializeJson(sample,text);
  settingsBody(&r,(uint8_t*)text.data(),8,0,text.size());settingsReceive(&r,true);
  assert(r.status==400&&!files.count(SETTINGS_PENDING));
 }
 for(bool network:{false,true})for(bool antenna:{false,true}){
  init();AsyncWebServerRequest r;
  sample["payload"]["antenna"]["mode"]=2;settingsSeal(sample);
  r.headers["X-ECU-Settings"].text="1";
  std::string text;serializeJson(sample,text);
  settingsBody(&r,(uint8_t*)text.data(),10,0,text.size());
  settingsBody(&r,(uint8_t*)text.data()+10,text.size()-10,10,text.size());
  settingsReceive(&r,false);assert(r.status==200&&!files.count(SETTINGS_PENDING));
  assert(r.message.find("secret")==std::string::npos&&r.response.headers["Cache-Control"]=="no-store");
  settingsReceive(&r,true);assert(r.status==400&&!files.count(SETTINGS_PENDING));
  r.headers["X-ECU-Restore"].text="confirmed";
  if(network)r.headers["X-ECU-Network"].text="restore";
  if(antenna)r.headers["X-ECU-Antenna"].text="restore";
  settingsReceive(&r,true);assert(r.status==200&&actionFlag==10&&files.count(SETTINGS_PENDING));
  JsonDocument staged;assert(!deserializeJson(staged,files[SETTINGS_PENDING]));
  assert(staged["payload"]["network"]["ssid"]==(network?"wifi":""));
  assert(staged["payload"]["antenna"]["mode"]==(antenna?2:0));
  assert(files["/basisconfig.json"]=="old configuration");unchangedHistory();
 }
 std::cout<<"PASS settings format, roundtrip, radio identity, export, history isolation and "<<operations<<" interrupted-restore operations\n";
}
'''
with tempfile.TemporaryDirectory() as directory:
    cpp,binary=Path(directory)/'test.cpp',Path(directory)/'test'
    cpp.write_text(harness)
    subprocess.run([os.environ.get('CXX','g++'),'-std=c++17','-Wall','-Wextra','-I',str(include),'-I',str(root),str(cpp),'-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True)
