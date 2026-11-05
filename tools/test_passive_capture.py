"""Exercise the production passive capture and its HTTP transfer cursor."""
from pathlib import Path
import subprocess
import tempfile
import struct
import sys
sys.dont_write_bytecode = True
from passive_capture_to_pcap import convert

root = Path(__file__).resolve().parents[1]
transport = (root / 'ZIGBEE_A_TRANSPORT.ino').read_text(encoding='utf-8')


def function(source, signature):
    start = source.index(signature)
    end = source.index('{', start) + 1
    depth = 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end] + '\n'


code = r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <iostream>
#include <map>
#include <functional>
#include <algorithm>
using std::min;
bool failReserve=false,failResponse=false;
struct String:std::string {
  using std::string::string;
  String(const std::string &s):std::string(s){}
  String(uint32_t n):std::string(std::to_string(n)){}
  bool reserve(size_t n){if(failReserve)return false;std::string::reserve(n);return true;}
  void concat(const char *s,size_t n){append(s,n);}
};
#define VERSION "host-test"
using portMUX_TYPE=int;
#define portMUX_INITIALIZER_UNLOCKED 0
void portENTER_CRITICAL(int*){}
void portEXIT_CRITICAL(int*){}
uint32_t clockMs=100,dropped=0,randomValue=40;
uint32_t millis(){return clockMs;}
uint32_t esp_random(){return ++randomValue;}
uint32_t rawRadioDroppedFrames(){return dropped;}
bool radioSetup=true;
bool rawRadioBeginPassive();
struct Param {String text;String value(){return text;}};
struct AsyncWebServerResponse {
  String text;int status;
  std::function<size_t(uint8_t*,size_t,size_t)> fill;
  void addHeader(const char*,const char*){}
};
struct AsyncWebServerRequest {
  int status=0;bool header=true;String text;
  std::unique_ptr<AsyncWebServerResponse> response;
  Param auth{"1"};std::map<std::string,Param> params;
  bool hasHeader(const char*){return header;}
  Param* getHeader(const char*){return &auth;}
  bool hasParam(const char *s,bool=false){return params.count(s);}
  Param* getParam(const char *s){return &params.at(s);}
  void send(int code,const char*,String body){status=code;text=body;}
  void send(AsyncWebServerResponse *r){status=r->status;text=r->text;response.reset(r);}
  AsyncWebServerResponse* beginResponse(int code,const char*,String body){if(failResponse)return nullptr;auto r=new AsyncWebServerResponse;r->text=body;r->status=code;return r;}
  template<class T> AsyncWebServerResponse* beginChunkedResponse(const char*,T f){auto r=new AsyncWebServerResponse;r->status=200;r->fill=f;return r;}
};
#include "PASSIVE_CAPTURE.ino"
bool rawRadioBeginPassive(){assert(passiveCaptureOwnsRadio());return radioSetup;}
namespace driver {
bool rawRadioStarted=true,rawPromiscuous=false;
int rawTxMutex=1;
constexpr int pdTRUE=1,ESP_OK=0,APS_CHANNEL=16;
int failStep=0,step=0,sleeps=0;
bool promiscuous=false;
int pdMS_TO_TICKS(int n){return n;}
int xSemaphoreTake(int,int){return pdTRUE;}
void xSemaphoreGive(int){}
int next(){return ++step==failStep?-1:0;}
int esp_ieee802154_sleep(){++sleeps;return next();}
int esp_ieee802154_set_promiscuous(bool value){promiscuous=value;return next();}
bool esp_ieee802154_get_promiscuous(){return promiscuous;}
int esp_ieee802154_set_channel(int n){assert(n==16);return next();}
int esp_ieee802154_set_rx_when_idle(bool value){assert(value);return next();}
int esp_ieee802154_receive(){return next();}
'''
code += function(transport, 'bool rawRadioBeginPassive()') + r'''
}
void cursor(AsyncWebServerRequest &r,uint32_t after){
  r.params["session"]={String(passiveSession)};r.params["after"]={String(after)};
  passiveCaptureDownload(&r);
}
int main(){
  uint8_t raw[]={5,0x02,0,7,0,0};
  assert(passiveCaptureOwnsRadio()==bool(APS_PASSIVE_ONLY));
  assert(passiveCaptureLoop()==bool(APS_PASSIVE_ONLY));
  assert(passiveCaptureObserve(raw,sizeof(raw),100,16,-40,200)==bool(APS_PASSIVE_ONLY));
  assert(passiveCaptureRequest());assert(!passiveCaptureRequest());
  AsyncWebServerRequest r;
  cursor(r,0);assert(r.status==200&&r.text.find("pending=1")!=String::npos);
  assert(passiveCaptureLoop());
  passiveCaptureObserve(raw,sizeof(raw),99,16,-40,200);assert(passiveStore->count==0);
  for(uint32_t i=0;i<10000;++i){
    clockMs=101+i;passiveCaptureObserve(raw,sizeof(raw),clockMs,16,-40,200);
    if(i==0){
      failReserve=true;cursor(r,0);assert(r.status==503);failReserve=false;
      failResponse=true;cursor(r,0);assert(r.status==503);failResponse=false;
      assert(passiveStore->acknowledged==0&&passiveReaders==0);
    }
    cursor(r,i);assert(r.status==200);
    String first=r.text;cursor(r,i);assert(r.text==first);
    assert(r.text.find(std::to_string(clockMs)+",16,-40,200,050200070000\n")!=String::npos);
  }
  assert(passiveStore->count==10000&&passiveStore->dropped==0);
  cursor(r,10000);assert(r.status==200);
  cursor(r,10001);assert(r.status==409);
  cursor(r,0);assert(r.status==409);
  assert(passiveReaders==0);
  for(int i=0;i<300;++i)passiveCaptureObserve(raw,sizeof(raw),++clockMs,16,-40,200);
  assert(passiveStore->count==10256&&passiveStore->dropped==44&&passiveStore->running);
  cursor(r,10000);assert(r.status==200&&r.text.find("buffer_drops=44")!=String::npos);
  cursor(r,10016);assert(r.status==200);
  passiveCaptureObserve(raw,sizeof(raw),++clockMs,16,-40,200);assert(passiveStore->count==10257);
  dropped=3;clockMs=900099;passiveCaptureLoop();assert(passiveStore->running);
  clockMs=900100;passiveCaptureLoop();assert(!passiveStore->running);
  assert(passiveStore->ended-passiveStore->started==900000);
  assert(passiveCaptureStatus().find("queue_drops=3")!=String::npos);
  uint32_t oldSession=passiveSession;
  assert(passiveCaptureRequest());assert(passiveSession!=oldSession);
  r.params["session"]={String(oldSession)};passiveCaptureDownload(&r);assert(r.status==409);
  clockMs=0xFFFFFFF0;passiveCaptureLoop();
  passiveCaptureObserve(raw,sizeof(raw),20,16,-40,200);assert(passiveStore->count==1);
  clockMs+=900000;passiveCaptureLoop();assert(!passiveStore->running);
  assert(passiveCaptureRequest());clockMs=500;passiveCaptureLoop();
  passiveStop=true;clockMs=510;passiveCaptureLoop();assert(!passiveStore->running);
  assert(passiveCaptureOwnsRadio());
  passiveCaptureObserve(raw,sizeof(raw),511,16,-40,200);assert(passiveStore->count==0);
  radioSetup=false;assert(passiveCaptureRequest());passiveCaptureLoop();
  assert(passiveCaptureOwnsRadio()&&!passiveStore->running);
  assert(passiveCaptureStatus().find("radio setup failed")!=String::npos);
  r.header=false;passiveCaptureControl(&r);assert(r.status==400);
  for(int failure=0;failure<=5;++failure){
    driver::failStep=failure;driver::step=driver::sleeps=0;
    assert(driver::rawRadioBeginPassive()==(failure==0));
    assert(driver::sleeps==(failure?2:1));
  }
  std::cout<<"PASS capture: 10,000 frames, retries, expired cursors, overflow recovery, 15-minute timeout, wrap and setup failures\n";
}
'''
worker = function(transport, 'static void rawWorker(')
assert worker.index('passiveCaptureObserve') < worker.index('pairTransportObserve')
sketch = (root / 'ESP32C6-APsystems-ECU.ino').read_text(encoding='utf-8')
loop = function(sketch, 'void loop()')
assert loop.index('passiveCaptureLoop') < loop.index('haLoop') < loop.index('pollSchedulerLoop')
assert 'if (!APS_PASSIVE_ONLY && !settingsHasRestoredIdentity())' in sketch
assert 'if (APS_PASSIVE_ONLY) coordinator(false);' in sketch
assert 'if(Polling && !APS_PASSIVE_ONLY)' in sketch
start = function(transport, 'bool rawRadioStart()')
assert start.index('esp_ieee802154_set_promiscuous(true)') < start.index('esp_ieee802154_receive()')
tx = function(transport, 'bool radioTransmit(')
assert tx.index('passiveCaptureOwnsRadio()') < tx.index('esp_ieee802154_transmit(')
with tempfile.TemporaryDirectory() as tmp:
    p = Path(tmp)
    (p/'test.cpp').write_text(code, encoding='utf-8')
    for passive in (0, 1):
        subprocess.run(['g++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-fsanitize=undefined',
                        f'-DAPS_PASSIVE_ONLY={passive}', '-I', str(root), str(p/'test.cpp'), '-o', str(p/'test')], check=True)
        subprocess.run([str(p/'test')], check=True)
    capture = p/'capture.csv'
    capture.write_text('# aps-passive-v1 test\nuptime_ms,channel,rssi_dbm,lqi,raw_hex\n4294967280,16,-40,200,050200070000\n20,16,-40,200,050200080000\n\n# browser_download packets=2 complete=1\n')
    output = p/'test.pcap'
    assert convert(capture, output) == 2
    data = output.read_bytes()
    assert struct.unpack_from('<I', data, 20)[0] == 230
    assert data[40:43] == bytes.fromhex('020007')
    assert struct.unpack_from('<IIII', data, 43) == (4294967, 316000, 3, 3)
    capture.write_text(capture.read_text().replace('050200070000', '060200070000'))
    try:
        convert(capture, output)
        raise AssertionError('Malformed PHY length accepted')
    except ValueError:
        pass
    print('PASS PCAP conversion: bytes, uptime wrap, capture footer and invalid lengths')
