"""Replay QS1 NWK routing and the production pairing flow with fake radio I/O."""
from pathlib import Path
import json
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[1]
scope = {'__file__': str(root/'tools/test_qs1_pairing.py')}
exec(compile((root/'tools/test_qs1_pairing.py').read_text().split("code += (root / 'RADIO_TRACE.ino')")[0], '<shared-radio-fakes>', 'exec'), scope)
code, function = scope['code'], scope['function']
code=code.replace('void pairTransportStop(){}','')
code += r'''
#include "QS1_ROUTING.h"
#include "PAIRING_SESSION.h"
#include "POLL_TELEMETRY.h"
constexpr int portMAX_DELAY=100000,YC600_MAX_NUMBER_OF_INVERTERS=1;
int qs1RouteGate=1; int xSemaphoreCreateMutex(){return 1;}
uint16_t &rawCurrentPan=currentPan;
Qs1RouteWindow qs1RouteWindow;
uint32_t qs1NormalRouteAt[1]={};
bool wantStatus=false, wantRoute=true, validPower=true;
unsigned powerQueries=0;
bool apsDecryptTail(const uint8_t*,const uint8_t*,size_t,uint8_t*,size_t,size_t*){return false;}
void pairTraceAsdu(uint16_t,uint16_t,uint16_t,const uint8_t*,size_t,uint32_t){}
bool apsSendQs1Read(uint16_t pan,uint16_t src,const uint8_t*,uint8_t cmd,bool extended){
 assert(pan==zbOperationalPan && src==0x4321 && cmd==0xBB && !extended);++powerQueries;return true;
}
'''
code+=function('ZIGBEE_A_TRANSPORT.ino','struct RawRxFrame')+';\n'
code+=(root/'RADIO_TRACE.ino').read_text()
code+=(root/'PAIRING_SESSION.ino').read_text()
code+=function('PAIRING_FALLBACK.ino','static PairPowerCheck pairPowerCheck(')
for signature in ['bool rawQs1Reception(', 'bool apsSendQs1Pairing(', 'static bool processQs1Route(',
                  'bool qs1RoutingBegin(', 'uint16_t qs1RoutingSource(', 'void qs1RoutingEnd(']:
    code+=function('ZIGBEE_A_TRANSPORT.ino',signature)
