"""Exercise actual probe orchestration, packet construction and streamed export.

AES primitive is a reversible test double; production key/envelope code is used.
No radio, network, settings or flash writes occur in this harness.
"""
from pathlib import Path
import subprocess, tempfile
root=Path(__file__).resolve().parents[1]
def function(file,signature):
    s=(root/file).read_text(); start=s.index(signature); end=s.index('{',start)+1; depth=1
    while depth:
        depth+=(s[end]=='{')-(s[end]=='}'); end+=1
    return s[start:end]+'\n'
code=r'''
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cctype>
#include <string>
#include <vector>
#include <functional>
#include <memory>
#include "ENCRYPTED_PROBE.h"
using std::min;
struct String:std::string {using std::string::string;String(std::string s):std::string(s){};
 void toCharArray(char* p,size_t n){snprintf(p,n,"%s",c_str());}};
using portMUX_TYPE=int;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(p) (void)(p)
#define portEXIT_CRITICAL(p) (void)(p)
#define VERSION "probe-test"
struct Response {std::function<size_t(uint8_t*,size_t,size_t)> callback;
 void addHeader(const char*,const char*){}};
struct AsyncWebServerRequest {int status=0;std::unique_ptr<Response> response;
 void send(int n,const char*,const char*){status=n;}
 Response* beginChunkedResponse(const char*,std::function<size_t(uint8_t*,size_t,size_t)> cb){response.reset(new Response);response->callback=cb;return response.get();}
 void send(Response*){status=200;}};
struct Inverter {char invSerial[13];char invID[5];bool encrypted;};
Inverter Inv_Prop[2]={{"725000000001","0000",true},{"703000000001","3412",false}};
int inverterCount=2,pendingPairInverter=-1;
uint32_t now=100,panChanges=0;
uint16_t zbOperationalPan=0xA3D8,currentPan=0xA3D8;
bool flightRecorderEnabled=true,pairedActive=false,discoveryReplies=true,conflicting=false,txAck=true,panOk=true;
int txs=0,queries=0,discoveryAfter=0,controls=0,migrateAt=-1,failControl=-1;
bool lostAfterPrepare=false,baselineOperating=false,needsBootstrap=false,operatingOnly=false,bootstrapOk=true;
int bootstraps=0;
uint8_t rawMacSequence=0,rawNwkSequence=0,rawApsCounter=0,rawExtendedAddress[8]={};int rawTxFailure=3;
uint32_t millis(){return now;}
String ECU_REVERSE(){return "80971B01A3D8";}
void consoleOut(String){}
void diagnosticsAppend(String){}
const int PD_ACK_FAIL=0;void pollDiagnosticsCount(int){}
void empty_serial2(){}
bool pairReceiveBegin(const char*,uint16_t){pairedActive=true;return true;}
void pairReceiveStop(){pairedActive=false;}
bool pairReceiveActive(){return pairedActive;}
bool rawRadioSetPromiscuous(bool on){assert(!on);return true;}
bool rawRadioSetPan(uint16_t pan){++panChanges;currentPan=pan;return panOk;}
bool apsUseSpecificPan(uint16_t pan,const char*){return rawRadioSetPan(pan);}
bool apsUsePairingPan(bool yes){return rawRadioSetPan(yes?0xFFFF:zbOperationalPan);}
bool apsRadioLoadPeer(const char* serial,uint16_t* pan,uint16_t* source){assert(serial[1]=='0');*pan=0xA3D8;*source=0x1234;return true;}
bool apsInverterUsesEncryption(int i){return Inv_Prop[i].encrypted;}
bool sendZB(char[]);void delay(unsigned);
bool radioTransmit(const uint8_t*,size_t,bool,const char*);
void esp_fill_random(uint8_t* out,size_t n){for(size_t i=0;i<n;++i)out[i]=0x10+i;}
static const uint8_t APS_AES_KEY_TAIL[4]={0x18,0x28,0x45,0x90};
std::vector<uint8_t> lastKey;
static bool apsEcb(bool,const uint8_t key[16],const uint8_t* in,size_t n,uint8_t* out){
 lastKey.assign(key,key+16);for(size_t i=0;i<n;++i)out[i]=in[i]^key[i%16];return true;}
'''
for sig in ['static uint8_t apsHexNibble(', 'bool apsSerialDefaultsToEncrypted(',
            'static bool apsSerialToBcd(', 'static void apsFrameKey(',
            'static bool apsDecryptTail(', 'bool apsEncryptOutgoing(']:
    code+=function('APS_CRYPTO.ino',sig)
