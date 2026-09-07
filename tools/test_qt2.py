"""Exercise the production decoder, MQTT, SunSpec and control guards with g++.

Run: python3 tools/test_qt2.py (no Arduino board or radio needed).
Captures are credited to TobiasTTM in fixtures/qt2.json and QT2.md.
"""
from pathlib import Path
import json
import os
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def function(file, signature):
    text = (ROOT / file).read_text()
    start = text.index(signature)
    end = text.index("{", start) + 1
    masked = re.sub(r'//[^\n]*|/\*[\s\S]*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'',
                    lambda m: " " * len(m[0]), text)
    depth = 1
    while depth:
        depth += (masked[end] == "{") - (masked[end] == "}")
        end += 1
    return text[start:end]


harness = r'''
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>
#include "QT2_PROTOCOL.h"
using std::isfinite;
using std::min;
#define F(x) x
#define CC2530_MAX_SERIAL_BUFFER_SIZE 1024
#define YC600_MAX_NUMBER_OF_INVERTERS 9
#define VERSION "test"
struct String : std::string {
  using std::string::string;
  String(const std::string &s):std::string(s) {}
  String(char *s):std::string(s) {}
  template<class T> String(T n):std::string(std::to_string(n)) {}
  void toCharArray(char *out, size_t size) const { snprintf(out,size,"%s",c_str()); }
};
size_t strlcpy(char *d,const char *s,size_t n) {
  size_t len=strlen(s); if(n) {size_t k=min(len,n-1);memcpy(d,s,k);d[k]=0;} return len;
}
size_t strlcat(char *d,const char *s,size_t n) {
  size_t len=strlen(d); return len+strlcpy(d+len,s,n-len);
}
void consoleOut(const String &) {}
void yield() {}
void delayMicroseconds(int) {}
float round1(float x) {return roundf(x*10)/10;}
struct SerialStub {
  template<class T> void println(T) {}
  template<class... T> void printf(const char *,T...) {}
} Serial;
int inverterCount=1, readCounter=0, errorCode=0, zigbeeUp=1;
int t_saved[9]={}, desiredThrottle[10]={};
float en_saved[9][4]={}, recorded=0;
bool polled[9]={};
int transmissions=0, reads=0, recordCalls=0;
std::string incoming;
char *readZB(char *out) {++reads;strlcpy(out,incoming.c_str(),1024);readCounter=incoming.size()/2;return out;}
void qt2CaptureObserve(int,const uint8_t *,size_t,uint16_t,uint16_t,int8_t,uint8_t) {}
void energyRecordDelta(int,float n) {recorded+=n;++recordCalls;}
void sendZB(char *) {++transmissions;}
bool waitSerial2Available() {return false;}
void empty_serial2() {}
String ECU_REVERSE() {return String("123456789012");}
char *split(char *s,const char *sep) {char *p=strstr(s,sep);return p?p+strlen(sep):s;}
int Mqtt_Format=0;
char Mqtt_outTopic[40]="ecu/", ECU_ID[13]="123456789012";
struct MqttStub {
  void publish(const char *,const char *json,bool) {std::cout<<"JSON "<<Mqtt_Format<<" "<<json<<'\n';}
} MQTT_Client;
bool mqttConnect() {return true;}
uint64_t energyLifetimeWhFor(int) {return 1234;}
const size_t SUNSPEC_REG_COUNT=124;
'''

includes = (ROOT / "AAA_INCLUDES.h").read_text()
for typename in ("inverters", "inverterdata"):
    end = includes.index("} " + typename + ";") + len("} " + typename + ";")
    start = includes.rfind("typedef struct", 0, end)
    harness += includes[start:end] + "\n"
harness += "inverters Inv_Prop[9]; inverterdata Inv_Data[9];\n"

