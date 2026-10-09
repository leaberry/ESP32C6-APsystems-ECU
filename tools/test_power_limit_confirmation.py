"""Replay missing readbacks through the actual power write and reply decoder."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
decoder = (root / 'ZIGBEE_QUERYING.ino').read_text(encoding='utf-8')
decoder = 'int decodeQueryAnswer(int welke)' + decoder.split('int decodeQueryAnswer(int welke)', 1)[1]
harness = r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <string>
#include <deque>
#include <vector>
#include <iostream>
#define F(x) x
#define CC2530_MAX_SERIAL_BUFFER_SIZE 512
struct String:std::string {
 using std::string::string;
 String(std::string s):std::string(s){} String(int n):std::string(std::to_string(n)){}
 void toCharArray(char* p,size_t n){snprintf(p,n,"%s",c_str());}
};
struct {template<typename... A>void printf(A...){} void println(String){}} Serial;
void consoleOut(String){} void delayMicroseconds(int){}
uint32_t clockMs=0;uint32_t millis(){return clockMs;}
void delay(unsigned ms){clockMs+=ms;}
String ECU_REVERSE(){return "ABCDEF123456";}
int inverterCount=3,zigbeeUp=1,errorCode=0,readCounter=0,desiredThrottle[9];
struct {int invType,calib;char invID[5];} Inv_Prop[9];
std::vector<std::string> sent;
std::deque<std::deque<std::string>> windows;
std::deque<std::string> replies;
int failTx=0;
void empty_serial2(){replies.clear();}
bool sendZB(char* command){
 sent.emplace_back(command);
 if(static_cast<int>(sent.size())==failTx)return false;
 if(strstr(command,"06DE000000000000E4FEFE")&&!windows.empty()){
  replies=windows.front();windows.pop_front();
 }
 return true;
}
char* readZB(char* out){
 out[0]=0;
 if(replies.empty()){clockMs+=3200;readCounter=0;return out;}
 clockMs+=50;snprintf(out,CC2530_MAX_SERIAL_BUFFER_SIZE,"%s",replies.front().c_str());
 replies.pop_front();readCounter=strlen(out)/2;return out;
}
char* split(char* s,const char* marker){char* p=strstr(s,marker);if(!p)return nullptr;*p=0;return p+strlen(marker);}
''' + decoder + (root / 'SETPOWER.ino').read_text(encoding='utf-8') + r'''
std::string readback(int watts,int model,int calibration=0){
 char hex[5];snprintf(hex,sizeof(hex),"%04X",(int)((watts+calibration)*(model==2?16.59:28.89)));
 return model==2?std::string("4481FBFB5CDDDE0104")+hex+"0013FEFE":std::string("4481FBFB4DDE000000")+hex+"3B660000FEFE";
}
void reset(int model,int watts,int calibration=0){
 sent.clear();windows.clear();replies.clear();clockMs=0;failTx=0;
 for(int i=0;i<3;++i){Inv_Prop[i]={model,calibration,"ABCD"};desiredThrottle[i]=500;}
 desiredThrottle[2]=watts;
}
void check(unsigned queries){
 assert(sent.size()==2+queries);
 // Exactly one power write and one activation, followed only by read-only queries.
 for(unsigned i=2;i<sent.size();++i)assert(sent[i].find("06DE000000000000E4FEFE")!=std::string::npos);
 assert(desiredThrottle[0]==500&&desiredThrottle[1]==500);
 assert(clockMs<22000); // includes the production reader's 3.2 second empty wait
}
int main(){
 const std::string ack="4481FBFB06DE02000000000000FEFE";
 for(int model=0;model<3;++model)for(int watts:{20,237,500})for(int calibration:{-5,0,10}){
  reset(model,watts,calibration);windows={{ack,readback(watts,model,calibration)}};
  assert(setMaxPower(2));check(1);
  // Production failure: only an intermediate ACK; a later explicit query succeeds.
  reset(model,watts,calibration);windows={{ack},{readback(watts,model,calibration)}};
  assert(setMaxPower(2));check(2);assert(desiredThrottle[2]==watts);
  reset(model,watts,calibration);windows={{},{ack},{readback(watts,model,calibration)}};
  assert(setMaxPower(2));check(3);
  // A stale value must not stop a later matching readback from confirming the write.
  reset(model,watts,calibration);windows={{readback(watts+10,model,calibration)},{readback(watts,model,calibration)}};
  assert(setMaxPower(2));check(2);
  // Silence, ACKs, malformed messages and mismatches never imply success.
  for(const auto& bad:{std::string(),ack,std::string("4481FBFB5CDDDE0104ZZZZ0013FEFE"),readback(watts+10,model,calibration)}){
   reset(model,watts,calibration);windows={{bad},{bad},{bad}};
   assert(!setMaxPower(2));check(3);
  }
 }
 for(int tx=1;tx<=5;++tx){
  reset(2,20);failTx=tx;windows={{ack},{ack},{ack}};
  assert(!setMaxPower(2));assert(sent.size()==static_cast<unsigned>(tx));
 }
 std::cout<<"PASS bounded confirmation: missing/ACK-only/stale replies recover, failures stay unknown, no repeated power writes, all models and calibration\n";
}
'''
with tempfile.TemporaryDirectory() as tmp:
    p = Path(tmp)
    (p / 'test.cpp').write_text(harness, encoding='utf-8')
    subprocess.run(['g++', '-std=c++17', str(p / 'test.cpp'), '-o', str(p / 'test')], check=True)
    subprocess.run([str(p / 'test')], check=True)
