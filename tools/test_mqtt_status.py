"""Render both MQTT service states using the production System information helper."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'WEB_UI.ino').read_text()
helper = source.split('String mqttServicesHtml() {', 1)[1].split('\n}', 1)[0]
assert 'page += mqttServicesHtml();' in source
harness = r'''
#include <cassert>
#include <string>
#include <initializer_list>
using String=std::string;
#define F(x) x
int Mqtt_Format=0;
bool haEnabled=false;
struct Client {bool up=false;bool connected(){return up;}} MQTT_Client,haClient;
String mqttServicesHtml() {
'''+helper+r'''
}
int main(){
 for(int format:{0,1,2,3})for(bool ha:{false,true})
 for(bool legacyUp:{false,true})for(bool haUp:{false,true}) {
  Mqtt_Format=format;haEnabled=ha;MQTT_Client.up=legacyUp;haClient.up=haUp;
  String expected="<dt>Domoticz MQTT</dt><dd>";
  expected+=!format?"Disabled":legacyUp?"Connected":"Disconnected";
  expected+="</dd><dt>Home Assistant MQTT</dt><dd>";
  expected+=!ha?"Disabled":haUp?"Connected":"Disconnected";
  expected+="</dd>";
  assert(mqttServicesHtml()==expected);
 }
}
'''
with tempfile.TemporaryDirectory() as directory:
    path = Path(directory)
    (path / 'test.cpp').write_text(harness)
    subprocess.run(['g++', '-std=c++17', str(path / 'test.cpp'), '-o', str(path / 'test')], check=True)
    subprocess.run([str(path / 'test')], check=True)
print('Independent Home Assistant and Domoticz MQTT status combinations passed.')
