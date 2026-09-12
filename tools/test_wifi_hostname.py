"""Exercise actual hostname startup, NVS writes and both HTTP save handlers."""
from pathlib import Path
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
portal = (root / 'PORTAL_WIFI.ino').read_text()
wifi = (root / 'Start_WiFi.ino').read_text()
web = (root / 'WEB_UI.ino').read_text()

def function(source, signature):
    start = source.index(signature)
    opening = source.index('{', start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]

harness = r'''
#include <cassert>
#include <string>
#include <map>
#include <algorithm>
#include <iostream>
#define F(x) x
struct String : std::string {
  using std::string::string;
  String(const std::string &s) : std::string(s) {}
  bool isEmpty() const { return empty(); }
  void trim() { auto first=find_first_not_of(" \t\r\n");
    *this=first==npos ? "" : substr(first,find_last_not_of(" \t\r\n")-first+1); }
  void toLowerCase() { for(auto &c:*this) if(c>='A'&&c<='Z') c+=32; }
  bool endsWith(const char *s) const { return !empty() && back()==s[0]; }
  void remove(size_t p) { erase(p); }
  int toInt() const { return empty() ? 0 : std::stoi(*this); }
};
std::map<std::string,String> storage;
std::string failKey, corruptKey;
bool openOk=true;
struct Preferences {
  bool begin(const char *,bool) { return openOk; }
  void end() {}
  bool isKey(const char *k) { return storage.count(k); }
  size_t putString(const char *k,const String &v) {
    if(failKey==k) return 0;
    storage[k]=corruptKey==k ? String("corrupt") : v; return v.length();
  }
  String getString(const char *k,const String &fallback) {
    return isKey(k) ? storage[k] : fallback;
  }
  size_t putBool(const char *k,bool v) { return putString(k,v?"1":"0"); }
  bool getBool(const char *k,bool fallback) {
    return isKey(k) ? storage[k]=="1" : fallback;
  }
};
String defaultWifiHostname() { return "aps-ecu-123456"; }
constexpr int WIFI_STA=1;
struct FakeWiFi {
  String configured,active;
  bool started=false, setOk=true, modeOk=true;
  struct Station { const char *getHostname(); } STA;
  bool setHostname(const char *s) { if(!setOk) return false; configured=String(s).substr(0,31); return true; }
  bool mode(int) { if(!modeOk) return false; if(!started) active=configured; started=true; return true; }
} WiFi;
const char *FakeWiFi::Station::getHostname() { return WiFi.started?WiFi.active.c_str():nullptr; }
struct Param { String text; String value() { return text; } };
struct AsyncWebServerRequest {
  std::map<std::string,Param> params;
  int status=0;
  bool hasParam(const char *k,bool) { return params.count(k); }
  Param *getParam(const char *k,bool) { return &params[k]; }
  String arg(const char *k) { return params[k].text; }
  void send(int code,const char *,const String &) { status=code; }
};
struct IPAddress { bool fromString(const String &v) { return !v.empty(); } };
String ecuPageStart(const char *,const char *) { return ""; }
String ecuPageEnd() { return ""; }
String webEscape(const String &s) { return s; }
String htmlEscape(const String &s) { return s; }
String buildPortalPage(const String &) { return ""; }
bool accessPasswordIsValid(const String &) { return true; }
char pswd[33]={};
void strlcpy(char *,const char *,size_t) {}
int securityLevel=0,actionFlag=0;
bool portalRebootPending=false;
unsigned portalRebootAt=0;
unsigned millis() { return 42; }
int constrain(int x,int a,int b) { return std::max(a,std::min(b,x)); }
void wifiConfigsave() {}
'''
harness += portal[portal.index('constexpr const char *WIFI_PREFS_NAMESPACE'):portal.index('constexpr uint32_t PORTAL_TIMEOUT_MS')]
for signature in ['String normalizeWifiHostname(', 'void loadStoredWifiCredentials(',
                  'bool wifiHostnameInputValid(', 'bool wifiSaveString(',
                  'bool saveStoredWifiConfiguration(', 'String normalizedWifiHostname(']:
    harness += function(portal, signature) + '\n'
for signature in ['String wifiActiveHostname(', 'bool wifiStartStationWithHostname(']:
    harness += function(wifi, signature) + '\n'
harness += function(web, 'void handleNetworkSave(')
handler = portal[portal.index('server.on("/wifi/save"'):]
harness += '\nvoid handlePortalSave' + function(handler, '(AsyncWebServerRequest *request)')
harness += r'''
int main() {
  assert(wifiActiveHostname()=="Unavailable");
  assert(wifiStartStationWithHostname("my-ecu"));
  assert(WiFi.active=="my-ecu");
  // A late global-name change must not masquerade as a changed interface name.
  WiFi.setHostname("other"); assert(wifiActiveHostname()=="my-ecu");
  assert(!wifiStartStationWithHostname("other"));
  WiFi={}; WiFi.setOk=false; assert(!wifiStartStationWithHostname("x") && !WiFi.started);
  WiFi={}; WiFi.modeOk=false; assert(!wifiStartStationWithHostname("x"));
  assert(normalizeWifiHostname("  My ECU!!  ")=="my-ecu");
  assert(normalizeWifiHostname(String(32,'a')).length()==31);
  assert(normalizeWifiHostname(std::string(30,'a')+"--b")==String(30,'a'));
  assert(normalizeWifiHostname("---")==defaultWifiHostname());
  assert(wifiHostnameInputValid(String(31,'a')));
  assert(!wifiHostnameInputValid(String(32,'a')) && !wifiHostnameInputValid("  "));
  for(auto handle:{handleNetworkSave,handlePortalSave}) {
    for(auto key:{"hostname","ssid","password","dhcp","ip","netmask","gateway"}) {
      storage.clear(); failKey=key; actionFlag=0; portalRebootPending=false;
      AsyncWebServerRequest r;
      r.params={{"ssid",{"network"}},{"s",{"network"}},{"hostname",{"My ECU"}},
                {"addressing",{"dhcp"}}};
      handle(&r); assert(r.status==500 && actionFlag==0 && !portalRebootPending);
    }
    failKey=""; storage.clear(); openOk=false;
    AsyncWebServerRequest r;
    r.params={{"ssid",{"network"}},{"s",{"network"}},{"hostname",{"ecu"}},
              {"addressing",{"dhcp"}}};
    handle(&r); assert(r.status==500 && !portalRebootPending && !actionFlag);
    openOk=true; corruptKey="hostname";
    handle(&r); assert(r.status==500 && !portalRebootPending && !actionFlag);
    corruptKey=""; storage.clear(); r.params["hostname"].text=String(32,'a');
    handle(&r); assert(r.status==400 && storage.empty());
    r.params["hostname"].text="My ECU"; handle(&r);
    assert(r.status==200 && storage["hostname"]=="my-ecu");
    assert(handle==handleNetworkSave ? actionFlag==10 : portalRebootPending);
    actionFlag=0; portalRebootPending=false;
  }
  std::cout << "PASS hostname startup, normalization, NVS failures and both HTTP save paths\n";
}
'''
with tempfile.TemporaryDirectory() as directory:
    cpp, binary = Path(directory) / 'test.cpp', Path(directory) / 'test'
    cpp.write_text(harness)
    subprocess.run([os.environ.get('CXX', 'g++'), '-std=c++11', '-Wall', '-Wextra',
                    str(cpp), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
