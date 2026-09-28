"""Replay production pairing fallback and captured DS3-H telemetry without RF.

The AES primitive is the shared reversible host double, not an interoperability
claim. Packet envelopes, validation, orchestration and mode selection are real.
"""
from pathlib import Path
import subprocess
import tempfile
from test_encrypted_probe import base_code, function, root

code = base_code.replace('#include "ENCRYPTED_PROBE.h"',
                         '#include "ENCRYPTED_PROBE.h"\n#include "APS_TRANSPORT_MODE.h"\n#include "PAIRING_AUDIT.h"')
code = code.replace(function('ZIGBEE_A_TRANSPORT.ino', 'static bool sendApsAck('), '')
includes = (root / 'AAA_INCLUDES.h').read_text()
end = includes.index('} inverters;') + len('} inverters;')
start = includes.rfind('typedef struct', 0, end)
start_fake = code.index('struct Inverter {')
end_fake = code.index('int inverterCount=', start_fake)
code = code[:start_fake] + includes[start:end] + '\ninverters Inv_Prop[2];\n' + code[end_fake:]
code = code.replace('bool apsInverterUsesEncryption(int i){return Inv_Prop[i].encrypted;}', '')
offset = code.index('#include "ENCRYPTED_PROBE.h"', code.index('bool apsSendDiagnosticInfo('))
code = code[:offset] + function('APS_CRYPTO.ino', 'bool apsInverterUsesEncryption(') + code[offset:]
code += function('APS_CRYPTO.ino', 'int apsFindInverter(')
code += function('APS_CRYPTO.ino', 'bool apsDecryptIncoming(')
code += r'''
bool startOk=true,restoreOk=true,saveOk=true;
int saves=0,normalOps=0,discoveries=0,assignments=0,bootstrapCount=0;
int moveAfter=5,failCommand=-1,modeReplies=2,badReply=0;
bool alreadyOperating=false,missing=false,needBootstrap=false,noAck=false;
std::vector<std::string> commands;
bool radioTraceBegin(){return startOk;}
void radioTraceEnd(){}
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
# Restore failure must be injected only at cleanup, not in discovery.
code = code.replace('bool apsUsePairingPan(bool yes){return rawRadioSetPan(yes?0xFFFF:zbOperationalPan);}',
                    'extern bool restoreOk; bool apsUsePairingPan(bool yes){return rawRadioSetPan(yes?0xFFFF:zbOperationalPan) && restoreOk;}')
code += (root / 'PAIRING_FALLBACK.ino').read_text()
code += r'''
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
   encryptedProbeObserve(raw.data(),raw.size(),now,-65,10);
   if(conflicting){raw[8]=raw[14]=0xB6;encryptedProbeObserve(raw.data(),raw.size(),now,-65,10);}
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
 if(!aes)assert(encryptedCapture.phases[35].sent==2);
 uint8_t phase=encryptedCapture.phase;
 uint8_t repeat=encryptedCapture.phases[phase].sent;
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
   encryptedProbeAsdu(pan,src,badReply==6?0x0101:0x0106,data.data(),data.size(),badReply==8?now-1:now+1);
 }
 return !noAck;
}
void delay(unsigned ms){now+=ms;}
void reset(){
 encryptedCapture=ProbeCapture{};assert(!encryptedProbeReaders);
 Inv_Prop[0]=inverters{};Inv_Prop[1]=inverters{};
 snprintf(Inv_Prop[0].invSerial,13,"725000000001");Inv_Prop[0].invType=2;
 snprintf(Inv_Prop[1].invSerial,13,"703000000001");
 saves=normalOps=discoveries=assignments=bootstrapCount=txs=0;commands.clear();now=100;
 startOk=restoreOk=saveOk=panOk=true;alreadyOperating=missing=needBootstrap=noAck=conflicting=false;
 moveAfter=5;failCommand=-1;modeReplies=2;badReply=0;currentPan=zbOperationalPan;rawTxFailure=3;
}
void failed(){
 assert(!pairWithTransportFallback(0));assert(!normalOps&&!strcmp(Inv_Prop[0].invID,"0000"));
 assert(apsInverterUsesEncryption(0)&&!pairedActive&&!encryptedCapture.pairingSaved&&currentPan==zbOperationalPan);
}
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
 assert(encryptedCapture.pairingSaved&&encryptedCapture.verifiedMode==APS_TRANSPORT_PLAIN);
 assert(encryptedCapture.phases[20].entered&&encryptedCapture.phases[35].entered&&encryptedCapture.phases[32].entered);
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
 assert(saves==1&&txs==2&&apsInverterUsesEncryption(0)&&!encryptedCapture.phases[32].entered);
 for(int migration:{1,4}){reset();moveAfter=migration;modeReplies=1;assert(pairWithTransportFallback(0));assert(assignments==migration&&!encryptedCapture.phases[20].entered);}
 reset();needBootstrap=true;assert(pairWithTransportFallback(0)&&bootstrapCount==1);
 reset();noAck=true;assert(pairWithTransportFallback(0));
 reset();noAck=true;rawTxFailure=42;failed();assert(!encryptedCapture.phases[32].entered);
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
 reset();encryptedProbeReaders=1;assert(!pairWithTransportFallback(0)&&!discoveries);encryptedProbeReaders=0;
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
