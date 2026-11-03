"""Replay the real QS1 transmitter and pairing flow without RF or storage writes."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]

def function(file, signature):
    source = (root / file).read_text(encoding='utf-8')
    start = source.index(signature)
    end = source.index('{', start) + 1
    depth = 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end] + '\n'

code = r'''
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <iostream>
#include "PAIRING_PROTOCOL.h"
#include "PAIRING_AUDIT.h"
#include "APS_TRANSPORT_MODE.h"
#include "QS1_PAIRING.h"
using std::min;
struct String:std::string {
 using std::string::string;String(const std::string &s):std::string(s){}
 void toCharArray(char*p,size_t n) const {snprintf(p,n,"%s",c_str());}
};
#define F(x) x
using portMUX_TYPE=int;
#define portMUX_INITIALIZER_UNLOCKED 0
void portENTER_CRITICAL(int*){} void portEXIT_CRITICAL(int*){}
using esp_err_t=int;
constexpr int ESP_OK=0,pdTRUE=1,APS_CHANNEL=16;
uint32_t now=100;uint32_t millis(){return now;}
uint16_t zbOperationalPan=0xA3D8,currentPan=0xA3D8;
bool rawRadioStarted=true,rawPromiscuous=false,probe=false,traceReady=true;
int rawTxMutex=1,zigbeeUp=1,rawTxFailure=0,failTx=0,receiveCalls=0,failReception=0;
uint8_t rawMacSequence=0,rawNwkSequence=0,rawApsCounter=0;
int pdMS_TO_TICKS(int n){return n;} int xSemaphoreTake(int,int){return pdTRUE;}
void xSemaphoreGive(int){} bool passiveCaptureOwnsRadio(){return false;}
int esp_ieee802154_sleep(){return ESP_OK;}
bool rawRadioSetPan(uint16_t pan){currentPan=pan;return true;}
bool rawRadioSetPromiscuous(bool value){rawPromiscuous=value;return true;}
uint16_t esp_ieee802154_get_panid(){return currentPan;}
bool esp_ieee802154_get_promiscuous(){return rawPromiscuous;}
int esp_ieee802154_get_channel(){return 16;}
int esp_ieee802154_set_channel(uint8_t){return ESP_OK;}
void qs1CaptureFilter(bool){}
int esp_ieee802154_receive(){return ++receiveCalls==failReception?-1:0;}
struct Inv {char invSerial[13]="806000000001";int invType=1;uint8_t transportMode=0;uint16_t transportTag=0;};
Inv Inv_Prop[1];int inverterCount=1,saves=0,normalOps=0;
bool saveOk=true,reply=true,wrongUid=false,wrongPan=false,conflict=false,probeReply=false,savedRoute=false,onlySaved=false;
String stage; bool capturedSaved=false,capturedRestored=false; int traceTx=0;
std::vector<std::vector<uint8_t>> frames;std::vector<uint32_t> sentAt;
std::vector<uint16_t> listenPans;std::vector<bool> filters;
void diagnosticsAppend(String){}void consoleOut(String){}
void empty_serial2(){}void sendNO(){++normalOps;}void checkCoordinator(){}
String ECU_REVERSE(){return "80971B01A3D8";}
bool apsSerialToBcd(const char*s,uint8_t*out){for(int i=0;i<6;++i){char b[3]={s[2*i],s[2*i+1],0};out[i]=strtoul(b,nullptr,16);}return true;}
bool qs1CaptureBegin(const char*,uint16_t){return traceReady;}
void qs1CaptureStage(const char* s,uint16_t){stage=s;}
void qs1CaptureSetProbe(bool value){probe=value;}
void qs1CaptureTx(const uint8_t*,size_t,uint32_t,bool,int){++traceTx;}
void qs1CaptureEnd(bool saved,bool restored){capturedSaved=saved;capturedRestored=restored;}
void qs1CaptureResult(int,uint16_t,const uint8_t*,size_t){}
// BATCH_STUBS_BEGIN
bool qs1BatchBefore(int,const uint8_t*,const uint8_t*,bool &already){already=false;return true;}
bool qs1BatchAfter(int,const uint8_t*,const uint8_t*,bool&,char*,uint16_t&,uint16_t&){return true;}
void pairTransportStop(){}
// BATCH_STUBS_END
void pairAuditStep(PairAuditStage,bool,uint8_t,uint16_t){}
void pairAuditEvent(PairAuditStage,bool,uint8_t,uint16_t,uint16_t,uint16_t,uint16_t,int8_t,uint8_t,bool){}
void pairTraceBegin(const uint8_t*,bool){}void pairTracePause(){}
void pairTraceObserve(const uint8_t*,size_t,uint32_t,int8_t,uint8_t){}
uint16_t pairTraceOmitted(){return 0;}
void pairTraceStage(const char*,uint16_t,uint16_t){}
void pairTraceTx(uint16_t,const uint8_t*,size_t,uint32_t,bool,int){}
void pairTraceResult(bool,uint8_t,bool){}
bool flightRecorderIsEnabled(){return false;}
bool apsRadioLoadPeer(const char*,uint16_t*pan,uint16_t*source){*pan=0x1234;*source=0x4321;return savedRoute;}
bool saveVerifiedPairing(int,const char* id,uint16_t pan,uint16_t source){
 assert(!rawPromiscuous&&!probe);assert(source==0x4321&&!strcmp(id,"2143"));
 assert(pan==(onlySaved?0x1234:0xA3D8));++saves;return saveOk;
}
bool radioTransmit(const uint8_t*b,size_t n,bool,const char*){
 frames.emplace_back(b,b+n);sentAt.push_back(now++);listenPans.push_back(currentPan);filters.push_back(rawPromiscuous);
 ++traceTx;
 return int(frames.size())!=failTx;
}
void delay(unsigned);
'''
code += (root / 'RADIO_TRACE.ino').read_text(encoding='utf-8')
code += function('ZIGBEE_A_TRANSPORT.ino', 'bool rawQs1Reception(')
code += function('ZIGBEE_A_TRANSPORT.ino', 'bool apsSendQs1Pairing(')
code += (root / 'QS1_PAIRING.ino').read_text(encoding='utf-8')
code += r'''
std::vector<uint8_t> hex(const char*s){std::vector<uint8_t>b;for(;*s;s+=2){char t[3]={s[0],s[1],0};b.push_back(strtoul(t,nullptr,16));}return b;}
void delay(unsigned n){
 now+=n;
 if(!(stage=="qs1-verify"||stage=="qs1-saved-network"||(probe&&probeReply)))return;
 if(!reply || (onlySaved&&currentPan!=0x1234))return;
 auto b=hex("23618801D8A3000021434800000021430F0100140101050F1400FF1A8060000000010000");
 b[4]=currentPan;b[5]=currentPan>>8;
 if(wrongUid)b[28]^=1;
 if(wrongPan)b[4]^=1;
 // Unfiltered packets go to the capture only, as in rawWorker.
 if(!probe)radioTraceObserve(b.data(),b.size(),16,-50,10,now);
 if(conflict&&!probe){b[8]=b[14]=0x22;radioTraceObserve(b.data(),b.size(),16,-50,10,now);}
}
void reset(){
 now=100;currentPan=zbOperationalPan;rawRadioStarted=true;rawPromiscuous=probe=false;
 traceReady=true;failTx=receiveCalls=failReception=saves=normalOps=traceTx=0;
 rawMacSequence=rawNwkSequence=rawApsCounter=0;
 reply=saveOk=true;wrongUid=wrongPan=conflict=probeReply=savedRoute=onlySaved=false;
 capturedSaved=capturedRestored=false;frames.clear();sentAt.clear();listenPans.clear();filters.clear();
 Inv_Prop[0].invType=1;pairReceiveStop();
}
int main(){
 reset();assert(pairQs1(0));assert(saves==1&&normalOps==1&&frames.size()==11&&traceTx==11);
 assert(capturedSaved&&capturedRestored&&!rawPromiscuous&&!pairReceiveActive());
 // Synthetic serial, captured MAC/NWK/APS layout. Counters start at 1.
 assert(frames[0]==hex("018801FFFFFFFFD8A300000800FFFF00000F0108140D02050F1401806000000001FFFF10FFFF80971B01A3D8"));
 assert(frames[1]==hex("418802D8A3FFFF00000800FFFF00000D0108140D02050F1401806000000001FFFF10FFFF80971B01A3D8"));
 for(size_t i=0;i<8;i+=2){assert(listenPans[i]==0xA3D8&&!filters[i]);assert(frames[i][11]==8&&frames[i][12]==0);}
 for(size_t i=2;i<8;i+=2)assert(sentAt[i]-sentAt[i-2]==2150);
 for(size_t i=8;i<frames.size();++i)assert(frames[i][0]==0x41&&listenPans[i]==0xA3D8&&!filters[i]);
 for(int mode=0;mode<4;++mode){
  reset();if(mode==0)reply=false;if(mode==1)wrongUid=true;if(mode==2)wrongPan=true;if(mode==3)conflict=true;
  probeReply=true;assert(!pairQs1(0));assert(!saves&&!normalOps&&frames.size()==11);
  assert(!rawPromiscuous&&!probe&&capturedRestored&&!capturedSaved);
 }
 for(int fail=1;fail<=11;++fail){reset();failTx=fail;assert(!pairQs1(0)&&!saves&&!normalOps);assert(!probe&&!rawPromiscuous);}
 reset();failReception=1;assert(!pairQs1(0)&&frames.empty()&&!saves);
 reset();failReception=2;assert(!pairQs1(0)&&!saves&&!rawRadioStarted&&!capturedRestored);
 reset();saveOk=false;assert(!pairQs1(0)&&saves==1&&!normalOps);
 reset();savedRoute=onlySaved=true;assert(pairQs1(0)&&saves==1&&currentPan==zbOperationalPan);
 reset();traceReady=false;assert(!pairQs1(0)&&frames.empty()&&!saves);
 for(int model:{0,2,3}){reset();Inv_Prop[0].invType=model;assert(!pairQs1(0)&&frames.empty());}
 reset();assert(!pairQs1(-1)&&!pairQs1(1));
 uint8_t b[80]={},payload[40]={};
 assert(!qs1PairingFrame(b,1,0xA3D8,0xFFFF,0x020C,payload,6,1,1,1,15));
 assert(qs1PairingFrame(b,sizeof(b),0xFFFF,0xFFFF,0x020C,payload,6,1,1,1,15));
 assert(!qs1PairingFrame(b,sizeof(b),0xFFFF,0xFFFF,0x020D,payload,6,1,1,1,15));
 assert(!qs1PairingFrame(b,sizeof(b),0xA3D8,0xFFFF,0x0006,payload,6,1,1,1,15));
 assert(!qs1PairingFrame(b,sizeof(b),0xA3D8,0xFFFF,0x020C,payload,40,1,1,1,15));
 std::cout<<"PASS QS1 captured headers, operating-PAN copies, timing, identity/PAN validation, log-only probes, saved routes and failure cleanup\n";
}
'''
with tempfile.TemporaryDirectory() as directory:
    p = Path(directory)
    (p/'test.cpp').write_text(code, encoding='utf-8')
    subprocess.run(['g++','-std=c++17','-Wall','-Wextra','-Werror','-fsanitize=undefined','-I',str(root),str(p/'test.cpp'),'-o',str(p/'test')], check=True)
    subprocess.run([str(p/'test')], check=True)
