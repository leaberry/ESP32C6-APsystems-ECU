"""Replay real pairing, receive ownership and optional tracing. No RF or flash.

AES primitive is a reversible host double, not evidence of AES interoperability.
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
#include "PAIRING_SESSION.h"
#include "PAIRING_TRACE.h"
#include "APS_TRANSPORT_MODE.h"
#include "PAIRING_AUDIT.h"
using std::min;
struct String:std::string {using std::string::string;String(std::string s):std::string(s){};
 void toCharArray(char* p,size_t n){snprintf(p,n,"%s",c_str());}};
using portMUX_TYPE=int;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(p) (void)(p)
#define portEXIT_CRITICAL(p) (void)(p)
#define VERSION "pairing-test"
struct Response {std::function<size_t(uint8_t*,size_t,size_t)> callback;
 void addHeader(const char*,const char*){}};
bool failResponse=false;
struct AsyncWebServerRequest {int status=0;std::unique_ptr<Response> response;
 void send(int n,const char*,const char*){status=n;}
 Response* beginChunkedResponse(const char*,std::function<size_t(uint8_t*,size_t,size_t)> cb){if(failResponse)return nullptr;response.reset(new Response);response->callback=cb;return response.get();}
 void send(Response*){status=200;}};
// INVERTER_RECORD
inverters Inv_Prop[2];
int inverterCount=2;
uint32_t now=100,panChanges=0;
uint16_t zbOperationalPan=0xA3D8,currentPan=0xA3D8;
bool flightRecorderEnabled=true,pairedActive=false,conflicting=false,panOk=true;
int txs=0;
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
extern bool restoreOk; bool apsUsePairingPan(bool yes){return rawRadioSetPan(yes?0xFFFF:zbOperationalPan)&&restoreOk;}
bool apsRadioLoadPeer(const char* serial,uint16_t* pan,uint16_t* source){assert(serial[1]=='0');*pan=0xA3D8;*source=0x1234;return true;}

bool sendZB(char[]);void delay(unsigned);
bool radioTransmit(const uint8_t*,size_t,bool,const char*);
void esp_fill_random(uint8_t* out,size_t n){for(size_t i=0;i<n;++i)out[i]=0x10+i;}
static const uint8_t APS_AES_KEY_TAIL[4]={0x18,0x28,0x45,0x90};
std::vector<uint8_t> lastKey;
static bool apsEcb(bool,const uint8_t key[16],const uint8_t* in,size_t n,uint8_t* out){
 lastKey.assign(key,key+16);for(size_t i=0;i<n;++i)out[i]=in[i]^key[i%16];return true;}
'''
includes=(root/'AAA_INCLUDES.h').read_text()
end=includes.index('} inverters;')+len('} inverters;')
start=includes.rfind('typedef struct',0,end)
code=code.replace('// INVERTER_RECORD',includes[start:end])
for sig in ['static uint8_t apsHexNibble(', 'bool apsSerialDefaultsToEncrypted(',
            'static bool apsSerialToBcd(', 'static void apsFrameKey(',
            'static bool apsDecryptTail(', 'bool apsEncryptOutgoing(', 'bool apsInverterUsesEncryption(']:
    code+=function('APS_CRYPTO.ino',sig)
code+=(root/'PAIRING_TRACE.ino').read_text()
code+=(root/'PAIRING_SESSION.ino').read_text()
code+=function('ZIGBEE_A_TRANSPORT.ino','static void putLe16(')
code+=function('ZIGBEE_A_TRANSPORT.ino','bool apsSendPairingQuery(')
code+=function('APS_CRYPTO.ino','int apsFindInverter(')
code+=function('APS_CRYPTO.ino','bool apsDecryptIncoming(')
code+=r'''
bool startOk=true,restoreOk=true,saveOk=true;
int saves=0,normalOps=0,discoveries=0,assignments=0,bootstrapCount=0;
int moveAfter=5,failCommand=-1,modeReplies=2,badReply=0;
bool alreadyOperating=false,missing=false,needBootstrap=false,noAck=false;
std::vector<std::string> commands;
bool overflowTrace=false; bool radioTraceBegin(){uint8_t uid[6];pairSerialBytes(Inv_Prop[0].invSerial,uid);pairTraceBegin(uid,flightRecorderEnabled);if(overflowTrace)for(int i=0;i<30;++i)pairTraceStage("filled",0,0);return startOk;}
void radioTraceEnd(){pairTracePause();}
void pairAuditStep(PairAuditStage,bool,uint8_t,uint16_t){}
void sendNO(){++normalOps;}
void checkCoordinator(){}
bool saveVerifiedPairingMode(int which,const char *id,uint16_t pan,uint16_t source,uint8_t mode){
 ++saves;assert(pan==zbOperationalPan&&source==0x4567&&mode!=APS_TRANSPORT_AUTO);
 if(!saveOk)return false;
 snprintf(Inv_Prop[which].invID,5,"%s",id);
 Inv_Prop[which].transportMode=mode;Inv_Prop[which].transportTag=APS_TRANSPORT_TAG;
 return true;
}
'''
code+=(root/"PAIRING_FALLBACK.ino").read_text()
code+=r'''
std::vector<uint8_t> hex(const char *s){std::vector<uint8_t> v;for(size_t i=0;i<strlen(s);i+=2)v.push_back(std::stoul(std::string(s+i,2),nullptr,16));return v;}
// Reporter's two phase-32 ASDUs, with only the UID anonymized. Their checksum
// covers bytes after the UID and remains the exact captured DS3 checksum.
const char *packets[]={
 "725000000001FBFB5CBBBB2000050162FFFF0000000000008000064501E0004400010371138B023A00170030FFFF055207440001A8990000010106014DC7FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF0132B8FEFE",
 "725000000001FBFB5CBBBB2000050162FFFF0000000000008000063801DF004500010372138C024000140035FFFF055407440001B29B0000010606014DC7FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF0132C8FEFE"};
bool sendZB(char command[]){
 commands.push_back(command);
 if((int)commands.size()==failCommand)return false;
 bool moved=alreadyOperating || (moveAfter>0&&assignments>=moveAfter);
 if(strstr(command,"0C0201000F0600")){
   ++discoveries;
   if(missing || (needBootstrap&&!bootstrapCount&&!moved) || currentPan!=(moved?0xA3D8:0xFFFF))return true;
   auto raw=hex("28618872FFFF0000B5D848000000B5D80F2100140101050F1400FF0E72500000000100B8200000B50B");
   raw[4]=currentPan;raw[5]=currentPan>>8;
   if(moved){raw[8]=raw[14]=0x67;raw[9]=raw[15]=0x45;}
   pairTransportObserve(raw.data(),raw.size(),now);pairTraceObserve(raw.data(),raw.size(),now,-65,10);
   if(conflicting){raw[8]=raw[14]=0xB6;pairTransportObserve(raw.data(),raw.size(),now);pairTraceObserve(raw.data(),raw.size(),now,-65,10);}
 }else{
   assert(currentPan==0xFFFF);
   if(strstr(command,"725000000001FFFF10FFFF")){
     ++bootstrapCount;assert(discoveries>=4);
   }else{
     ++assignments;
     assert(!strcmp(command,"24020FFFFFFFFFFFFFFFFF14FFFF140F0102000F1100725000000001A3D810FFFF80971B01A3D8") ||
            !strcmp(command,"24020FFFFFFFFFFFFFFFFF14FFFF140D0200000F1100725000000001A3D810FFFF80971B01A3D8") ||
            !strcmp(command,"24020FFFFFFFFFFFFFFFFF14FFFF14010103000F060080971B01A3D8"));
   }
 }
 return true;
}
bool radioTransmit(const uint8_t *f,size_t n,bool cca,const char*){
 ++txs;assert(cca&&n>=52&&pairLe16(f+3)==zbOperationalPan&&pairLe16(f+5)==0x4567);
 bool aes=!(f[39]==0xFB&&f[40]==0xFB);
 // The plaintext path must run only after two AES attempts.
 if(!aes)assert(txs>=3);
 uint8_t repeat=(txs-1)%2;
 if((modeReplies==1&&aes)||(modeReplies==2&&!aes)||(modeReplies==3)){
   if(badReply==7&&repeat==1)return !noAck;
   auto data=hex(packets[repeat]);
   if(badReply==1)data[101]^=1; // checksum
   if(badReply==2)data[0]^=1; // serial
   if(badReply==3)data=hex("725000000001FBFB09DC0200050000CB00000000FEFE");
   if(aes && badReply!=9){
     uint8_t out[300];size_t len=0;assert(apsEncryptOutgoing(0,data.data(),data.size(),out,sizeof(out),&len,false));
     data.assign(out,out+len);
   }
   uint16_t pan=badReply==4?0xFFFF:zbOperationalPan,src=badReply==5?0xD8B5:0x4567;
   pairTransportAsdu(pan,src,badReply==6?0x0101:0x0106,data.data(),data.size(),badReply==8?now-1:now+1);
 }
 return !noAck;
}
void delay(unsigned ms){now+=ms;}
void reset(){
 pairTransportStop();assert(!pairTraceReaders);assert(pairingTraceClear());overflowTrace=false;
 Inv_Prop[0]=inverters{};Inv_Prop[1]=inverters{};
 snprintf(Inv_Prop[0].invSerial,13,"725000000001");Inv_Prop[0].invType=2;
 snprintf(Inv_Prop[1].invSerial,13,"703000000001");
 saves=normalOps=discoveries=assignments=bootstrapCount=txs=0;commands.clear();now=100;
 startOk=restoreOk=saveOk=panOk=true;alreadyOperating=missing=needBootstrap=noAck=conflicting=false;
 moveAfter=5;failCommand=-1;modeReplies=2;badReply=0;currentPan=zbOperationalPan;rawTxFailure=3;
}
void failed(){
 assert(!pairWithTransportFallback(0));assert(!normalOps&&!strcmp(Inv_Prop[0].invID,"0000"));
 assert(apsInverterUsesEncryption(0)&&!pairedActive&&!pairTrace.saved&&currentPan==zbOperationalPan);
}
bool sawStage(const char* name){for(int i=0;i<pairTrace.count;++i)if(!strcmp(name,pairTrace.stages[i].name))return true;return false;}
int main(){
 // Binary compatibility and auto migration. Old bool/padding cannot silently
 // override serial defaults without a matching tag and a supported mode.
 static_assert(sizeof(inverters)==52 && offsetof(inverters,transportMode)==49 && offsetof(inverters,transportTag)==50,"record ABI");
 reset();assert(apsInverterUsesEncryption(0)&&!apsInverterUsesEncryption(1));
 for(int tag:{0,0xFFFF,1})for(int mode:{0,1,2,255}){
   Inv_Prop[0].transportMode=mode;Inv_Prop[0].transportTag=tag;assert(apsInverterUsesEncryption(0));
 }
 reset();assert(pairWithTransportFallback(0));
 assert(assignments==5&&saves==1&&normalOps==1&&!strcmp(Inv_Prop[0].invID,"6745"));
 assert(!apsInverterUsesEncryption(0)&&!pairedActive&&currentPan==zbOperationalPan);
 assert(pairTrace.saved&&pairTrace.mode==APS_TRANSPORT_PLAIN);
 assert(sawStage("directed-PAN")&&sawStage("verify-AES")&&sawStage("verify-plain"));
 // Simulated reboot from the exact persisted struct bytes retains plaintext.
 inverters disk=Inv_Prop[0];Inv_Prop[0]=inverters{};memcpy(&Inv_Prop[0],&disk,sizeof(disk));
 Inv_Prop[0].transportMode=apsStoredTransportMode(Inv_Prop[0].transportMode,Inv_Prop[0].transportTag);
 assert(!apsInverterUsesEncryption(0));
 // Unsolicited AES cannot revert the explicit mode.
 auto plain=hex(packets[0]);uint8_t wire[300],decoded[300];size_t n=0,decodedLen=0;
 assert(apsEncryptOutgoing(0,plain.data(),plain.size(),wire,sizeof(wire),&n,false));
 assert(apsDecryptIncoming(0x4567,wire,n,decoded,sizeof(decoded),&decodedLen,nullptr));
 assert(!apsInverterUsesEncryption(0));
 // Already moved units skip every assignment. Recorder off still pairs.
 reset();alreadyOperating=true;flightRecorderEnabled=false;assert(pairWithTransportFallback(0));
 assert(!assignments&&!bootstrapCount&&saves==1&&txs==4);flightRecorderEnabled=true;
 reset();alreadyOperating=true;modeReplies=1;assert(pairWithTransportFallback(0));
 assert(saves==1&&txs==2&&apsInverterUsesEncryption(0)&&!sawStage("verify-plain"));
 for(int migration:{1,4}){reset();moveAfter=migration;modeReplies=1;assert(pairWithTransportFallback(0));assert(assignments==migration&&!sawStage("directed-PAN"));}
 reset();needBootstrap=true;assert(pairWithTransportFallback(0)&&bootstrapCount==1);
 reset();noAck=true;assert(pairWithTransportFallback(0));
 reset();noAck=true;rawTxFailure=42;failed();assert(!sawStage("verify-plain"));
 reset();missing=true;failed();assert(!assignments);
 reset();moveAfter=99;failed();assert(assignments==5&&!saves);
 reset();conflicting=true;failed();assert(!assignments&&!saves);
 for(int bad=1;bad<=8;++bad){reset();alreadyOperating=true;badReply=bad;failed();assert(!saves);}
 // Plain replies to AES requests cannot falsely prove AES support.
 reset();alreadyOperating=true;modeReplies=3;badReply=9;assert(pairWithTransportFallback(0));
 assert(Inv_Prop[0].transportMode==APS_TRANSPORT_PLAIN);
 reset();alreadyOperating=true;restoreOk=false;failed();assert(!saves);
 reset();saveOk=false;failed();assert(saves==1);
 reset();startOk=false;failed();assert(!assignments&&!saves);
 // Every failed discovery/assignment transmission aborts without persistence.
 reset();assert(pairWithTransportFallback(0));int total=commands.size();
 for(int failure=1;failure<=total;++failure){reset();failCommand=failure;failed();assert(!saves);}
 // Capture overflow and a slow download must not affect the actual outcome.
 reset();alreadyOperating=true;overflowTrace=true;assert(pairWithTransportFallback(0)&&saves==1&&pairTrace.omittedStages>0);
 reset();alreadyOperating=true;assert(pairWithTransportFallback(0));
 AsyncWebServerRequest request;pairingTraceDownload(&request);assert(request.status==200&&pairTraceReaders==1);
 AsyncWebServerRequest second;pairingTraceDownload(&second);assert(second.status==409&&pairTraceReaders==1);
 const auto previousStarted=pairTrace.started;assert(!pairingTraceClear());
 assert(pairWithTransportFallback(0)&&saves==2&&pairTrace.started==previousStarted);
 std::string text;uint8_t chunk[7];size_t count;
 while((count=request.response->callback(chunk,sizeof(chunk),0)))text.append((char*)chunk,count);
 assert(text.find("verify-plain")!=std::string::npos&&text.find("bytes=105")!=std::string::npos);
 request.response.reset();assert(!pairTraceReaders&&pairingTraceClear()&&!pairTrace.count);
 failResponse=true;AsyncWebServerRequest failedDownload;pairingTraceDownload(&failedDownload);assert(failedDownload.status==503&&!pairTraceReaders);failResponse=false;
 // Each request owns a separate receive window. Never freshen an old packet.
 reset();uint8_t uid[6];pairSerialBytes("725000000001",uid);pairTransportBegin(uid,zbOperationalPan);
 pairTransportStage(zbOperationalPan,0x4567,false);
 auto routed=hex("28618872D8A3000067454800000067450F2100140101050F1400FF0E72500000000100B8200000B50B");
 assert(pairTransportAcceptFrame(routed.data(),routed.size(),now));
 routed[14]=routed[15]=0;assert(!pairTransportAcceptFrame(routed.data(),routed.size(),now));
 pairTransportWaitForReply();
 auto data=hex(packets[0]);assert(!pairTransportAsdu(0xFFFF,0x4567,0x0106,data.data(),data.size(),now));
 assert(!pairTransportAsdu(zbOperationalPan,0x4567,0x0106,data.data(),data.size(),now-1));
 assert(pairTransportAsdu(zbOperationalPan,0x4567,0x0106,data.data(),data.size(),now));
 PairPowerReply reply;assert(pairTransportTakeReply(reply));now+=6000;pairTransportWaitForReply();
 assert(!pairTransportTakeReply(reply));pairTransportStop();
 printf("Functional state=%zu bytes; optional trace=%zu bytes\n",sizeof(PairTransportSession),sizeof(PairTraceCapture));
 reset();Inv_Prop[0].invType=0;assert(!pairWithTransportFallback(0)&&!discoveries);
 reset();assert(!pairWithTransportFallback(-1)&&!pairWithTransportFallback(2));
 puts("PASS actual fallback: encrypted first, directed PAN, valid captured telemetry, persistence gates, legacy modes, restoration and failure paths");
}
'''
with tempfile.TemporaryDirectory() as directory:
    p = Path(directory)
    (p / 'test.cpp').write_text(code)
    subprocess.run(['g++', '-std=c++11', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=address,undefined', '-I', str(root),
                    str(p / 'test.cpp'), '-o', str(p / 'test')], check=True)
    subprocess.run([str(p / 'test')], check=True)
