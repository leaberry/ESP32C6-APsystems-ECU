"""Test the production persistent capture queue/ring with an in-memory filesystem."""
from pathlib import Path
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / "QT2_CAPTURE.ino").read_text(encoding="utf-8")
harness = r'''
#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <iostream>
#include "QT2_CAPTURE.h"
using std::min;
#define VERSION "test"
#define pdTRUE 1
using SemaphoreHandle_t = void *;
SemaphoreHandle_t xSemaphoreCreateMutex() {return (void *)1;}
int xSemaphoreTake(void *,int) {return 1;}
void xSemaphoreGive(void *) {}
uint32_t ticks=1;
uint32_t millis() {return ticks;}
uint32_t esp_random() {return 123;}
int64_t esp_timer_get_time() {return (int64_t)ticks*1000;}
void yield() {}
bool timeRetrieved=true;
int64_t ecuNow() {return 1800000000;}
int currentUtcOffsetMinutes=-360;
int inverterCount=2;
struct Props {int invType=3;char invSerial[13]="901000010817";bool conPanels[4]={true,true,false,true};} Inv_Prop[2];
struct Data {char firmwareVersion[20]="5.123";} Inv_Data[2];
size_t strlcpy(char *d,const char *s,size_t n) {size_t len=strlen(s);size_t k=min(len,n-1);memcpy(d,s,k);d[k]=0;return len;}
std::vector<uint8_t> disk;
bool failWrite=false;
struct File {
  bool valid=false;size_t position=0;
  operator bool() const {return valid;}
  size_t size() {return disk.size();}
  size_t read(uint8_t *out,size_t n) {
    if(!valid||position>disk.size()) return 0;
    n=min(n,disk.size()-position);memcpy(out,disk.data()+position,n);position+=n;return n;
  }
  size_t write(const uint8_t *in,size_t n) {
    if(!valid||failWrite) return 0;
    if(position+n>disk.size()) disk.resize(position+n);
    memcpy(disk.data()+position,in,n);position+=n;return n;
  }
  bool seek(size_t offset) {position=offset;return valid;}
  void close() {}
};
struct FS {
  size_t available=2000000;
  size_t totalBytes() {return available;}
  size_t usedBytes() {return disk.size();}
  File open(const char *,const char *mode) {
    File f;if(!strcmp(mode,"w")) {disk.clear();f.valid=true;}
    else f.valid=!disk.empty();return f;
  }
} SPIFFS;
'''
harness += source[source.index("namespace {"):source.index("String qt2CaptureJson(")] + "}\n"
harness += source[source.index("void qt2CaptureBegin("):source.index("void qt2CaptureDownload(")]
harness += r'''
Qt2CaptureRecord slot(uint32_t seq) {
  Qt2CaptureRecord r;memcpy(&r,disk.data()+(seq%qt2CaptureCapacity)*sizeof(r),sizeof(r));return r;
}
int main() {
  qt2CaptureBegin();assert(disk.empty());assert(qt2CaptureCapacity==512);
  uint8_t data[]={0x90,0x10,0xFB,0xFB};
  Inv_Prop[1].invType=2;
  qt2CaptureObserve(1,data,4,11,262,-60,200);
  qt2CaptureObserve(-1,data,4,11,262,-60,200);
  assert(qt2PendingCount==0 && disk.empty());
  qt2CaptureObserve(0,data,4,11,262,-60,200);
  assert(qt2PendingCount==1 && disk.empty()); // no disk I/O during RX
  qt2CaptureLoop();assert(qt2CaptureSequence==1);
  assert(disk.size()==512*sizeof(Qt2CaptureRecord));
  auto r=slot(1);assert(qt2CaptureValid(r));assert(r.bootId==123 && r.localEpoch==1800000000);
  assert(r.utcOffsetMinutes==-360 && r.source==11 && r.cluster==262 && r.rssi==-60 && r.lqi==200);
  assert(r.connectedMask==11 && r.length==4 && r.event==0 && !memcmp(r.data,data,4));
  assert(!strcmp(r.serial,"901000010817") && !strcmp(r.ecuVersion,"test") && !strcmp(r.inverterVersion,"5.123"));
  for(size_t i=0;i<sizeof(r);++i) {auto bad=r;((uint8_t *)&bad)[i]^=1;assert(!qt2CaptureValid(bad));}
  for(int i=0;i<5;++i) qt2CaptureObserve(0,nullptr,0,0,262,0,0);
  assert(qt2PendingCount==4 && qt2CaptureDropped==1);qt2CaptureLoop();
  assert(slot(5).event==1 && slot(5).length==0);
  for(int i=0;i<600;++i) {++ticks;qt2CaptureObserve(0,data,4,11,262,-60,200);qt2CaptureLoop();}
  assert(qt2CaptureSequence==605 && disk.size()==512*sizeof(r));
  for(uint32_t seq=94;seq<=605;++seq) {auto stored=slot(seq);assert(qt2CaptureValid(stored)&&stored.sequence==seq);}
  qt2CaptureSequence=0;qt2CaptureReady=false;qt2CaptureBegin();assert(qt2CaptureSequence==605);
  failWrite=true;qt2CaptureObserve(0,data,4,11,262,-60,200);qt2CaptureLoop();
  assert(qt2CaptureSequence==605 && qt2CaptureDropped==2);failWrite=false;
  // Partial record after a power cut must not supersede the last valid record.
  disk[(605%512)*sizeof(r)+4]^=1;
  qt2CaptureSequence=0;qt2CaptureBegin();assert(qt2CaptureSequence==604);
  std::cout<<"PASS queued RX/timeout metadata, every-byte corruption, bounded ring, restart recovery and failed writes\n";
  disk.clear();SPIFFS.available=500000;qt2CaptureReady=false;qt2CaptureSequence=0;
  qt2CaptureRetryAt=0;qt2CaptureBegin();assert(qt2CaptureCapacity==128);
  qt2CaptureObserve(0,data,4,11,262,-60,200);qt2CaptureLoop();assert(disk.size()==128*sizeof(r));
  disk.clear();SPIFFS.available=100000;qt2CaptureReady=false;qt2CaptureRetryAt=0;
  uint32_t dropped=qt2CaptureDropped;
  qt2CaptureObserve(0,data,4,11,262,-60,200);qt2CaptureLoop();
  assert(disk.empty() && qt2CaptureDropped==dropped+1);
  std::cout<<"PASS smaller 4 MB capture capacity and low-space protection\n";
}
'''
with tempfile.TemporaryDirectory(prefix="ecu-qt2-capture-") as directory:
    cpp = Path(directory) / "test.cpp"
    binary = Path(directory) / "test"
    cpp.write_text(harness, encoding="utf-8")
    subprocess.run([os.environ.get("CXX", "g++"), "-std=c++11", "-g",
                    "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
                    "-I", str(ROOT), str(cpp), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
