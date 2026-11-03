"""Run the production QS1 batch, pairing ownership and packet builders with fake RF."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
test = root / 'tools/test_qs1_pairing.py'
scope = {'__file__': str(test)}
exec(compile(test.read_text(encoding='utf-8').split("code += (root / 'RADIO_TRACE.ino')")[0], str(test), 'exec'), scope)
code, function = scope['code'], scope['function']
start, end = code.index('// BATCH_STUBS_BEGIN'), code.index('// BATCH_STUBS_END')
code = code[:start] + code[end + len('// BATCH_STUBS_END'):]
code = code.replace('String stage;', 'std::vector<std::string> stages; String stage;')
code = code.replace('stage=s;', 'stage=s;stages.push_back(s);')
code = code.replace('onlySaved?0x1234:0xA3D8', 'onlySaved?0x1234:zbOperationalPan')
code = code.replace('int esp_ieee802154_get_channel(){return 16;}',
                    'uint8_t currentChannel=16;int esp_ieee802154_get_channel(){return currentChannel;}')
code = code.replace('int esp_ieee802154_set_channel(uint8_t){return ESP_OK;}',
                    'int esp_ieee802154_set_channel(uint8_t c){currentChannel=c;return ESP_OK;}')
code += r'''
#include "PAIRING_SESSION.h"
char ECU_ID[]="D8A3011B9780";
uint8_t rawExtendedAddress[8]={1,2,3,4,5,6,7,8};
void esp_ieee802154_get_extended_address(uint8_t*b){memcpy(b,rawExtendedAddress,8);}
void pairTraceAsdu(uint16_t,uint16_t,uint16_t,const uint8_t*,size_t,uint32_t){}
PairPowerCheck pairPowerCheck(const uint8_t*,const PairPowerReply&,int model,bool aes){assert(model==1&&!aes);return PP_VALID;}
bool decodeInverterInfoL2(const uint8_t*,size_t,uint8_t&,char*,size_t){return true;}
void pairTransportBegin(const uint8_t*,uint16_t);
void pairTransportStage(uint16_t,uint16_t,bool);
void pairTransportStop();
void pairTransportWaitForReply();
bool pairTransportTakeReply(PairPowerReply&);
uint16_t pairTransportFound(bool);
bool pairTransportConflict();
'''
code += (root / 'RADIO_TRACE.ino').read_text(encoding='utf-8')
code += (root / 'PAIRING_SESSION.ino').read_text(encoding='utf-8')
for signature in ['static void putLe16(', 'bool rawQs1Reception(', 'bool rawQs1Channel(',
                  'bool apsSendQs1Pairing(', 'bool apsSendQs1Read(', 'bool apsSendPairingQuery(']:
    code += function('ZIGBEE_A_TRANSPORT.ino', signature)
code += (root / 'QS1_BATCH.ino').read_text(encoding='utf-8')
code += (root / 'QS1_PAIRING.ino').read_text(encoding='utf-8')
code += r'''
int scenario=0;uint16_t targetPan=0;int reads=0;
std::vector<uint8_t> hex(const char*s){std::vector<uint8_t>b;for(;*s;s+=2){char t[3]={s[0],s[1],0};b.push_back(strtoul(t,nullptr,16));}return b;}
void delay(unsigned n){
 now+=n;
 if(scenario==2 && stage=="directed-020D-operating")targetPan=zbOperationalPan;
 if(stage.rfind("DC-",0)==0 || stage.rfind("BB-",0)==0){
   ++reads;
   uint8_t payload[]={0x80,0x60,0,0,0,1,0xFB,0xFB,6,0xBB,0,0,0,0,0,0,0xC1,0xFE,0xFE};
   if(targetPan==currentPan) assert(pairTransportAsdu(currentPan,0x4321,0x0106,payload,sizeof(payload),now));
   return;
 }
 if(!scenario || currentPan!=targetPan)return;
 if(stage.find("discovery")==String::npos && stage.find("verify")==String::npos)return;
 auto b=hex("23618801D8A3000021434800000021430F0100140101050F1400FF1A8060000000010000");
 b[4]=currentPan;b[5]=currentPan>>8;
 if(scenario==4)b[28]^=1;
 if(scenario==5)b[14]=b[15]=0; // A relay echo is never a source address.
 if(!probe){pairTransportObserve(b.data(),b.size(),now);radioTraceObserve(b.data(),b.size(),16,-50,10,now);}
}
uint16_t cluster(const std::vector<uint8_t>&b){
 size_t nwk=(b[0]&0x40)?9:11;
 size_t aps=nwk+8+((b[nwk+1]&0x10)?8:0);
 return b[aps+2]|uint16_t(b[aps+3])<<8;
}
void reset(int mode){
 now=100;currentPan=zbOperationalPan;currentChannel=16;rawRadioStarted=true;rawPromiscuous=probe=false;
 traceReady=true;failTx=receiveCalls=failReception=saves=normalOps=traceTx=reads=0;
 rawMacSequence=rawNwkSequence=rawApsCounter=0;saveOk=true;savedRoute=onlySaved=false;
 capturedSaved=capturedRestored=false;frames.clear();sentAt.clear();listenPans.clear();filters.clear();stages.clear();
 Inv_Prop[0].invType=1;pairReceiveStop();pairTransportStop();
 scenario=mode;targetPan=(mode==2||mode==3)?0xFFFF:zbOperationalPan;
}
int main(){
 reset(0);assert(!pairQs1(0)&&!saves&&capturedRestored&&currentChannel==16&&!probe&&!rawPromiscuous);
 assert(stages.size()<52&&now<360000&&reads==0);
 int broadcastReads=0;for(auto &b:frames)if(cluster(b)==0x0006){assert(b[0]==0x41);++broadcastReads;}
 assert(broadcastReads==12);
 auto silentStages=stages;int receptionCount=receiveCalls;
 assert(std::find(stages.begin(),stages.end(),"unfiltered-bootstrap")!=stages.end());
 assert(std::find(stages.begin(),stages.end(),"directed-020D-operating")!=stages.end());
 assert(std::find(stages.begin(),stages.end(),"receive-only-channel-scan")!=stages.end());
 for(int mode:{1,2}){
   reset(mode);assert(pairQs1(0)&&saves==1&&normalOps==1&&capturedSaved&&capturedRestored);
   assert(!probe&&!rawPromiscuous&&currentChannel==16&&!pairTransport.active&&!pairReceiveActive());
   assert(reads==12&&now<360000&&stages.size()<52);
   if(mode==1)for(auto &b:frames)assert(cluster(b)==0x020C||cluster(b)==0x0006);
   assert(std::find(stages.begin(),stages.end(),"unfiltered-bootstrap")==stages.end());
 }
 reset(3);assert(!pairQs1(0)&&!saves&&capturedRestored&&reads==12&&stages.size()<52);
 zbOperationalPan=0xEFD9;reset(3);savedRoute=true;
 assert(!pairQs1(0)&&!saves&&stages.size()<52&&now<360000);
 zbOperationalPan=0xA3D8;
 for(int mode:{4,5}){reset(mode);assert(!pairQs1(0)&&!saves&&!reads&&capturedRestored);}
 for(int i=1;i<=receptionCount;++i){reset(0);failReception=i;assert(!pairQs1(0)&&!saves&&!normalOps&&!pairReceiveActive()&&!pairTransport.active);}
 reset(1);saveOk=false;assert(!pairQs1(0)&&saves==1&&!normalOps);
 reset(0);failTx=1;assert(!pairQs1(0)&&!saves&&capturedRestored);
 std::cout<<"PASS QS1 batch: silent, already paired, directed-PAN recovery, foreign-only, wrong UID, relay echo, all radio failures, read ownership, stage/time bounds and no storage on failure\n";
}
'''
with tempfile.TemporaryDirectory() as directory:
    p = Path(directory)
    (p/'test.cpp').write_text(code, encoding='utf-8')
    subprocess.run(['g++','-std=c++17','-Wall','-Wextra','-Werror','-fsanitize=undefined','-I',str(root),str(p/'test.cpp'),'-o',str(p/'test')],check=True)
    subprocess.run([str(p/'test')],check=True)
