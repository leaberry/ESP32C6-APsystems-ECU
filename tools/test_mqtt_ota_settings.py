"""Exercise actual OTA and combined MQTT handlers, including rejected writes."""
from pathlib import Path
import os, subprocess, tempfile
root = Path(__file__).resolve().parents[1]
include = os.environ.get('ARDUINOJSON_INCLUDE', str(Path.home()/'Arduino/libraries/ArduinoJson/src'))
harness = r'''
#include <ArduinoJson.h>
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <map>
#include <string>
#include <vector>
#include <iostream>
struct String:std::string {
 using std::string::string;
 String(const std::string&s):std::string(s){}
 bool isEmpty()const{return empty();}
 int toInt()const{return atoi(c_str());}
 void trim(){auto a=find_first_not_of(" \r\n\t");*this=a==npos?"":substr(a,find_last_not_of(" \r\n\t")-a+1);}
};
namespace ArduinoJson {template<> struct Converter<String> {
 static void toJson(const String&s,JsonVariant v){v.set(std::string(s));}
 static String fromJson(JsonVariantConst v){return v.as<std::string>();}
 static bool checkJson(JsonVariantConst v){return v.is<const char*>();}
};}
size_t strlcpy(char*d,const char*s,size_t n){if(n){strncpy(d,s,n);d[n-1]=0;}return strlen(s);}
struct Param {String text;String value(){return text;}};
struct IP {String toString(){return "local";}};
struct Client {IP remoteIP(){return {};}};
struct AsyncWebServerRequest {
 void *_tempObject=nullptr; bool authorized=true; int status=0; String message;
 std::map<std::string,Param> params; std::function<void()> disconnect;
 ~AsyncWebServerRequest(){if(disconnect)disconnect();free(_tempObject);}
 bool authenticate(const char*,const char*){return authorized;}
 bool hasParam(const char*n,bool=false){return params.count(n);}
 Param*getParam(const char*n,bool=false){return &params.at(n);}
 Client*client(){static Client c;return &c;}
 void onDisconnect(std::function<void()> f){disconnect=f;}
 void send(int n,const char*,const char*s){status=n;message=s;}
};
bool remoteDenied=false;
bool checkRemote(String){return remoteDenied;}
const char*pswd="";
bool settingsAuthorized(AsyncWebServerRequest*r){if(!r->authorized||remoteDenied){r->status=403;return false;}return true;}
bool otaAvailable=true, saveOk=true; int saves=0;
const void*esp_ota_get_next_update_partition(void*){return otaAvailable?&otaAvailable:nullptr;}
constexpr int YC600_MAX_NUMBER_OF_INVERTERS=4;
uint32_t energyTodayWh[4]={1};
bool energySaveTodayCheckpoint(String &s){++saves;s="Checkpoint failed";return saveOk;}
struct {size_t getFreeSketchSpace(){return 4000000;}} ESP;
struct Writer {
 bool running=false,beginOk=true,writeOk=true,endOk=true; int begins=0,writes=0,ends=0;
 bool begin(size_t){++begins;return running=beginOk;}
 bool isRunning(){return running;}
 void abort(){running=false;}
 size_t write(uint8_t*,size_t n){++writes;return writeOk?n:0;}
 bool end(bool){++ends;running=false;return endOk;}
} Update;
JsonDocument current, saved;
bool configSaveOk=true,haConfigured=false;int actionFlag=0,configWrites=0;
void mqttConfigDocument(JsonDocument&d){d.set(current);}
bool settingsWriteJson(const char*,JsonVariantConst d){++configWrites;if(!configSaveOk)return false;saved.set(d);return true;}
bool haPrefixValid(const char*s){return *s && strlen(s)<=48 && !strpbrk(s,"+# ");}
'''
ota=(root/'OTA_UPLOAD.ino').read_text(encoding='utf-8-sig')
mqtt=(root/'MQTT_CONFIG.ino').read_text(encoding='utf-8')
mqtt=mqtt[mqtt.index('void mqttConfigSaveCombined'):]
tests=r'''
void upload(AsyncWebServerRequest&r,bool final=true,size_t index=0){uint8_t data[10]={};otaUploadChunk(&r,"firmware.bin",index,data,10,final);}
void reset(){Update=Writer{};saves=0;saveOk=true;otaAvailable=true;energyTodayWh[0]=1;remoteDenied=false;}
int main(){
 {AsyncWebServerRequest r;reset();upload(r);assert(Update.ends==0);otaUploadComplete(&r);assert(Update.ends==1);assert(r.status==200&&saves==1&&Update.begins==1);}
 {AsyncWebServerRequest r;reset();saveOk=false;upload(r);upload(r,true,10);otaUploadComplete(&r);assert(r.status==500&&saves==1&&Update.begins==0&&Update.writes==0);}
 {AsyncWebServerRequest r;reset();r.params["saveToday"]={"0"};saveOk=false;upload(r);otaUploadComplete(&r);assert(r.status==200&&saves==0);}
 {AsyncWebServerRequest r;reset();energyTodayWh[0]=0;upload(r);otaUploadComplete(&r);assert(r.status==200&&saves==0);}
 {AsyncWebServerRequest r;reset();r.authorized=false;upload(r);otaUploadComplete(&r);assert(r.status==403&&saves==0&&Update.begins==0);}
 {AsyncWebServerRequest r;reset();remoteDenied=true;upload(r);otaUploadComplete(&r);assert(r.status==403&&Update.begins==0);}
 {AsyncWebServerRequest r;reset();otaAvailable=false;upload(r);otaUploadComplete(&r);assert(r.status==409&&saves==0&&Update.begins==0);}
 {AsyncWebServerRequest r;reset();otaUploadComplete(&r);assert(r.status==400);}
 for(int fault=0;fault<3;++fault){AsyncWebServerRequest r;reset();if(fault==0)Update.beginOk=false;if(fault==1)Update.writeOk=false;if(fault==2)Update.endOk=false;upload(r);otaUploadComplete(&r);assert(r.status==500);}
 {AsyncWebServerRequest r;reset();upload(r,false);otaUploadComplete(&r);assert(r.status==500);AsyncWebServerRequest other;upload(other);otaUploadComplete(&other);assert(other.status==409&&saves==1&&Update.begins==1);}
 assert(!ecuOtaOwner&&!Update.running);
 {AsyncWebServerRequest r;reset();upload(r);upload(r);otaUploadComplete(&r);assert(r.status==400&&Update.ends==0);}
 current["Mqtt_savedFormat"]=3;current["Mqtt_Format"]=3;current["Mqtt_outTopic"]="legacy";
 current["Mqtt_Password"]="secret";current["haDiscoveryPrefix"]="custom";
 for(int modes=0;modes<4;++modes){
  AsyncWebServerRequest r;r.params={{"mqtAdres",{"broker"}},{"mqtPort",{"1883"}},{"mqtUser",{"user"}},{"mqtPas",{""}}};
  if(modes&1){r.params["haEnabled"]={"on"};r.params["prefix"]={"homeassistant"};}
  if(modes&2){r.params["legacyEnabled"]={"on"};r.params["fm"]={"5"};r.params["mqidx"]={"123"};r.params["mqtoutTopic"]={"topic"};}
  actionFlag=0;mqttConfigSaveCombined(&r);assert(r.status==200&&actionFlag==10);
  assert(saved["haEnabled"].as<bool>()==bool(modes&1));assert(saved["Mqtt_Format"].as<int>()==((modes&2)?5:0));
  assert(saved["Mqtt_savedFormat"].as<int>()==((modes&2)?5:3));assert(saved["Mqtt_Password"]=="secret");
  assert(saved["haDiscoveryPrefix"]==((modes&1)?"homeassistant":"custom"));
  assert(saved["Mqtt_outTopic"]==((modes&2)?"topic":"legacy"));
 }
 AsyncWebServerRequest r;r.params={{"mqtAdres",{"broker"}},{"mqtPort",{"1883"}},{"mqtUser",{"user"}},{"mqtPas",{""}}};
 configSaveOk=false;actionFlag=0;mqttConfigSaveCombined(&r);assert(r.status==500&&actionFlag==0);
 configSaveOk=true;r.params["mqtPort"]={"1883x"};int before=configWrites;mqttConfigSaveCombined(&r);assert(r.status==400&&configWrites==before);
 r.params["mqtPort"]={"1883"};r.params["haEnabled"]={"on"};r.params["prefix"]={"bad/#"};mqttConfigSaveCombined(&r);assert(r.status==400&&configWrites==before);
 r.authorized=false;mqttConfigSaveCombined(&r);assert(r.status==403&&configWrites==before);
 std::cout<<"OTA checkpoint and MQTT configuration tests passed\n";
}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'test.cpp').write_text(harness+ota+mqtt+tests,encoding='utf-8')
 subprocess.run(['g++','-std=c++17','-I'+include,str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
