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
int txs=0,queries=0,discoveryAfter=0;
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
 ++queries;assert(strstr(command,"0C0201000F0600725000000001"));
 if(discoveryReplies && queries>discoveryAfter){
  auto b=unhex("28618872FFFF0000B5D848000000B5D80F2100140101050F1400FF0E72500000000100B8200000B50B");
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
 return txAck;
}
void delay(unsigned ms){assert(ms==3500 || ms==6000);now+=ms;}
void reset(){
 assert(!encryptedProbeReaders);txs=queries=panChanges=0;now=100;flightRecorderEnabled=discoveryReplies=txAck=panOk=true;
 discoveryAfter=0;conflicting=false;currentPan=0xA3D8;pairedActive=false;
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
 reset();assert(encryptedProbeRun(0));assert(txs==18&&queries==4&&now==92100);
 assert(currentPan==0xA3D8&&!pairedActive&&!strcmp(Inv_Prop[0].invID,"0000"));
 assert(encryptedCapture.discovered==0xD8B5&&!encryptedCapture.conflict);
 assert(encryptedCapture.phases[0].broadcast&&encryptedCapture.phases[11].broadcast);
 assert(!encryptedCapture.phases[5].broadcast&&encryptedCapture.phases[7].broadcast);
 for(int phase=3;phase<13;++phase) {
  if(phase==11)continue;
  const auto &p=encryptedCapture.phases[phase];
  bool telemetry=phase==5||phase==7||phase==9;
  assert(p.mode==(telemetry?7:0));
  assert(p.broadcast==(phase==7));
  assert(p.pan==((phase==3||phase==9||phase==12)?0xA3D8:0xFFFF));
  assert(p.source==((phase==3||phase==12)?0x1234:0xD8B5));
  for(int request=0;request<2;++request) {
    assert(p.txLength[request]==19);
    assert(p.tx[request][9]==(telemetry?0xBB:0xDC));
    assert(p.tx[request][16]==(telemetry?0xC1:0xE2));
  }
  assert(p.txMs[1]-p.txMs[0]==(telemetry?6000:3500));
 }
 for(int i=3;i<13;++i)assert(encryptedCapture.phases[i].entered&&encryptedCapture.phases[i].sent==2);
 assert(!encryptedCapture.phases[1].entered&&!encryptedCapture.phases[2].entered);
 {AsyncWebServerRequest request; encryptedProbeDownload(&request);assert(request.status==200&&encryptedProbeBusy());
  assert(!encryptedProbeRun(0));std::string text;uint8_t b[7];size_t read;
  while((read=request.response->callback(b,sizeof(b),text.size())))text.append((char*)b,read);
  assert(text.find("phase=12 entered=1")!=std::string::npos && text.find("phase=0 entered=1")!=std::string::npos);
 }
 assert(!encryptedProbeBusy());
 reset();discoveryReplies=false;assert(!encryptedProbeRun(0)&&txs==0&&queries==6&&!pairedActive&&currentPan==0xA3D8);
 reset();discoveryAfter=4;assert(encryptedProbeRun(0)&&queries==8&&txs==18);
 assert(encryptedCapture.phases[2].entered);
 reset();conflicting=true;assert(!encryptedProbeRun(0)&&txs==0);
 reset();txAck=false;assert(encryptedProbeRun(0)&&txs==18); // no ACK is an outcome, not suite failure
 assert(!encryptedCapture.phases[5].txOk[0]);
 reset();flightRecorderEnabled=false;assert(!encryptedProbeRun(0)&&!txs&&!queries);
 reset();assert(!encryptedProbeRun(1)&&!txs&&!queries);assert(!encryptedProbeRun(-1));
 reset();panOk=false;assert(!encryptedProbeRun(0)&&!encryptedCapture.restored&&!pairedActive);
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
 char line[900];size_t len=encryptedProbeLogLine(5+2*11+9,line,sizeof(line));assert(len<sizeof(line));
 assert(strstr(line,"bytes=300 data="));
 puts("PASS plaintext telemetry and read-only probes: exact DC/key/layouts, explicit unicast, fresh/conflicting discovery, bounded capture, no-ACK outcomes, restoration, immutable streamed export");
}
'''
with tempfile.TemporaryDirectory() as d:
    p=Path(d);(p/'test.cpp').write_text(code)
    subprocess.run(['g++','-std=c++11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I',str(root),str(p/'test.cpp'),'-o',str(p/'test')],check=True)
    subprocess.run([str(p/'test')],check=True)
