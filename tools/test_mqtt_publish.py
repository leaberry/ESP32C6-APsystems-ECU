"""Exercise production HA publishing and installed PubSubClient packet methods."""
from pathlib import Path
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
include = Path(os.environ.get('ARDUINOJSON_INCLUDE', str(Path.home() / 'Arduino/libraries/ArduinoJson/src')))
library = Path(os.environ.get('PUBSUBCLIENT_SOURCE', str(include.parents[1] / 'PubSubClient/src/PubSubClient.cpp')))

def function(source, signature):
    start = source.index(signature)
    end = source.index('{', start) + 1
    depth = 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end] + '\n'

transport = library.read_text()
production = (root / 'HOME_ASSISTANT.ino').read_text()
harness = r'''
#include <ArduinoJson.h>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <algorithm>
#include <iostream>
using String=std::string;
using boolean=bool;
#define MQTT_MAX_HEADER_SIZE 5
#define MQTTPUBLISH 0x30
#define MQTTDISCONNECT 0xE0
#define MQTT_DISCONNECTED -1
uint32_t millis(){return 100;}
struct Network {
 bool up=true;int limit=-1,stops=0;
 std::vector<uint8_t> wire;
 void connect(){wire.clear();limit=-1;up=true;}
 void stop(){up=false;++stops;}
 void flush(){}
 size_t write(const uint8_t*p,size_t n){
  if(!up)return 0;
  if(limit>=0)n=std::min(n,(size_t)limit);
  wire.insert(wire.end(),p,p+n);if(limit>=0)limit-=n;return n;
 }
} haNetwork;
struct PubSubClient {
 Network *_client=&haNetwork;
 uint8_t buffer[8192];uint16_t bufferSize=sizeof(buffer);int _state=0;
 uint32_t lastInActivity=0,lastOutActivity=0;
 bool connected(){return _client->up;}
 boolean beginPublish(const char*,unsigned int,boolean);
 int endPublish();
 size_t write(const uint8_t*,size_t);
 boolean write(uint8_t,uint8_t*,uint16_t);
 size_t buildHeader(uint8_t,uint8_t*,uint16_t);
 uint16_t writeString(const char*,uint8_t*,uint16_t);
 boolean publish(const char*,const char*,boolean);
 boolean publish(const char*,const uint8_t*,unsigned int,boolean);
 void disconnect();
} haClient;
'''
for signature in [
    'boolean PubSubClient::beginPublish(', 'int PubSubClient::endPublish(',
    'size_t PubSubClient::write(const uint8_t', 'boolean PubSubClient::write(uint8_t header',
    'size_t PubSubClient::buildHeader(', 'uint16_t PubSubClient::writeString(',
    'boolean PubSubClient::publish(const char* topic, const char* payload, boolean retained)',
    'boolean PubSubClient::publish(const char* topic, const uint8_t* payload, unsigned int plength, boolean retained)',
    'void PubSubClient::disconnect(',
]:
    harness += function(transport, signature)
harness += production[production.index('struct HaPublishWriter'):production.index('bool haFresh')]
harness += r'''
std::vector<uint8_t> frame(const String&topic,const String&payload){
 std::vector<uint8_t> result{0x31};size_t remaining=topic.size()+2+payload.size();
 do{uint8_t byte=remaining%128;remaining/=128;if(remaining)byte|=128;result.push_back(byte);}while(remaining);
 result.push_back(topic.size()>>8);result.push_back(topic.size()&255);
 result.insert(result.end(),topic.begin(),topic.end());result.insert(result.end(),payload.begin(),payload.end());
 return result;
}
int main(){
 const String topic="aps_ecu/123456789ABC/inverter/408000000001/state";
 JsonDocument doc;doc["value"]=String(1200,'x');String payload;serializeJson(doc,payload);
 const auto expected=frame(topic,payload);
 assert(haPublishJson(topic,doc)&&haNetwork.wire==expected);
 assert(haPublishJson(topic,doc));auto doubled=expected;doubled.insert(doubled.end(),expected.begin(),expected.end());
 assert(haNetwork.wire==doubled);
 // Every possible short header/body write, including the 512-byte payload
 // boundary, must end the connection before another packet can reach it.
 for(size_t cut=0;cut<expected.size();++cut){
  haNetwork.connect();haNetwork.limit=cut;const int stops=haNetwork.stops;
  assert(!haPublishJson(topic,doc));assert(!haClient.connected()&&haNetwork.stops>stops);
  const std::vector<uint8_t> prefix(expected.begin(),expected.begin()+cut);
  assert(haNetwork.wire==prefix); // No DISCONNECT appended to the partial packet.
  haNetwork.limit=-1;assert(!haPublishJson(topic,doc));assert(haNetwork.wire==prefix);
  haNetwork.connect();assert(haPublishJson(topic,doc)&&haNetwork.wire==expected);
 }
 const auto textFrame=frame(topic,"online");
 for(size_t cut=0;cut<textFrame.size();++cut){
  haNetwork.connect();haNetwork.limit=cut;assert(!haPublishText(topic,"online",true));
  assert(!haClient.connected()&&haNetwork.wire==std::vector<uint8_t>(textFrame.begin(),textFrame.begin()+cut));
 }
 haNetwork.connect();assert(haPublishText(topic,"online",true)&&haNetwork.wire==textFrame);
 haNetwork.connect();doc["value"]=String(17000,'x');
 assert(!haPublishJson(topic,doc)&&haClient.connected()&&haNetwork.wire.empty());
 std::cout<<"Actual PubSubClient header/body short writes, TCP abort order, reconnect, text and size bounds passed\n";
}
'''
with tempfile.TemporaryDirectory() as directory:
    path = Path(directory)
    (path / 'test.cpp').write_text(harness)
    subprocess.run(['g++', '-std=c++17', '-Wall', '-Wextra', '-I'+str(include), str(path / 'test.cpp'), '-o', str(path / 'test')], check=True)
    subprocess.run([str(path / 'test')], check=True)
