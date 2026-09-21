"""Run production telemetry validation, collection and round handling with fake RF."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]

def function(name, signature):
    source = (root / name).read_text(encoding='utf-8')
    start = source.index(signature)
    end = source.index('{', start) + 1
    depth = 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]

code = r'''
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <deque>
#include <functional>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>
#include "POLL_TELEMETRY.h"
using std::min;
#define F(x) x
constexpr int YC600_MAX_NUMBER_OF_INVERTERS=9, CC2530_MAX_SERIAL_BUFFER_SIZE=1024;
struct String: std::string {
 using std::string::string;
 String(std::string s):std::string(s){}
 String(char* s):std::string(s){}
 template<class T> String(T n):std::string(std::to_string(n)){}
 void toCharArray(char* p,size_t n){snprintf(p,n,"%s",c_str());}
};
struct Prop {char invID[5],invSerial[13];int invType;bool conPanels[4]={true,true,false,false};};
Prop Inv_Prop[9]={{"0100","000000000001",2},{"0200","000000000002",0},
 {"0300","000000000003",0},{"0400","000000000004",1},{"0500","000000000005",2}};
struct {int radioRssi,radioLqi;bool radioMetricsValid;float sigQ,acv,freq,heath,dcv[4],dcc[4],power[4],en_total,pw_total;} Inv_Data[9];
int t_saved[9]={};float en_saved[9][4]={};
void energyRecordDelta(int,float){} float round1(float n){return std::round(n*10)/10;}
void delayMicroseconds(int){}
char* split(char* s,const char* d){char* p=strstr(s,d);if(!p)return nullptr;*p=0;return p+strlen(d);}
int inverterCount=5,zigbeeUp=1,errorCode=0;
bool polled[9]={},timeRetrieved=true;
time_t inverterLastPollSuccess[9]={};
uint32_t now=1000;
uint32_t millis(){return now;}
time_t ecuNow(){return now;}
void delay(uint32_t n){now+=n;}void yield(){} uint32_t esp_random(){return 0;}
void consoleOut(String){} void diagnosticsAppend(String){}
String ECU_REVERSE(){return "000000000099";}
int published[9]={},decodedCount[9]={};
void energyRecordTelemetry(int i){++published[i];}void haTelemetry(int){}void mqttPoll(int){}
uint16_t pans[9]={0xA3D8,0xA3D8,0xA3D8,0xA3D8,0xCAFE},rawCurrentPan=0xA3D8;
bool apsRadioLoadPeer(const char* s,uint16_t* p,uint16_t*){
 for(int i=0;i<inverterCount;++i)if(!strcmp(s,Inv_Prop[i].invSerial)){*p=pans[i];return true;}return false;
}
bool apsSerialToBcd(const char* s,uint8_t* b){for(int i=0;i<6;++i)b[i]=((s[2*i]-'0')<<4)|(s[2*i+1]-'0');return true;}
bool apsDecryptIncoming(uint16_t,const uint8_t* a,size_t n,uint8_t* out,size_t,size_t* len,int*){
 if(n<8||a[6]!=0xfb||a[7]!=0xfb)return false;memcpy(out,a,n);*len=n;return true;
}
void pollPublishTelemetry(int);
#include "POLL_DIAGNOSTICS.h"
uint32_t diagCounts[PD_COUNT]={};uint16_t diagSeen=0,diagAccepted=0;
void pollDiagnosticsCount(PollDiagCounter c,uint32_t n){diagCounts[c]+=n;}
void pollDiagnosticsReply(int i){if(i>=0)diagSeen|=1U<<i;}
void pollDiagnosticsAccepted(int i){diagAccepted|=1U<<i;}
void pollDiagnosticsStart(int,uint8_t,uint16_t){}
void pollDiagnosticsFinish(bool,int,uint16_t){}

'''
transport = (root/'ZIGBEE_A_TRANSPORT.ino').read_text(encoding='utf-8')
start = transport.index('struct ApsRxFrame {')
code += transport[start:transport.index('\n};', start)+3]
code += r'''
std::deque<ApsRxFrame> queue;
bool apsRxQueue=true;
constexpr int pdTRUE=1;
uint32_t pdMS_TO_TICKS(uint32_t n){return n;}
int xQueueReceive(bool,ApsRxFrame* f,uint32_t wait){
 if(queue.empty()){now+=wait;return 0;}*f=queue.front();queue.pop_front();++now;return 1;
}
void empty_serial2(){queue.clear();}
void appendHex(char* out,size_t cap,const uint8_t* b,size_t n){
 for(size_t i=0;i<n;++i){size_t p=strlen(out);assert(p+2<cap);snprintf(out+p,cap-p,"%02X",b[i]);}
}

'''
code += function('AAA_DECODE.ino', 'float extractValue(')
code += function('AAA_DECODE.ino', 'int decodePollMessage(').replace('{', '{ ++decodedCount[which];', 1)
code += function('ZIGBEE_A_TRANSPORT.ino', 'static void formatApsReply(')
code += function('ZIGBEE_A_TRANSPORT.ino', 'uint16_t readPollReplies(')
code += r'''
int sends=0;
std::function<void(int)> onSend;
bool txOK=true;
bool sendZB(char* cmd){
 int target=(cmd[5]-'0')-1;
 assert(target>=0&&target<5);rawCurrentPan=pans[target];++sends;
 if(onSend)onSend(target);return txOK;
}
'''
code += (root/'ZIGBEE_POLLING.ino').read_text(encoding='utf-8')
code += r'''
void checksum(ApsRxFrame &f){
 uint16_t sum=0;for(size_t i=8;i<size_t(f.len-4);++i)sum+=f.data[i];
 f.data[f.len-4]=sum>>8;f.data[f.len-3]=sum;
}
ApsRxFrame reply(int i){
 ApsRxFrame f={};f.receivedAt=now;f.pan=pans[i];f.cluster=0x0106;
 f.sourceEp=f.destEp=0x14;f.lqi=200;f.rssi=-60;
 f.len=Inv_Prop[i].invType==2?105:94;apsSerialToBcd(Inv_Prop[i].invSerial,f.data);
 f.data[6]=f.data[7]=0xfb;f.data[8]=f.len-13;
 f.data[9]=Inv_Prop[i].invType==2?0xbb:0xb1;f.data[12]=1;
 f.data[f.len-2]=f.data[f.len-1]=0xfe;checksum(f);return f;
}
void reset(){
 now=1000;sends=0;txOK=true;zigbeeUp=1;onSend=nullptr;
 memset(polled,0,sizeof(polled));memset(published,0,sizeof(published));
 memset(decodedCount,0,sizeof(decodedCount));pollingRoundBegin();
}
int main(){
 // Full actual DS3 capture from the decoder documentation validates checksum/length.
 const char* captured="703000021300fbfb5cbbbb20000200e6ffff000000000000000006f506f9002e00340360138a17a70024001fffff054206900016f62b0018e451ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff3969fefe";
 std::vector<uint8_t> bytes;
 for(size_t i=0;i<strlen(captured);i+=2){unsigned b;sscanf(captured+i,"%2x",&b);bytes.push_back(b);}
 assert(pollTelemetryValid(bytes.data(),bytes.size(),2));
 for(int i=0;i<5;++i){auto f=reply(i);assert(pollTelemetryValid(f.data,f.len,Inv_Prop[i].invType));}
 // Mixed DS3/YC600/QS1 replies, different order, duplicate, and second PAN.
 reset();onSend=[](int i){if(i==0){for(int j:{2,2,3,1,0})queue.push_back(reply(j));}else queue.push_back(reply(i));};
 for(int i=0;i<5;++i)pollingForRound(i);
 assert(sends==2&&pollingRoundSucceeded());
 for(int i=0;i<5;++i)assert(published[i]==1&&decodedCount[i]==1&&polled[i]);
 // Never count a mere reply identity: reject bad length, checksum, opcode,
 // terminator, frequency divisor, other PAN/cluster/endpoint, stale and unknown UID.
 reset();onSend=[](int){
  auto f=reply(1);f.data[40]^=1;queue.push_back(f);
  f=reply(1);--f.len;queue.push_back(f);
  f=reply(1);f.data[9]=0xde;checksum(f);queue.push_back(f);
  f=reply(1);f.data[f.len-1]=0;queue.push_back(f);
  f=reply(1);f.data[12]=0;checksum(f);queue.push_back(f);
  f=reply(1);f.pan=0xCAFE;queue.push_back(f);
  f=reply(1);f.cluster=1;queue.push_back(f);
  f=reply(1);f.sourceEp=1;queue.push_back(f);
  f=reply(1);f.receivedAt=999;queue.push_back(f);
  f=reply(1);f.data[5]=0x99;queue.push_back(f);
 };
 pollingForRound(1);assert(sends==2&&!polled[1]&&published[1]==0&&!pollingRoundSucceeded());
 assert(diagCounts[PD_BAD_CHECKSUM]==2&&diagCounts[PD_BAD_LENGTH]==4&&diagCounts[PD_BAD_KIND]==2&&diagCounts[PD_BAD_VALUE]==2);
 assert(diagCounts[PD_STALE]==2&&diagCounts[PD_UNEXPECTED]==8&&diagCounts[PD_DUPLICATE]>=1);
 assert(diagSeen==31&&diagAccepted==31);
 // Target times out twice, then recovers during a subsequent inverter's poll.
 reset();onSend=[](int i){if(i==1)for(int j:{3,0,2,1})queue.push_back(reply(j));else if(i==4)queue.push_back(reply(i));};
 for(int i=0;i<5;++i)pollingForRound(i);
 assert(sends==4&&pollingRoundSucceeded());for(int i=0;i<5;++i)assert(published[i]==1);
 // Retry only the missing target; other successes are not decoded twice.
 reset();onSend=[](int){queue.push_back(reply(2));if(sends==2)queue.push_back(reply(1));};
 pollingForRound(1);assert(sends==2&&published[1]==1&&published[2]==1);
 assert(!pollingRoundSucceeded()&&!polled[0]);
 // Single-target manual poll neither publishes other responders nor pollutes round mask.
 reset();onSend=[](int){queue.push_back(reply(2));queue.push_back(reply(1));};
 polling(1);assert(sends==1&&published[1]==1&&published[2]==0&&pollRoundAccepted==0);
 // Failed transmission cannot consume queued traffic as success.
 reset();txOK=false;onSend=[](int){queue.push_back(reply(1));};
 pollingForRound(1);assert(sends==2&&published[1]==0&&!polled[1]);
 // A second response for the same inverter must not reset its energy baseline/power.
 reset();onSend=[](int){auto f=reply(1);f.data[17]=0;f.data[18]=10;f.data[39]=10;checksum(f);queue.push_back(f);};
 polling(1);assert(t_saved[1]==10);
 pollingRoundBegin();onSend=[](int){auto f=reply(1);f.data[17]=0;f.data[18]=20;f.data[39]=20;checksum(f);queue.push_back(f);queue.push_back(f);};
 pollingForRound(1);assert(Inv_Data[1].pw_total>0&&t_saved[1]==20&&published[1]==2);
 // Stale-frame comparison also works around millis() wrap.
 reset();now=0xfffffff0;pollingRoundBegin();now=5;
 onSend=[](int){auto f=reply(1);f.receivedAt=0xffffffe0;queue.push_back(f);queue.push_back(reply(1));};
 polling(1);assert(published[1]==1);
 std::cout<<"PASS telemetry collection: all responders, duplicates, validation, stale frames, late recovery, retries, PAN isolation, manual polls and TX failures\n";
}
'''
with tempfile.TemporaryDirectory() as tmp:
    p = Path(tmp)
    (p/'test.cpp').write_text(code, encoding='utf-8')
    subprocess.run(['g++', '-std=c++17', '-Wall', '-Wextra', '-Wno-misleading-indentation',
                    '-fsanitize=undefined', '-I', str(root), str(p/'test.cpp'), '-o', str(p/'test')], check=True)
    subprocess.run([str(p/'test')], check=True)