code+=(root/'QS1_ROUTING.ino').read_text()
code+='#define APS_QS1_ROUTE_DIAG 1\n'+(root/'QS1_PAIRING.ino').read_text()
code+=r'''
std::vector<uint8_t> hex(const char*s){std::vector<uint8_t>b;for(;*s;s+=2){char t[3]={s[0],s[1],0};b.push_back(strtoul(t,nullptr,16));}return b;}
RawRxFrame requestFrame(){
 auto b=hex("2141885FCE14FFFF34B30910FDFF21430763FFFF8060000000010100000000070000");
 RawRxFrame f={};f.channel=16;f.receivedAt=now;f.captured=b.size();memcpy(f.bytes,b.data(),b.size());return f;
}
void delay(unsigned n){
 now+=n;
 if(wantRoute && stage=="route-discovery") {auto f=requestFrame();processQs1Route(f);}
 if(wantStatus && stage=="route-status") {
  auto b=hex("23618801D8A3000021434800000021430F0100140101050F1400FF1A8060000000010000");
  radioTraceObserve(b.data(),b.size(),16,-50,10,now);
 }
 if(stage=="route-power-verify" && reply) {
  uint8_t b[94]={};pairSerialBytes(Inv_Prop[0].invSerial,b);
  b[6]=b[7]=0xFB;b[8]=81;b[9]=0xB1;b[12]=1;b[92]=b[93]=0xFE;
  if(!validPower)b[8]=0;
  pairTransport.asdu(zbOperationalPan,0x4321,0x0106,b,sizeof(b),now);
 }
}
void reset(){
 now=100;currentPan=zbOperationalPan;rawRadioStarted=true;rawPromiscuous=probe=false;
 traceReady=reply=saveOk=wantRoute=validPower=true;wantStatus=false;
 failTx=receiveCalls=failReception=saves=normalOps=traceTx=powerQueries=0;
 rawMacSequence=rawNwkSequence=rawApsCounter=0;capturedSaved=capturedRestored=false;
 frames.clear();sentAt.clear();listenPans.clear();filters.clear();Inv_Prop[0].invType=1;
 pairReceiveStop();pairTransportStop();qs1RouteWindow=Qs1RouteWindow{};qs1NormalRouteAt[0]=0;
 savedRoute=false;
}
int main(){
 reset();auto f=requestFrame();Qs1RouteRequest r;
 assert(qs1RouteRequest(f.bytes,f.captured,r) && r.source==0x4321 && r.sender==0xB334);
 uint8_t ecu[6]={0x80,0x97,0x1B,1,0xA3,0xD8},uid[6];pairSerialBytes(Inv_Prop[0].invSerial,uid);
 uint8_t out[64];assert(qs1RouteReply(out,sizeof(out),r,0xA3D8,ecu,0x32,0x6B)==57);
 auto expected=hex("618832D8A3214300000918214300001E6BFFFF806000000001FFFF80971B01A3D80230002143000007FFFF806000000001FFFF80971B01A3D8");
 assert(expected.size()==57 && !memcmp(out,expected.data(),57));
 r.pan=0xA3D8;assert(qs1RouteReply(out,sizeof(out),r,0xA3D8,ecu,1,2)==57);
 assert(pairLe16(out+5)==0xB334); // Same-PAN reply follows the incoming next hop.
 assert(!qs1RouteReply(out,56,r,0xA3D8,ecu,1,2));
 for(size_t n=0;n<f.captured;++n)assert(!qs1RouteRequest(f.bytes,n,r));
 for(int offset:{1,4,6,10,12,14,18,20,26,27,29}) {
  auto bad=f;bad.bytes[offset]=0;
  if(offset==4 || offset==14)bad.bytes[offset+1]=0;
  if(offset==20)bad.bytes[offset]=0xAA;
  if(offset==27 || offset==29)bad.bytes[offset]=1;
  assert(!qs1RouteRequest(bad.bytes,bad.captured,r));
 }
 assert(qs1RoutingBegin(uid,ecu,zbOperationalPan));
 assert(processQs1Route(f) && frames.size()==1 && qs1RoutingSource()==0x4321);
 assert(processQs1Route(f) && frames.size()==1); // Duplicate relay, same discovery.
 now+=501;f.receivedAt=now;assert(processQs1Route(f) && frames.size()==2);
 auto wrong=f;wrong.bytes[25]^=1;now+=501;processQs1Route(wrong);assert(frames.size()==2);
 auto conflict=f;conflict.bytes[14]^=1;processQs1Route(conflict);assert(qs1RoutingSource()==0);
 qs1RoutingEnd();
 // Reject queued pre-session packets, expired windows and excess replies.
 assert(qs1RouteRequest(f.bytes,f.captured,r));
 Qs1RouteWindow window;window.begin(uid,ecu,zbOperationalPan,1000);
 assert(!window.accept(r,999,1001));
 for(unsigned i=0;i<24;++i)assert(window.accept(r,1000+i*500,1000+i*500));
 assert(!window.accept(r,14000,14000));
 window.begin(uid,ecu,zbOperationalPan,1000);
 assert(!window.accept(r,91001,91001));
 window.begin(uid,ecu,zbOperationalPan,0xFFFFFF00);
 assert(window.accept(r,100,100)); // The window also works across millis() wrap.
 for(int model:{0,2,3}){reset();Inv_Prop[0].invType=model;assert(!pairQs1Routed(0)&&frames.empty());processQs1Route(f);assert(frames.empty());}
 reset();assert(pairQs1(0) && saves==1 && powerQueries==2 && capturedSaved && capturedRestored);
 assert(!rawPromiscuous && !qs1RouteWindow.active && !pairReceiveActive() && !pairTransport.active);
 assert(now<45000);
 reset();wantStatus=true;assert(pairQs1Routed(0) && saves==1 && !powerQueries);
 reset();reply=false;assert(!pairQs1Routed(0) && !saves && powerQueries==3 && capturedRestored);
 reset();validPower=false;assert(!pairQs1Routed(0) && !saves && capturedRestored);
 reset();wantRoute=false;assert(!pairQs1Routed(0) && !saves && !powerQueries);
 reset();failTx=1;assert(!pairQs1Routed(0) && !saves && capturedRestored && !qs1RouteWindow.active);
 reset();saveOk=false;assert(!pairQs1Routed(0) && saves==1 && !normalOps);
 reset();failReception=2;assert(!pairQs1Routed(0) && !saves);
 reset();traceReady=false;assert(!pairQs1Routed(0) && frames.empty());
 // Normal handling requires a configured QS1, a saved peer and the current PAN.
 reset();savedRoute=true;currentPan=0x1234;f=requestFrame();f.bytes[4]=0x34;f.bytes[5]=0x12;
 processQs1Route(f);assert(frames.size()==1);
 reset();savedRoute=true;f=requestFrame();processQs1Route(f);assert(frames.empty());
 reset();savedRoute=true;currentPan=0x1234;f=requestFrame();f.bytes[4]=0x34;f.bytes[5]=0x12;
 f.bytes[14]^=1;processQs1Route(f);assert(frames.empty());
 reset();savedRoute=true;currentPan=0x1234;f=requestFrame();f.bytes[4]=0x34;f.bytes[5]=0x12;
 rawPromiscuous=true;processQs1Route(f);assert(frames.empty());
 rawPromiscuous=false;assert(pairReceiveBegin(Inv_Prop[0].invSerial,zbOperationalPan));
 processQs1Route(f);assert(frames.empty());pairReceiveStop();
 for(int model:{0,2,3}){reset();savedRoute=true;currentPan=0x1234;Inv_Prop[0].invType=model;processQs1Route(f);assert(frames.empty());}
'''
if len(sys.argv)>1:
    work=Path(sys.argv[1])
    requests=[p for p in json.loads((work/'NewSystem-decoded.json').read_text()) if p.get('command')==1]
    replies=[p for p in json.loads((work/'OldSystem-decoded.json').read_text()) if p.get('command')==2]
    assert len(requests)==6 and len(replies)==15
    for p in requests:
        raw=bytes.fromhex(p['raw']);wire=bytes([len(raw)])+raw
        code+=' { auto packet=hex("'+wire.hex()+'"); Qs1RouteRequest q; assert(qs1RouteRequest(packet.data(),packet.size(),q));'
        code+=f' assert(q.source==0x{p["nwk_src"]} && q.sender==0x{p["mac_src"]} && q.request=={p["request_id"]}); }}\n'
    for p in replies:
        code+=' { Qs1RouteRequest q;'
        code+=f'q.pan=0xA3D8;q.sender=q.source=0x{p["originator"]};q.request={p["request_id"]};q.cost={p["cost"]};'
        serial=p['originator_ieee_wire'][4:]
        code+=f'pairSerialBytes("{serial}",q.serial);auto expected=hex("{p["raw"][:-4]}");uint8_t actual[64];'
        code+=f'assert(qs1RouteReply(actual,sizeof(actual),q,0xA3D8,ecu,{p["mac_seq"]},{p["nwk_seq"]})==expected.size());'
        code+='assert(!memcmp(actual,expected.data(),expected.size())); }\n'
code+='std::cout<<"PASS QS1 routes, rejected inputs, reply bytes, rate limits, model isolation, pairing verification and cleanup\\n"; }\n'
with tempfile.TemporaryDirectory() as directory:
    p=Path(directory);(p/'test.cpp').write_text(code)
    subprocess.run(['g++','-std=c++17','-Wall','-Wextra','-Werror','-fsanitize=undefined','-I',str(root),str(p/'test.cpp'),'-o',str(p/'test')],check=True)
    subprocess.run([str(p/'test')],check=True)
