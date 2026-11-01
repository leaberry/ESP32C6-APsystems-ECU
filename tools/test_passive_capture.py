"""Exercise passive capture ownership, bounds, download leases and radio setup."""
from pathlib import Path
import subprocess
import tempfile
import struct
import sys
sys.dont_write_bytecode = True
from passive_capture_to_pcap import convert

root = Path(__file__).resolve().parents[1]
transport = (root / "ZIGBEE_A_TRANSPORT.ino").read_text(encoding="utf-8")


def function(source, signature):
    start = source.index(signature)
    end = source.index("{", start) + 1
    depth = 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end] + "\n"


code = r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <algorithm>
#include <iostream>
using String = std::string;
using std::min;
#define VERSION "host-test"
using portMUX_TYPE=int;
#define portMUX_INITIALIZER_UNLOCKED 0
void portENTER_CRITICAL(int*){}
void portEXIT_CRITICAL(int*){}
uint32_t clockMs=100, dropped=0;
uint32_t millis(){return clockMs;}
uint32_t rawRadioDroppedFrames(){return dropped;}
bool radioSetup=true, duringSetup=false;
bool rawRadioBeginPassive();
struct AsyncWebServerResponse {
  std::function<size_t(uint8_t*,size_t,size_t)> fill;
  void addHeader(const char*,const char*){}
};
struct Param { String value(){return "1";} };
struct AsyncWebServerRequest {
  int status=0; bool header=true, start=true;
  Param param;
  std::unique_ptr<AsyncWebServerResponse> response;
  bool hasHeader(const char*){return header;}
  Param* getHeader(const char*){return &param;}
  bool hasParam(const char*,bool){return start;}
  void send(int code,const char*,const char*){status=code;}
  void send(AsyncWebServerResponse* r){response.reset(r);status=200;}
  template<class T> AsyncWebServerResponse* beginChunkedResponse(const char*,T f){
    auto r=new AsyncWebServerResponse;r->fill=f;return r;
  }
};
#include "PASSIVE_CAPTURE.ino"
bool rawRadioBeginPassive(){
  assert(passiveCaptureOwnsRadio());
  duringSetup=true;
  assert(!passiveCaptureRequest());
  AsyncWebServerRequest r;passiveCaptureDownload(&r);assert(r.status==409);
  return radioSetup;
}
'''
# Use the production setup sequence as well as the lifecycle above.
setup = function(transport, "bool rawRadioBeginPassive()")
code += r'''
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
'''+setup+r'''
}
int main(){
  uint8_t raw[]={5,0x02,0x00,0x07,0,0};
  assert(!passiveCaptureLoop());
  assert(!passiveCaptureObserve(raw,sizeof(raw),clockMs,16,-40,200));
  assert(passiveCaptureRequest());
  assert(!passiveCaptureOwnsRadio());
  AsyncWebServerRequest request;passiveCaptureDownload(&request);assert(request.status==409);
  assert(passiveCaptureLoop()&&duringSetup);
  assert(passiveCaptureOwnsRadio());
  assert(!passiveCaptureRequest());
  passiveCaptureObserve(raw,sizeof(raw),99,16,-40,200);assert(passiveStore->count==0);
  passiveCaptureObserve(raw,sizeof(raw),101,16,-40,200);assert(passiveStore->count==1);
  passiveCaptureDownload(&request);assert(request.status==409);
  passiveStop=true;clockMs=110;passiveCaptureLoop();
  passiveCaptureDownload(&request);assert(request.status==200);
  assert(!passiveCaptureRequest());
  String csv;uint8_t chunk[7];size_t n;
  while((n=request.response->fill(chunk,sizeof(chunk),csv.size()))) csv.append((char*)chunk,n);
  assert(csv.find("101,16,-40,200,050200070000\n")!=String::npos);
  assert(csv.find("packets=1 reason=stopped by user")!=String::npos);
  request.response.reset();assert(passiveReaders==0);
  assert(passiveCaptureOwnsRadio()&&passiveCaptureLoop());
  assert(passiveCaptureObserve(raw,sizeof(raw),111,16,-40,200));
  assert(passiveStore->count==1); // Stopping recording never re-enables decoders/ACKs.
  assert(passiveCaptureRequest());clockMs=0xFFFFFFF0;passiveCaptureLoop();
  passiveCaptureObserve(raw,sizeof(raw),20,16,-40,200);assert(passiveStore->count==1);
  clockMs+=PassiveCapture::duration;passiveCaptureLoop();
  assert(!passiveStore->running&&String(passiveStore->reason)=="time limit");
  assert(passiveCaptureRequest());clockMs=500;passiveCaptureLoop();
  for(int i=0;i<300;++i)passiveCaptureObserve(raw,sizeof(raw),501+i,16,-40,200);
  assert(passiveStore->count==256&&!passiveStore->running);
  assert(String(passiveStore->reason)=="buffer full");
  radioSetup=false;assert(passiveCaptureRequest());passiveCaptureLoop();
  assert(passiveCaptureOwnsRadio()&&!passiveStore->running);
  assert(passiveCaptureStatus().find("radio setup failed")!=String::npos);
  for(int failure=0;failure<=5;++failure){
    driver::failStep=failure;driver::step=driver::sleeps=0;
    assert(driver::rawRadioBeginPassive()==(failure==0));
    assert(driver::sleeps==(failure?2:1));
  }
  std::cout<<"PASS passive capture: ownership, setup failures, bounds, timeout/wrap, stale packets, download leases and seven-byte chunks\n";
}
'''

# These ordering checks cover the points where normal radio traffic is diverted.
worker = function(transport, "static void rawWorker(")
assert worker.index("passiveCaptureObserve") < worker.index("pairTransportObserve")
loop = function((root / "ESP32C6-APsystems-ECU.ino").read_text(encoding="utf-8"), "void loop()")
assert loop.index("passiveCaptureLoop") < loop.index("haLoop") < loop.index("pollSchedulerLoop")

with tempfile.TemporaryDirectory() as tmp:
    p = Path(tmp)
    (p / "test.cpp").write_text(code, encoding="utf-8")
    subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-fsanitize=undefined",
                    "-I", str(root), str(p / "test.cpp"), "-o", str(p / "test")], check=True)
    subprocess.run([str(p / "test")], check=True)
    capture = p / "capture.csv"
    capture.write_text("# aps-passive-v1 test\nuptime_ms,channel,rssi_dbm,lqi,raw_hex\n4294967280,16,-40,200,050200070000\n20,16,-40,200,050200080000\n")
    output = p / "test.pcap"
    assert convert(capture, output) == 2
    data = output.read_bytes()
    assert struct.unpack_from("<I", data, 20)[0] == 230
    assert data[40:43] == bytes.fromhex("020007")
    assert struct.unpack_from("<IIII", data, 43) == (4294967, 316000, 3, 3)
    capture.write_text(capture.read_text().replace("050200070000", "060200070000"))
    try:
        convert(capture, output)
        raise AssertionError("Malformed PHY length accepted")
    except ValueError:
        pass
    print("PASS PCAP conversion: frame bytes, link type, uptime wrap and invalid lengths")
