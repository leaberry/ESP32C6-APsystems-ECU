"""Exercise real receive parser/queue/fragment diagnostic hooks without RF hardware."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / "ZIGBEE_A_TRANSPORT.ino").read_text(encoding="utf-8")

def function(signature):
    start = source.index(signature)
    end = source.index("{", start) + 1
    depth = 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end] + "\n"

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
struct String:std::string{using std::string::string;String(std::string s):std::string(s){}};
#include "POLL_DIAGNOSTICS.h"
constexpr uint8_t YC600_MAX_NUMBER_OF_INVERTERS=9;
uint32_t counters[PD_COUNT]={};
void pollDiagnosticsCount(PollDiagCounter c,uint32_t n){counters[c]+=n;}
void pollDiagnosticsRaw(uint16_t pan,uint16_t src){if(pan==0xA3D8&&src==0xA315)++counters[PD_TARGET_RX];}
bool pairReceiveActive(){return false;}
bool radioPreference(const char*,uint32_t*,bool){return true;}
void diagnosticsAppend(String){}
uint32_t millis(){return 100;}
bool queueFull=false,txOk=true;
const int pdTRUE=1;
int apsRxQueue=1;
int xQueueSend(int,const void*,int){return queueFull?0:pdTRUE;}
uint8_t rawMacSequence=0,rawNwkSequence=0;
bool rawRadioSetPan(uint16_t){return true;}
using esp_err_t=int;
const int ESP_OK=0, ESP_IEEE802154_TX_ERR_CCA_BUSY=1, ESP_IEEE802154_TX_ERR_COEXIST=2;
bool rawRadioStarted=true,rawTxSucceeded=false;
int rawTxMutex=1,rawTxDone=2,rawTxFailure=0;
std::vector<int> errors;size_t transmissions=0;
int pdMS_TO_TICKS(int n){return n;}
int xSemaphoreTake(int semaphore,int ticks){return semaphore==rawTxMutex||ticks?pdTRUE:0;}
void xSemaphoreGive(int){} void vTaskDelay(int){}
const char *esp_err_to_name(int){return "mock";}
int esp_ieee802154_transmit(const uint8_t*,bool){
 rawTxFailure=transmissions<errors.size()?errors[transmissions]:(txOk?0:99);
 ++transmissions;rawTxSucceeded=!rawTxFailure;return ESP_OK;
}
'''
code += source[source.index("constexpr uint8_t APS_CHANNEL"):source.index("QueueHandle_t apsRxQueue")]
code += "RawReassembly rawSessions[RAW_REASSEMBLY_SLOTS] = {};\n"
for signature in ["static uint16_t readLe16(", "static void putLe16(",
                  "static bool serialBytesToText(", "static bool radioTransmit(", "static bool sendApsAck(",
                  "static RawReassembly *sessionFor(", "static void deliverAsdu(",
                  "static void processApsFrame("]:
    code += function(signature)
code += r'''
RawRxFrame frame(uint8_t frag=0,uint8_t block=0,uint8_t ctr=1){
 RawRxFrame rx={};size_t p=1;auto b=rx.bytes;
 putLe16(b,p,0x8841);b[p++]=1;putLe16(b,p,0xA3D8);putLe16(b,p,0);putLe16(b,p,0xA315);
 putLe16(b,p,0x0008);putLe16(b,p,0);putLe16(b,p,0xA315);b[p++]=15;b[p++]=1;
 b[p++]=frag?0x80:0x40;b[p++]=0x14;putLe16(b,p,0x0106);putLe16(b,p,0x0F05);b[p++]=0x14;b[p++]=ctr;
 if(frag){b[p++]=frag;b[p++]=block;}
 uint8_t data[]={0x40,0x90,0,4,0x32,0x25,0xFB,0xFB,1,2,3,4};memcpy(b+p,data,sizeof(data));p+=sizeof(data);
 b[0]=p+1;rx.captured=p+2;return rx;
}
int main(){
 auto rx=frame();processApsFrame(rx);
 assert(counters[PD_DELIVERED]==1&&counters[PD_UNFRAGMENTED_ACK_REQUEST]==1&&counters[PD_TARGET_RX]==1);
 queueFull=true;processApsFrame(rx);assert(counters[PD_APP_DROP]==1&&counters[PD_DELIVERED]==1);queueFull=false;
 rx.captured=12;processApsFrame(rx);assert(counters[PD_PARSE_REJECT]==1);
 rx=frame(2,1);processApsFrame(rx);assert(counters[PD_FRAGMENT_MISS]==1);
 txOk=false;rx=frame(1,2);processApsFrame(rx);assert(counters[PD_ACK_FAIL]==1);
 txOk=true;rx=frame(2,1);processApsFrame(rx);assert(counters[PD_DELIVERED]==2);
 queueFull=true;rx=frame(1,1,3);processApsFrame(rx);assert(counters[PD_APP_DROP]==2);
 for(auto &s:rawSessions){s.active=true;s.updatedAt=1;s.nwkSource=0x1111;}
 rx=frame(1,2,10);processApsFrame(rx);assert(counters[PD_FRAGMENT_EVICT]==1);
 uint8_t outbound[10]={};errors={ESP_IEEE802154_TX_ERR_CCA_BUSY,ESP_IEEE802154_TX_ERR_COEXIST,0};transmissions=0;
 assert(radioTransmit(outbound,sizeof(outbound),true,"test")&&transmissions==3);
 assert(counters[PD_CCA]==1&&counters[PD_COEX]==1&&counters[PD_TX_FAIL]>=1);
 errors.assign(12,ESP_IEEE802154_TX_ERR_CCA_BUSY);transmissions=0;
 auto failedBefore=counters[PD_TX_FAIL];
 assert(!radioTransmit(outbound,sizeof(outbound),true,"test")&&transmissions==12&&counters[PD_TX_FAIL]==failedBefore+1);
 std::cout<<"PASS production receive path: target RX, unfragmented ACK flag, app queue overflow, parser rejection, missing/evicted fragments, ACK failure, successful reassembly\n";
 std::cout<<"PASS production transmit path: separate CCA/coexistence counters and exhausted retry failure\n";
}
'''
with tempfile.TemporaryDirectory() as directory:
    p = Path(directory)
    (p / "test.cpp").write_text(code, encoding="utf-8")
    subprocess.run(["g++", "-std=c++11", "-Wall", "-Wextra", "-Werror", "-I", str(root),
                    str(p / "test.cpp"), "-o", str(p / "test")], check=True)
    subprocess.run([str(p / "test")], check=True)