for file, signatures in {
    "handledata.ino": ["static uint8_t inverterPhysicalPanelCount("],
    "ZIGBEE_QUERYING.ino": ["int decodeQueryAnswer("],
    "SETPOWER.ino": ["bool inverterSupportsThrottle("],
}.items():
    for sig in signatures:
        # Forward declarations before implementations with cross references.
        f = function(file, sig)
        harness += f[:f.index("{")] + ";\n"

for file, signatures in {
    "handledata.ino": ["static uint8_t inverterPhysicalPanelCount("],
    "AAA_DECODE.ino": ["float extractValue(", "int decodePollAnswer(", "void mqttPoll("],
    "SETPOWER.ino": ["bool inverterSupportsThrottle(", "bool setMaxPower("],
    "ZIGBEE_QUERYING.ino": ["void querying(", "int decodeQueryAnswer("],
}.items():
    for sig in signatures:
        harness += function(file, sig) + "\n"

ss = (ROOT / "SUNSPEC_MODBUS.ino").read_text()
harness += ss[ss.index("static void ssPut16("):ss.index("static void ssException(")]
fixtures = json.loads((ROOT / "tools/fixtures/qt2.json").read_text())["captures"]
# The original dump includes four trailing ZNP bytes outside the L2 frame.
harness += "const char *captures[]={\n" + ",\n".join(
    json.dumps(c["hex"][:-8]) for c in fixtures) + "};\n"
# Existing YC600/QS1 fixture strings and DS3 capture from main.
legacy = re.findall(r'strncpy\(inMessage, "([0-9a-fA-F]+)"',
                    (ROOT / "test.ino").read_text())[:3]