code+=function('INVERTER_INFO.ino','static bool decodeInverterInfoL2(')
code+=function('ZIGBEE_A_TRANSPORT.ino','static void putLe16(')
code+=function('ZIGBEE_A_TRANSPORT.ino','bool apsSendDiagnosticInfo(')
code+=function('ZIGBEE_A_TRANSPORT.ino','static bool sendApsAck(')
code+=(root/'ENCRYPTED_PROBE.ino').read_text()
code+=r'''
std::vector<uint8_t> unhex(const char* s){std::vector<uint8_t> b;for(size_t i=0;i<strlen(s);i+=2)b.push_back(std::stoul(std::string(s+i,2),nullptr,16));return b;}
bool sendZB(char command[]){
 if(!strcmp(command,"24020FFFFFFFFFFFFFFFFF14FFFF140D0200000F1100725000000001FFFF10FFFF80971B01A3D8")) {
   ++bootstraps;assert(currentPan==0xFFFF && queries>=4 && encryptedCapture.phases[3].entered);
   return bootstrapOk;
 }
 if(!strstr(command,"0C0201000F0600")) {
   ++controls;
   assert(currentPan==0xFFFF);
   assert(!strcmp(command,"24020FFFFFFFFFFFFFFFFF14FFFF140F0102000F1100725000000001A3D810FFFF80971B01A3D8") ||
          !strcmp(command,"24020FFFFFFFFFFFFFFFFF14FFFF140D0200000F1100725000000001A3D810FFFF80971B01A3D8") ||
          !strcmp(command,"24020FFFFFFFFFFFFFFFFF14FFFF14010103000F060080971B01A3D8"));
   return controls!=failControl;
 }
 ++queries;assert(strstr(command,"0C0201000F0600725000000001"));
 bool moved=migrateAt>=0 && controls>=migrateAt;
 bool reachable=currentPan==(moved?0xA3D8:0xFFFF) || (baselineOperating && currentPan==0xA3D8);
 if(needsBootstrap && !bootstraps && currentPan==0xFFFF) reachable=false;
 if(operatingOnly) reachable=currentPan==0xA3D8;
 if(lostAfterPrepare && controls>0) reachable=false;
 if(discoveryReplies && queries>discoveryAfter && reachable){
  auto b=unhex("28618872FFFF0000B5D848000000B5D80F2100140101050F1400FF0E72500000000100B8200000B50B");
  b[4]=currentPan; b[5]=currentPan>>8;
  if(moved || operatingOnly || (baselineOperating && currentPan==0xA3D8)) {b[8]=b[14]=0x67;b[9]=b[15]=0x45;}
  encryptedProbeObserve(b.data(),b.size(),now,-65,10);
  if(conflicting){b[8]=b[14]=0xB6;encryptedProbeObserve(b.data(),b.size(),now,-65,10);}
 }
 return true;
}
bool radioTransmit(const uint8_t* f,size_t n,bool cca,const char* reason){
 if(!strcmp(reason,"APS fragment ACK")){assert(cca&&f[0]==0x61&&((n==25&&f[17]==2)||(n==28&&f[17]==0x82)));return true;}
 ++txs; assert(cca && n>=33+19 && (f[0]==0x61 || f[0]==0x41) && f[1]==0x88);
 assert(pairLe16(f+3)==currentPan && pairLe16(f+5)==pairLe16(f+11));
 assert(pairLe16(f+7)==0 && pairLe16(f+13)==0 && (f[25]==0 || f[25]==8) && f[26]==0x14);
 assert(pairLe16(f+27)==6 && pairLe16(f+29)==0x0F05 && f[31]==0x14);
 if(pairLe16(f+5)==0x1234) {
   auto info=unhex("703000000001FBFB09DC0200050000CB00000000FEFE");
   assert(encryptedProbeAsdu(currentPan,0x1234,0x0106,info.data(),info.size(),now));
 }
 return txAck;
}
void delay(unsigned ms){assert(ms==3500 || ms==6000 || ms==1000 || ms==5000 || ms==10000);now+=ms;}
void reset(){
 assert(!encryptedProbeReaders);txs=queries=panChanges=0;now=100;flightRecorderEnabled=discoveryReplies=txAck=panOk=true;
 bootstraps=0;bootstrapOk=true;needsBootstrap=operatingOnly=false;discoveryAfter=controls=0;migrateAt=failControl=-1;lostAfterPrepare=baselineOperating=false;conflicting=false;currentPan=0xA3D8;pairedActive=false;
}
int main(){
 assert(sendApsAck(0xFFFF,0xD8B5,0xD8B5,0x14,0x14,6,0x0F05,1,0,0));
 assert(sendApsAck(0xFFFF,0xD8B5,0xD8B5,0x14,0x14,6,0x0F05,1,1,0));
 uint8_t out[64],uid[6];size_t n=0;pairSerialBytes(Inv_Prop[0].invSerial,uid);
 for(uint8_t mode=0;mode<6;++mode){
  assert(encryptedProbePayload(0,mode,out,sizeof(out),&n));
  assert(n==(mode==0?19:mode==1?28:mode==2||mode==4?29:23));
  size_t offset=mode==3||mode==5?0:6;
  if(mode>=2)assert(out[offset]==(mode>=4?0xA0:0xA1));
  if(mode){assert(lastKey.size()==16);for(int i=0;i<6;++i)assert(lastKey[6+i]==(mode>=4?0:uid[i]));}
  else assert(!memcmp(out+6,unhex("FBFB06DC000000000000E2FEFE").data(),13));
 }
 // Decode actual info layouts without altering persistent/inverter state.
 auto info=unhex("FBFB09DC0200050000CB00000000FEFE");
 char version[40];uint8_t model=0;
 assert(encryptedProbeDecode(uid,info.data(),info.size(),version,sizeof(version),&model));
 assert(!strcmp(version,"5.203")&&model==2);
 for(bool prefix:{false,true}) for(bool marker:{false,true}) {
   uint8_t nonce[6]={1,2,3,4,5,6},key[16],padded[32]={},cipher[32];
   padded[0]=info.size();memcpy(padded+1,info.data(),info.size());apsFrameKey(nonce,uid,key);
   apsEcb(true,key,padded,sizeof(padded),cipher);
   std::vector<uint8_t> wire;if(prefix)wire.insert(wire.end(),uid,uid+6);if(marker)wire.push_back(0xA1);
   wire.insert(wire.end(),nonce,nonce+6);wire.insert(wire.end(),cipher,cipher+32);
   assert(encryptedProbeDecode(uid,wire.data(),wire.size(),version,sizeof(version),&model));
 }
 assert(encryptedProbePayload(0,7,out,sizeof(out),&n) && n==19);
 assert(!memcmp(out+6,unhex("FBFB06BB000000000000C1FEFE").data(),13));
 assert(!encryptedProbePayload(0,7,out,18,&n));
 assert(!encryptedProbePayload(0,6,out,sizeof(out),&n));
 assert(!encryptedProbePayload(0,2,out,4,&n));
 reset();assert(encryptedProbeRun(0));
 assert(controls==8 && queries==20 && txs==28 && now<300100);
 assert(currentPan==0xA3D8&&!pairedActive&&!strcmp(Inv_Prop[0].invID,"0000"));
 assert(encryptedCapture.discovered==0xD8B5&&!encryptedCapture.conflict && !encryptedCapture.operatingSource);
 assert(!encryptedCapture.phases[1].entered&&!encryptedCapture.phases[2].entered);
 assert(!encryptedCapture.phases[31].entered && encryptedCapture.phases[33].entered);
 for(int phase:{3,4,5,6,7,8,13,18,19,23,24,29,30,33}) {
  const auto &p=encryptedCapture.phases[phase]; bool telemetry=phase==5||phase==7||phase==19||phase==24||phase==30;
  assert(p.entered && p.mode==(telemetry?7:0)); assert(p.broadcast==(phase==7));
  assert(p.pan==((phase==3||phase==33)?0xA3D8:0xFFFF));
  for(int request=0;request<2;++request) {
   assert(p.txLength[request]==19 && p.tx[request][9]==(telemetry?0xBB:0xDC));
   assert(p.tx[request][16]==(telemetry?0xC1:0xE2));
  }
  assert(p.txMs[1]-p.txMs[0]==(telemetry?6000:3500));
 }
 for(int phase:{10,14,15,20,25,26}) assert(encryptedCapture.phases[phase].entered);
 assert(encryptedCapture.phases[10].txLength[0]==17 && encryptedCapture.phases[20].txLength[0]==17);
 assert(encryptedCapture.phases[14].sent==1 && encryptedCapture.phases[15].sent==2);
 {AsyncWebServerRequest request; encryptedProbeDownload(&request);assert(request.status==200&&encryptedProbeBusy());
  assert(!encryptedProbeRun(0));std::string text;uint8_t b[7];size_t read;
  while((read=request.response->callback(b,sizeof(b),text.size())))text.append((char*)b,read);
  assert(text.find("phase=33 entered=1")!=std::string::npos && text.find("phase=0 entered=1")!=std::string::npos);
 }
 assert(!encryptedProbeBusy());
 reset();discoveryReplies=false;assert(!encryptedProbeRun(0)&&txs==4&&queries==8&&bootstraps==1&&!pairedActive&&currentPan==0xA3D8);
 reset();discoveryAfter=4;assert(encryptedProbeRun(0)&&queries==22&&txs==28&&bootstraps==1);
 assert(encryptedCapture.phases[2].entered);
 reset();conflicting=true;assert(!encryptedProbeRun(0)&&txs==4&&bootstraps==0);
 reset();txAck=false;assert(encryptedProbeRun(0)&&txs==28); // no ACK is an outcome, not suite failure
 assert(!encryptedCapture.phases[5].txOk[0]);
 // Regression: a target silent until normal pairing's serial bootstrap must
 // enter the full combined investigation in this same run.
 reset();needsBootstrap=true;assert(encryptedProbeRun(0)&&bootstraps==1&&controls==8);
 assert(encryptedCapture.phases[0].foundSource==0 && encryptedCapture.phases[2].foundSource==0xD8B5);
 assert(encryptedCapture.phases[1].txLength[0]==17 && encryptedCapture.phases[1].tx[0][6]==0xFF && encryptedCapture.phases[1].tx[0][7]==0xFF);
 assert(encryptedCapture.phases[3].asdus==2 && encryptedCapture.phases[33].asdus==2);
 assert(encryptedCapture.phases[3].started<encryptedCapture.phases[0].started);
 reset();operatingOnly=true;assert(encryptedProbeRun(0)&&bootstraps==0&&controls==0);
 assert(encryptedCapture.operatingSource==0x4567 && encryptedCapture.phases[32].entered && !encryptedCapture.phases[4].entered);
 reset();operatingOnly=true;discoveryAfter=4;assert(encryptedProbeRun(0)&&bootstraps==1&&controls==0);
 assert(encryptedCapture.phases[9].foundSource==0 && encryptedCapture.phases[37].foundSource==0x4567);
 assert(encryptedCapture.phases[32].entered && !encryptedCapture.phases[10].entered);
 reset();needsBootstrap=true;bootstrapOk=false;assert(!encryptedProbeRun(0)&&bootstraps==1&&controls==0&&queries==4);
 assert(encryptedCapture.phases[33].asdus==2 && !pairedActive && currentPan==0xA3D8);
 reset();discoveryReplies=false;assert(!encryptedProbeRun(0)&&controls==0&&bootstraps==1);
 assert(encryptedCapture.phases[3].asdus==2 && encryptedCapture.phases[33].asdus==2 && !encryptedCapture.phases[10].entered);
 reset();flightRecorderEnabled=false;assert(!encryptedProbeRun(0)&&!txs&&!queries);
 reset();assert(!encryptedProbeRun(1)&&!txs&&!queries);assert(!encryptedProbeRun(-1));
 reset();panOk=false;assert(!encryptedProbeRun(0)&&!encryptedCapture.restored&&!pairedActive);
 // Migration after prepare, after its commit, after directed assignment, or
 // after its commit must stop all subsequent writes, use the new address, and
 // run plaintext/native/A1 telemetry tests without persisting pairing.
 for(int step:{1,4,5,8}) {
   reset();migrateAt=step;assert(encryptedProbeRun(0));assert(controls==step);
   assert(encryptedCapture.operatingSource==0x4567);
   for(int phase:{31,32,34,35,36}) {
     assert(encryptedCapture.phases[phase].entered && encryptedCapture.phases[phase].source==0x4567);
     assert(encryptedCapture.phases[phase].pan==0xA3D8);
   }
   assert(!strcmp(Inv_Prop[0].invID,"0000") && Inv_Prop[0].encrypted);
 }
 reset();baselineOperating=true;assert(encryptedProbeRun(0)&&controls==0&&encryptedCapture.operatingSource==0x4567);
 reset();failControl=2;assert(!encryptedProbeRun(0)&&controls==2&&currentPan==0xA3D8&&!pairedActive);
 reset();lostAfterPrepare=true;assert(!encryptedProbeRun(0)&&controls==4); // do not start directed trial without fresh FFFF identity
 reset();strcpy(Inv_Prop[0].invID,"3412");assert(!encryptedProbeRun(0)&&controls==0&&queries==0);strcpy(Inv_Prop[0].invID,"0000");
 // Prove BB encryption bodies by undoing the reversible crypto test double.
 for(int mode:{11,12}) {
   assert(encryptedProbePayload(0,mode,out,sizeof(out),&n));
   size_t offset=mode==11?12:13;
   assert(n==offset+16);
   uint8_t clear[16];for(size_t i=0;i<16;++i)clear[i]=out[offset+i]^lastKey[i];
   assert(clear[0]==13 && !memcmp(clear+1,unhex("FBFB06BB000000000000C1FEFE").data(),13));
 }
 // Cross-network discovery is rejected, and two sources in an operating
 // discovery window stop the experiment rather than authorize writes.
 encryptedCapture.begin(uid,100);encryptedCapture.stage(9,0xA3D8,0,6,100);
 auto discovery=unhex("28618872FFFF0000B5D848000000B5D80F2100140101050F1400FF0E72500000000100B8200000B50B");
 encryptedProbeObserve(discovery.data(),discovery.size(),101,-60,10);assert(!encryptedCapture.operatingSource);
 discovery[4]=0xD8;discovery[5]=0xA3;
 encryptedProbeObserve(discovery.data(),discovery.size(),102,-60,10);assert(encryptedCapture.operatingSource==0xD8B5);
 discovery[8]=discovery[14]=0xB6;
 encryptedProbeObserve(discovery.data(),discovery.size(),103,-60,10);assert(probeOperatingSeen()&&!probeFound(9)&&encryptedCapture.conflict);
 // Capture address-matched ciphertext without any visible UID, including fragments.
 encryptedCapture.begin(uid,100);encryptedCapture.stage(2,0xFFFF,0xD8B5,1,100);
 auto raw=unhex("28618872FFFF0000B5D848000000B5D80F2100140101050F1400FF0E72500000000100B8200000B50B");
 std::fill(raw.begin()+26,raw.end(),0xAB);
 for(int i=0;i<30;++i)encryptedProbeObserve(raw.data(),raw.size(),100+i,-60,10);
 assert(encryptedCapture.phases[2].related==30&&encryptedCapture.phases[2].packets[0].ms==0);
 assert(encryptedProbeAcceptFrame(raw.data(),raw.size(),100));
 raw[8]=0x34;raw[9]=0x12;assert(encryptedProbeAcceptFrame(raw.data(),raw.size(),100));
 raw[8]=raw[9]=0;assert(!encryptedProbeAcceptFrame(raw.data(),raw.size(),100));raw[8]=0xB5;raw[9]=0xD8;
 raw[14]=0;assert(!encryptedProbeAcceptFrame(raw.data(),raw.size(),100));raw[14]=0xB5;
 assert(!encryptedProbeAcceptFrame(raw.data(),raw.size(),99));
 uint8_t answer[300]={};for(int i=0;i<5;++i)assert(encryptedProbeAsdu(0xFFFF,0xD8B5,6,answer,sizeof(answer),101+i));
 assert(encryptedCapture.phases[2].asdus==5&&encryptedCapture.phases[2].answers[0].ms==1&&encryptedCapture.phases[2].answers[1].ms==5);
 assert(!encryptedProbeAsdu(0xA3D8,0xD8B5,6,answer,sizeof(answer),110));
 {AsyncWebServerRequest request;encryptedProbeDownload(&request);assert(request.status==409);}
 encryptedCapture.active=false;
 // Export maximum-length ASDUs completely, without building a giant String.
 char line[900];size_t len=encryptedProbeLogLine(5+2*9+7,line,sizeof(line));assert(len<sizeof(line));
 assert(strstr(line,"bytes=300 data="));
 printf("Capture RAM: %zu bytes\n",sizeof(ProbeCapture));
 puts("PASS staged assignment/commit investigation: exact DC/key/layouts, explicit unicast, fresh/conflicting discovery, bounded capture, no-ACK outcomes, restoration, immutable streamed export");
}
'''
with tempfile.TemporaryDirectory() as d:
    p=Path(d);(p/'test.cpp').write_text(code)
    subprocess.run(['g++','-std=c++11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I',str(root),str(p/'test.cpp'),'-o',str(p/'test')],check=True)
    subprocess.run([str(p/'test')],check=True)
