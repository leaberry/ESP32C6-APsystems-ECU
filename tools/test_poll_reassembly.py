"""Verify concurrent production APS reassembly and receive timestamp propagation."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root/'ZIGBEE_A_TRANSPORT.ino').read_text(encoding='utf-8')

def function(signature):
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
#include <cstring>
#include <string>
#include <iostream>
#include <vector>
using std::min;
using String=std::string;
constexpr uint8_t YC600_MAX_NUMBER_OF_INVERTERS=9;
bool pairReceiveActive(){return false;}
bool radioPreference(const char*,uint32_t*,bool){return true;}
void diagnosticsAppend(String){}
uint32_t millis(){return 10000;}
bool apsRxQueue=true;
bool sendApsAck(uint16_t,uint16_t,uint16_t,uint8_t,uint8_t,uint16_t,uint16_t,uint8_t,uint8_t,uint8_t){return true;}
'''
code += source[source.index('constexpr uint8_t APS_CHANNEL'):source.index('QueueHandle_t apsRxQueue')]
code += r'''
RawReassembly rawSessions[RAW_REASSEMBLY_SLOTS]={};
std::vector<ApsRxFrame> received;
int xQueueSend(bool,const ApsRxFrame* f,int){received.push_back(*f);return 1;}
'''
for signature in ['static uint16_t readLe16(', 'static void putLe16(',
                  'static bool serialBytesToText(', 'static RawReassembly *sessionFor(',
                  'static void deliverAsdu(', 'static void processApsFrame(']:
    code += function(signature)
code += r'''
RawRxFrame frame(int inverter,uint8_t frag,uint8_t block,uint32_t timestamp){
 RawRxFrame rx={};rx.receivedAt=timestamp;rx.rssi=-50;rx.lqi=150;size_t p=1;auto b=rx.bytes;
 putLe16(b,p,0x8841);b[p++]=1;putLe16(b,p,0xA3D8);putLe16(b,p,0);putLe16(b,p,1+inverter);
 putLe16(b,p,0x0008);putLe16(b,p,0);putLe16(b,p,1+inverter);b[p++]=15;b[p++]=1;
 b[p++]=frag?0x80:0x40;b[p++]=0x14;putLe16(b,p,0x0106);putLe16(b,p,0x0F05);b[p++]=0x14;b[p++]=1;
 if(frag){b[p++]=frag;b[p++]=block;}
 uint8_t data[]={0x40,0x90,0,4,0x32,0x25,0xFB,0xFB,1,2,3,4};memcpy(b+p,data,sizeof(data));p+=sizeof(data);
 b[0]=p+1;rx.captured=p+2;return rx;
}
int main(){
 for(int i=0;i<9;++i)processApsFrame(frame(i,1,2,100+i));
 assert(received.empty());
 // A duplicate first fragment must not freshen an old partial response.
 processApsFrame(frame(0,1,2,500));
 for(int i=8;i>=0;--i)processApsFrame(frame(i,2,1,1000+i));
 assert(received.size()==9);
 for(const auto& f:received){assert(f.pan==0xA3D8&&f.receivedAt==99U+f.source&&f.len==24);}
 processApsFrame(frame(0,0,0,2000));
 assert(received.back().receivedAt==2000&&received.back().pan==0xA3D8&&received.back().len==12);
 std::cout<<"PASS APS reassembly: nine simultaneous inverters, original fragment timestamp, duplicate first fragment and direct replies\n";
}
'''
with tempfile.TemporaryDirectory() as tmp:
    p = Path(tmp)
    (p/'test.cpp').write_text(code, encoding='utf-8')
    subprocess.run(['g++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-fsanitize=undefined',
                    str(p/'test.cpp'), '-o', str(p/'test')], check=True)
    subprocess.run([str(p/'test')], check=True)