harness += "const char *legacy[]={\n" + ",\n".join(map(json.dumps, legacy)) + "};\n"
harness += r'''
std::string wrap(const std::string &hex) {
  return "FE0164010064FE034480001400D3FE7A4481000006018B1D1414008000000000000069"+hex;
}
void near(float x,float y,float tol=.001f) {if(std::abs(x-y)>tol) {std::cerr<<"actual="<<x<<" expected="<<y<<"\n";abort();}}
void put(std::string &s,size_t offset,uint32_t n,size_t bytes=2) {
  char b[9];snprintf(b,sizeof(b),bytes==2?"%04X":"%08X",n);s.replace(offset*2,bytes*2,b);
}
void checksum(std::string &s) {
  unsigned sum=0;for(size_t i=8;i<101;++i) sum+=std::stoul(s.substr(i*2,2),nullptr,16);
  put(s,101,sum);
}
void reject(const std::string &hex) {
  inverterdata before=Inv_Data[0];float energy[4];memcpy(energy,en_saved[0],sizeof(energy));
  int time=t_saved[0],calls=recordCalls;float total=recorded;
  incoming=wrap(hex);assert(decodePollAnswer(0)!=0);
  assert(memcmp(&before,&Inv_Data[0],sizeof(before))==0);
  assert(memcmp(energy,en_saved[0],sizeof(energy))==0);
  assert(time==t_saved[0] && calls==recordCalls && total==recorded);
}
int main() {
  Inv_Prop[0].invType=3;strcpy(Inv_Prop[0].invSerial,"901000010817");
  Qt2Telemetry q={};
  for(int i=0;i<11;++i) {
    bool valid=i!=8 && i!=10;
    assert(decodeQt2(captures[i],strlen(captures[i]),Inv_Prop[0].invSerial,q)==valid);
    incoming=wrap(captures[i]);assert((decodePollAnswer(0)==0)==valid);
  }
  assert(decodeQt2(captures[0],210,Inv_Prop[0].invSerial,q));
  near(q.acv[0],234);near(q.acv[1],235.6);near(q.acv[2],236.7);
  near(q.frequency,50.03);near(q.temperature,19.51);
  near(q.dcv[0],43.11787);near(q.dcv[1],43.11787);
  near(q.dcv[2],7.52852);near(q.dcv[3],7.52852);
  near(q.dcc[0],2.011236);near(q.dcc[1],.089888);near(q.dcc[2],.134831);near(q.dcc[3],.179775);
  assert(q.timestamp==585 && q.energy[0]==432676 && q.energy[1]==19830 && q.energy[2]==5456 && q.energy[3]==5963);
  incoming=wrap(captures[3]);assert(decodePollAnswer(0)==0);
  near(Inv_Data[0].freq,0);near(Inv_Data[0].acv,1.7);
  float offGridTotal=recorded;
  incoming=wrap(captures[4]);assert(decodePollAnswer(0)==0);
  near(recorded,offGridTotal);near(Inv_Data[0].pw_total,0);
  incoming=wrap(captures[5]);assert(decodePollAnswer(0)==0);
  near(recorded,offGridTotal);near(Inv_Data[0].pw_total,0);
  std::cout<<"PASS nine captured polls, two rejected configuration replies and decoded reference values\n";
  std::string base=captures[0];
  for(size_t i=0;i<base.size();++i) {reject(base.substr(0,i));auto bad=base;bad[i]='G';reject(bad);}
  for(size_t off: {size_t(6),size_t(7),size_t(8),size_t(9),size_t(10),size_t(11),size_t(103)}) {
    auto bad=base;bad[off*2]='0';checksum(bad);reject(bad);
  }
  auto bad=base;bad[60]='1';reject(bad); // checksum mismatch
  bad=base;bad[0]='8';reject(bad); // wrong inverter
  reject(base+"00");reject(std::string(captures[8]));reject(std::string(captures[10]));
  incoming=wrap(base);incoming.replace(incoming.find("0069")+2,2,"68");assert(decodePollAnswer(0)!=0);
  incoming=std::string(300,'A');assert(decodePollAnswer(0)!=0);
  assert(decodePollAnswer(-1)!=0 && decodePollAnswer(9)!=0);
  bad=base;std::transform(bad.begin(),bad.end(),bad.begin(),::tolower);
  assert(decodeQt2(bad.c_str(),bad.size(),Inv_Prop[0].invSerial,q));
  std::cout<<"PASS every truncation and non-hex position, identity/header/checksum/length guards, unchanged state on rejection\n";

  // Known 1 Wh per input over 60 seconds => 60 W each, 240 W total.
  base=captures[0];put(base,38,100);
  for(int i=0;i<4;++i) put(base,70+4*i,31600,4);
  checksum(base);t_saved[0]=0;recorded=0;Inv_Data[0].en_total=0;
  incoming=wrap(base);assert(decodePollAnswer(0)==0);near(recorded,0);near(Inv_Data[0].pw_total,0);
  put(base,38,160);for(int i=0;i<4;++i) put(base,70+4*i,63200,4);checksum(base);
  incoming=wrap(base);assert(decodePollAnswer(0)==0);near(recorded,4);near(Inv_Data[0].pw_total,240);
  assert(decodePollAnswer(0)==0);near(recorded,4);near(Inv_Data[0].pw_total,0); // duplicate
  put(base,38,1);checksum(base);incoming=wrap(base);assert(decodePollAnswer(0)==0);near(recorded,4); // reboot/wrap
  put(base,38,61);for(int i=0;i<4;++i) put(base,70+4*i,1,4);checksum(base);
  incoming=wrap(base);assert(decodePollAnswer(0)==0);near(recorded,4);near(Inv_Data[0].pw_total,0);
  for(int i=0;i<4;++i) put(base,70+4*i,0xF1234567u,4);checksum(base);
  assert(decodeQt2(base.c_str(),210,Inv_Prop[0].invSerial,q));assert(q.energy[3]==0xF1234567u);
  // Disconnected inputs do not accumulate energy.
  Inv_Prop[0].conPanels[2]=Inv_Prop[0].conPanels[3]=false;
  t_saved[0]=0;incoming=wrap(captures[0]);assert(decodePollAnswer(0)==0);
  incoming=wrap(captures[1]);assert(decodePollAnswer(0)==0);
  near(Inv_Data[0].pw_total,104.573f,.1f);
  Inv_Prop[0].conPanels[2]=Inv_Prop[0].conPanels[3]=true;
  std::cout<<"PASS energy baselines, duplicate/reset clocks, reset counters, unsigned counters and disconnected panels\n";

  for(int type=0;type<4;++type) {
    Inv_Prop[0].invType=type;
    assert(inverterPhysicalPanelCount(0)==(type==1||type==3?4:2));
    if(type<3) {incoming=legacy[type];t_saved[0]=0;assert(decodePollAnswer(0)==0);assert(isfinite(Inv_Data[0].freq));}
    else {incoming=wrap(captures[0]);assert(decodePollAnswer(0)==0);}
    // Large realistic totals exercise the actual JSON buffers.
    Inv_Data[0].en_total=123456789;Inv_Data[0].pw_total=2000;
    for(int i=0;i<4;++i) {en_saved[0][i]=135917;Inv_Data[0].power[i]=500;}
    std::cout<<"MODEL "<<type<<'\n';
    for(Mqtt_Format=1;Mqtt_Format<=4;++Mqtt_Format) mqttPoll(0);
    uint16_t bank[SUNSPEC_REG_COUNT];assert(ssBuildBank(2,bank));
    assert(bank[70]==(type==3?103:101) && bank[71]==50 && bank[122]==0xFFFF);
    if(type==3) {
      assert(bank[72]==0xFFFF && bank[73]==0xFFFF); // no invented AC current
      assert(bank[80]==234 && bank[81]==236 && bank[82]==237);
      assert(ssBuildBank(1,bank));assert(bank[70]==101 && bank[72]==0xFFFF);
    } else assert(bank[81]==0xFFFF && bank[82]==0xFFFF);
  }
  Inv_Prop[0].invType=3;transmissions=reads=0;
  assert(!setMaxPower(0));querying(0);assert(decodeQueryAnswer(0)!=0);
  assert(!setMaxPower(-1) && !setMaxPower(9));assert(transmissions==0 && reads==0);
  for(int type=0;type<3;++type) {Inv_Prop[0].invType=type;assert(inverterSupportsThrottle(0));}
  std::cout<<"PASS legacy models, MQTT, SunSpec phase registers and QT2 control rejection without radio traffic\n";
}
'''

