"""Exercise the USB listener's production loop with fake radio and USB drivers."""
from pathlib import Path
import subprocess
import tempfile
import sys
sys.dont_write_bytecode = True
from capture_usb import Recording, LineReader, VERSION, HEADER, check_ready, record
from passive_capture_to_pcap import convert

root = Path(__file__).resolve().parents[1]
code = r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <deque>
#include <algorithm>
using std::min;
#define APS_USB_PASSIVE 1
#define VERSION "host-test"
#define IRAM_ATTR
#include "USB_PASSIVE.h"
using portMUX_TYPE=int;
#define portMUX_INITIALIZER_UNLOCKED 0
void portENTER_CRITICAL(int*){} void portEXIT_CRITICAL(int*){}
void portENTER_CRITICAL_ISR(int*){} void portEXIT_CRITICAL_ISR(int*){}
using BaseType_t=int;
constexpr int pdFALSE=0,pdTRUE=1,ESP_OK=0,ESP_ERR_WIFI_NOT_INIT=-1;
constexpr int ESP_BT_CONTROLLER_STATUS_IDLE=0,OUTPUT=1,HIGH=1,LOW=0;
void portYIELD_FROM_ISR(){}
uint32_t clockMs=100;
uint32_t millis(){return clockMs;}
void delay(unsigned ms){clockMs+=ms;}
bool wifi=false,bt=false,ack=true,promisc=false,radio=false,antennaOk=true,failSleep=false;
bool ignorePromisc=false;
int rxStarts=0,sleeps=0,disabled=0;
using wifi_mode_t=int;
int esp_wifi_get_mode(int*){return wifi?0:-1;}
void esp_wifi_stop(){} void esp_wifi_deinit(){}
int esp_bt_controller_get_status(){return bt?1:0;}
int esp_ieee802154_enable(){return 0;}
int esp_ieee802154_disable(){++disabled;radio=false;return 0;}
int esp_ieee802154_sleep(){++sleeps;radio=false;return failSleep?-2:0;}
int esp_ieee802154_set_promiscuous(bool p){promisc=p;return 0;}
bool esp_ieee802154_get_promiscuous(){return promisc;}
int esp_ieee802154_set_coordinator(bool c){assert(!c);return 0;}
int esp_ieee802154_set_channel(int c){assert(c==16);return 0;}
int esp_ieee802154_get_channel(){return 16;}
int esp_ieee802154_set_rx_when_idle(bool v){assert(v);return 0;}
int esp_ieee802154_receive(){++rxStarts;radio=true;ack=ignorePromisc;return 0;}
bool ieee802154_ll_get_tx_auto_ack(){return ack;}
bool ieee802154_ll_get_rx_auto_ack(){return ack;}
bool ieee802154_ll_get_tx_enhance_ack(){return ack;}
struct AntennaSettings {int mode=0,selectPin=14,enablePin=3;bool enableHigh=false;};
bool antennaLoad(AntennaSettings &s){s=AntennaSettings();return antennaOk;}
void antennaPreset(AntennaSettings&){}
bool antennaSelectLevel(const AntennaSettings&s){return s.mode==2;}
int pins[32]={};
void pinMode(int,int){} void digitalWrite(int p,int v){pins[p]=v;}
int digitalRead(int p){return pins[p];}
struct Queue {size_t capacity;std::deque<UsbPassivePacket> frames;};
using QueueHandle_t=Queue*;
QueueHandle_t xQueueCreate(int n,size_t s){assert(s==sizeof(UsbPassivePacket));return new Queue{size_t(n),{}};}
void xQueueReset(Queue*q){q->frames.clear();}
int xQueueSendFromISR(Queue*q,const UsbPassivePacket*p,int*){
 if(q->frames.size()==q->capacity)return 0;q->frames.push_back(*p);return 1;
}
int xQueueReceive(Queue*q,UsbPassivePacket*p,int){if(q->frames.empty())return 0;*p=q->frames.front();q->frames.pop_front();return 1;}
struct FakeSerial {
 bool connected=true;int space=2;std::string input,output;
 explicit operator bool()const{return connected;}
 void setTxTimeoutMs(int n){assert(n==0);}
 int available(){return input.size();}
 char read(){char c=input[0];input.erase(0,1);return c;}
 int availableForWrite(){return space;}
 size_t write(const uint8_t*p,size_t n){size_t sent=min(n,size_t(1));output.append((const char*)p,sent);return sent;}
} Serial;
#include "USB_PASSIVE.ino"
void pump(int n=10000){while(n--)usbPassiveLoop();}
int main(int argc,char**argv){
 std::string scenario=argc>1?argv[1]:"success";
 if(scenario=="wifi")wifi=true;
 if(scenario=="bt")bt=true;
 if(scenario=="antenna")antennaOk=false;
 usbPassiveSetup();
 assert(rxStarts==0); // Merely connecting never starts reception or ECU work.
 if(scenario=="wifi"||scenario=="bt"||scenario=="antenna"){
   assert(!usbListener::ready);Serial.input="START\n";pump(1000);assert(rxStarts==0);return 0;
 }
 assert(usbListener::ready);
 Serial.input="ANTENNA XIAO\n";pump(1000);assert(pins[14]==1&&pins[3]==0);
 if(scenario=="ack")ignorePromisc=true;
 Serial.input="START\n";pump(1000);
 if(scenario=="ack"){assert(!radio&&!usbListener::ready);pump();assert(Serial.output.find("radio-setup-failed")!=std::string::npos);return 0;}
 assert(radio&&!ack);
 uint8_t frame[]={5,2,0,9,0,0};
 if(scenario=="overflow"){
   for(int i=0;i<257;i++)usbPassiveReceive(frame,16,-55,200);
   assert(usbListener::received==257&&usbListener::dropped==1);
 }else{
   usbPassiveReceive(frame,16,-55,200);
   usbPassiveReceive(frame,15,-55,200);
   assert(usbListener::received==1&&usbListener::invalid==1);
 }
 if(scenario=="disconnect")Serial.connected=false;
 else if(scenario=="stall"){Serial.space=0;clockMs+=10001;}
 else if(scenario=="tx")usbPassiveTransmitted();
 else if(scenario=="sleep"){failSleep=true;Serial.input="STOP\n";}
 else if(scenario=="timeout")clockMs=usbListener::started+900000;
 else Serial.input="STOP\n";
 usbPassiveLoop();assert(!radio&&!usbListener::receiving);
 if(scenario=="sleep")assert(disabled&& !usbListener::ready);
 Serial.connected=true;Serial.space=2;pump(50000);
 assert(Serial.output.find("# end reason=")!=std::string::npos);
 assert(Serial.output.find("050200090000,1\n")!=std::string::npos);
 if(scenario=="disconnect")assert(Serial.output.find("usb-disconnected")!=std::string::npos);
 if(scenario=="stall")assert(Serial.output.find("usb-stalled")!=std::string::npos);
 if(scenario=="tx")assert(Serial.output.find("tx=1")!=std::string::npos);
 if(scenario=="timeout")assert(Serial.output.find("time-limit")!=std::string::npos);
 if(scenario=="overflow")assert(Serial.output.find("received=257 written=256 queue_drops=1")!=std::string::npos);
 puts("USB firmware state machine passed");
}
'''
with tempfile.TemporaryDirectory() as directory:
    temp = Path(directory)
    (temp/'hal').mkdir()
    (temp/'esp_bt.h').write_text('')
    (temp/'hal/ieee802154_ll.h').write_text('')
    (temp/'test.cpp').write_text(code)
    subprocess.run(['g++','-std=c++17','-I',str(root),'-I',str(temp),str(temp/'test.cpp'),'-o',str(temp/'test')],check=True)
    for case in ('success','overflow','disconnect','stall','timeout','tx','ack','wifi','bt','antenna','sleep'):
        subprocess.run([str(temp/'test'),case],check=True)

    start = (f'# aps-passive-v1 firmware={VERSION} transport=usb channel=16 wifi=off '
             'bluetooth=off promiscuous=1 ack_tx=0 ack_rx=0 enh_ack=0')
    ready = (f'# aps-usb-ready-v1 firmware={VERSION} ready=1 state=idle channel=16 '
             'wifi=off bluetooth=off fault=none antenna=saved antenna_mode=0')
    row = '100,16,-55,200,050200090000,1'
    end = '# end reason=time-limit elapsed_ms=900000 received=1 written=1 queue_drops=0 invalid=0 tx=0 fault=none'
    check_ready(ready)
    for bad in (ready.replace('wifi=off','wifi=on'),ready.replace('state=idle','state=recording'),ready.replace(VERSION,'old')):
        try: check_ready(bad)
        except ValueError: pass
        else: raise AssertionError('Accepted unsafe startup')
    for bad in (row.replace(',1',',0'),row[:-2],row.replace('0502','0602'),end.replace('written=1','written=2')):
        state=Recording();state.accept(start);state.accept(HEADER)
        try: state.accept(bad)
        except ValueError: pass
        else: raise AssertionError('Accepted malformed stream: '+bad)
    state=Recording();state.accept(start);state.accept(HEADER)
    state.accept(row[:-1]+'2')
    state.accept(end.replace('received=1','received=3').replace('queue_drops=0','queue_drops=2'))
    state=Recording();state.accept(start);state.accept(HEADER)
    state.accept(end.replace('received=1','received=0').replace('written=1','written=0'))
    try:Recording().accept(start.replace('ack_tx=0','ack_tx=1'))
    except ValueError:pass
    else:raise AssertionError('Accepted hardware ACK transmission')
    class Port:
        def __init__(self, text):self.data=bytearray(text.encode());self.commands=[]
        @property
        def in_waiting(self):return len(self.data)
        def write(self,b):self.commands.append(b)
        def read(self,n):
            if not self.data:raise OSError('USB disconnected')
            out=self.data[:n];del self.data[:n];return bytes(out)
    port=Port('\n'.join([ready,ready,start,HEADER,row,end])+'\n')
    path=temp/'capture.csv'
    assert record(port,path,'saved')
    assert convert(path,temp/'capture.pcap')==1
    broken=Port('\n'.join([ready,ready,start,HEADER,row])+'\n')
    partial=temp/'partial.csv'
    try:record(broken,partial,'saved')
    except OSError:pass
    else:raise AssertionError('Disconnect reported success')
    assert 'host_complete=0' in partial.read_text()
    assert broken.commands[-1]==b'STOP\n'
    class SplitPort(Port):
        def read(self,n):return super().read(min(n,7))
    reader=LineReader(SplitPort('first\r\nsecond\n'))
    assert reader.read_line()=='first' and reader.read_line()=='second'

sketch=(root/'ESP32C6-APsystems-ECU.ino').read_text()
assert sketch.index('usbPassiveSetup();')<sketch.index('ecuTimeBegin();')
assert sketch.index('usbPassiveLoop();')<sketch.index('passiveCaptureLoop()')
source=(root/'USB_PASSIVE.ino').read_text()
for forbidden in ('SPIFFS_read(', 'start_wifi(', 'WiFi.begin(', 'antennaSave(', 'putString(', 'esp_ieee802154_transmit('):
    assert forbidden not in source
print('USB capture, partial writes, drops, radio failures, host disconnect and PCAP checks passed')
