"""Exercise the production connection-test handlers and isolated MQTT worker."""
from pathlib import Path
import os,subprocess,tempfile
root=Path(__file__).resolve().parents[1]
# Share only host platform stubs, without executing the other test's main.
base=(root/'tools/test_mqtt_ota_settings.py').read_text(encoding='utf-8')
harness=base.split("harness = r'''")[1].split("'''")[0]
harness=harness.replace('using std::string::string;','using std::string::string;\n String(uint32_t n):std::string(std::to_string(n)){}',1)
harness=harness.replace('bool isEmpty()const', 'size_t write(uint8_t c){push_back(c);return 1;} size_t write(const uint8_t*p,size_t n){append((const char*)p,n);return n;} bool isEmpty()const')
harness=harness.replace('void send(int n,const char*,const char*s)', 'void send(int n,const char*,const String&s)')
stubs=r'''
#include <new>
#define portMUX_INITIALIZER_UNLOCKED 0
using portMUX_TYPE=int;
#define portENTER_CRITICAL(x) ((void)0)
#define portEXIT_CRITICAL(x) ((void)0)
#define pdPASS 1
bool createOk=true;void(*queued)(void*)=nullptr;void*argument=nullptr;
int xTaskCreate(void(*f)(void*),const char*,int,void*p,int,void*){if(!createOk)return 0;queued=f;argument=p;return 1;}
void vTaskDelete(void*){}
const char*ECU_ID="D8A3011B9780";const char*Mqtt_Password="saved-secret";
uint32_t esp_random(){return 123;}
int brokerResult=0,connects=0,disconnects=0;
std::string testedId,testedUser,testedPassword,testedBroker;int testedPort=0;
struct WiFiClient {void setConnectionTimeout(int n){assert(n==3000);}void stop(){}};
struct PubSubClient {
 PubSubClient(WiFiClient&){}
 void setServer(const char*s,int n){testedBroker=s;testedPort=n;}
 void setSocketTimeout(int n){assert(n==3);}
 bool connect(const char*id,const char*u="",const char*p=""){++connects;testedId=id;testedUser=u;testedPassword=p;return brokerResult==0;}
 int state(){return brokerResult;}
 void disconnect(){++disconnects;}
};
'''
tests=r'''
void parameters(AsyncWebServerRequest&r){r.params={{"mqtAdres",{"broker"}},{"mqtPort",{"1883"}},{"mqtUser",{"user"}},{"mqtPas",{""}}};}
int main(){
 AsyncWebServerRequest invalid;parameters(invalid);invalid.authorized=false;mqttConnectionStart(&invalid);assert(invalid.status==403&&!queued);
 invalid.authorized=true;invalid.params["mqtPort"]={"bad"};mqttConnectionStart(&invalid);assert(invalid.status==400&&!queued);
 parameters(invalid);invalid.params.erase("mqtPas");mqttConnectionStart(&invalid);assert(invalid.status==400&&!queued);
 parameters(invalid);createOk=false;mqttConnectionStart(&invalid);assert(invalid.status==503&&!mqttConnectionRunning);createOk=true;
 for(int state:{0,1,2,3,4,5,-4,-3,-2}){
  brokerResult=state;AsyncWebServerRequest r;parameters(r);mqttConnectionStart(&r);assert(r.status==202&&mqttConnectionRunning);
  auto id=mqttConnectionId;AsyncWebServerRequest other;parameters(other);mqttConnectionStart(&other);assert(other.status==409&&mqttConnectionId==id);
  AsyncWebServerRequest status;status.params["id"]={String(id)};mqttConnectionStatus(&status);assert(status.status==200&&status.message.find("\"running\":true")!=std::string::npos);
  queued(argument);queued=nullptr;assert(!mqttConnectionRunning&&mqttConnectionResult==state);
  mqttConnectionStatus(&status);assert(status.status==200&&status.message.find(mqttConnectionMessage(state))!=std::string::npos);
  assert(testedId.find("aps-test-")==0&&testedUser=="user"&&testedPassword=="saved-secret");assert(testedBroker=="broker"&&testedPort==1883);
 }
 AsyncWebServerRequest r;parameters(r);r.params["mqtPas"]={"typed-secret"};mqttConnectionStart(&r);queued(argument);assert(testedPassword=="typed-secret");
 AsyncWebServerRequest stale;stale.params["id"]={"1"};mqttConnectionStatus(&stale);assert(stale.status==409);
 stale.authorized=false;mqttConnectionStatus(&stale);assert(stale.status==403);
 assert(connects==disconnects&&configWrites==0&&actionFlag==0);
 std::cout<<"Connection tests: authentication, validation, busy/stale requests, task failure, login outcomes, isolated client and no writes passed\n";
}
'''
include=os.environ.get('ARDUINOJSON_INCLUDE',str(Path.home()/'Arduino/libraries/ArduinoJson/src'))
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'test.cpp').write_text(harness+stubs+(root/'MQTT_CONNECTION_TEST.ino').read_text(encoding='utf-8')+tests,encoding='utf-8')
 subprocess.run(['g++','-std=c++17','-I'+include,str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