with tempfile.TemporaryDirectory(prefix="ecu-qt2-") as directory:
    cpp = Path(directory) / "test.cpp"
    binary = Path(directory) / "test"
    cpp.write_text(harness)
    subprocess.run([os.environ.get("CXX", "g++"), "-std=c++11", "-g",
                    "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
                    "-I", str(ROOT), str(cpp), "-o", str(binary)], check=True)
    result = subprocess.run([str(binary)], text=True, capture_output=True)
    if result.returncode:
        print(result.stdout, result.stderr)
        result.check_returncode()
    model = None
    for line in result.stdout.splitlines():
        if line.startswith("MODEL "):
            model = int(line.split()[1])
        elif line.startswith("JSON "):
            _, fmt, payload = line.split(" ", 2)
            data = json.loads(payload)
            assert len(payload) + 40 + 10 < 640
            if int(fmt) >= 3:
                assert "acv" in data
                assert ("acv2" in data) == (model == 3)
                if model == 3:
                    assert data["acv"] == data["acv0"] == 234
                count = 4 if model in (1, 3) else 2
                if fmt == "3":
                    assert all(len(data[k]) == count for k in ("dcv", "dcc", "pwr", "en"))
                    assert "energy_total" in data
                else:
                    assert all("ch" + str(i) in data for i in range(count))
        else:
            print(line)
    print("PASS all sixteen MQTT messages parse as complete JSON within the packet buffer")
