"""Run actual persistent pairing logger with a byte-backed flash substitute."""
from pathlib import Path
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
code = r'''
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <map>
#include <string>
#include <vector>
#include "PAIRING_AUDIT.h"
using std::min;
struct String : std::string {
  using std::string::string;
  String(const std::string &s):std::string(s){}
};
#define F(x) x
#define VERSION "test-firmware"
size_t strlcpy(char *out,const char *in,size_t n) {snprintf(out,n,"%s",in);return strlen(in);}
using SemaphoreHandle_t = int*;
const int portMAX_DELAY=0;
int *xSemaphoreCreateMutex(){static int mutex;return &mutex;}
void xSemaphoreTake(int*,int){}
void xSemaphoreGive(int*){}
uint32_t tick=100;
uint32_t millis(){return tick++;}
int64_t ecuNow(){return 1788890000;}
std::map<std::string,std::vector<uint8_t>> disk;
bool writesFail=false;
struct File {
  std::string name;
  bool directory=false;
  size_t position=0;
  explicit operator bool() const {return !name.empty();}
  bool isDirectory() const {return directory;}
  size_t size(){return directory ? 0 : disk[name].size();}
  bool seek(size_t p){if(directory || p>size())return false;position=p;return true;}
  size_t write(const uint8_t *p,size_t n){
    if(directory || writesFail)return 0;
    auto &b=disk[name];if(position+n>b.size())b.resize(position+n);
    std::copy(p,p+n,b.begin()+position);position+=n;return n;
  }
  size_t read(uint8_t *p,size_t n){
    if(directory)return 0;
    n=min(n,size()-position);auto &b=disk[name];
    std::copy(b.begin()+position,b.begin()+position+n,p);position+=n;return n;
  }
  void flush(){} void close(){name.clear();directory=false;}
};
struct FakeFS {
  bool exists(const char *name){return disk.count(name);}
  File open(const char *name,const char *mode){
    if(mode[0]=='w')disk[name].clear();
    File f;
    // Arduino VFS falls back to opendir after stat fails. SPIFFS accepts
    // virtual directory paths, so missing read/update files can be truthy.
    f.name=name;f.directory=!disk.count(name);return f;
  }
} SPIFFS;
'''
code += (root / "PAIRING_AUDIT.ino").read_text()
code += r'''
int failed=0;
void check(bool ok,const char *name){std::cout<<(ok?"PASS ":"FAIL ")<<name<<'\n';failed+=!ok;}
void reboot(){pairAuditSequence=0;pairAuditWriting=false;pairAuditBeginStorage();}
int main(){
  pairAuditBeginStorage();
  check(pairingAuditReport(24).find("attempt,firmware")!=std::string::npos,"empty log download");
  check(!SPIFFS.exists(PAIR_AUDIT_FILE),"reading missing log does not create it");
  pairAuditBegin(0,"704000007719");
  check(SPIFFS.exists(PAIR_AUDIT_FILE) && !pairAuditWriteFailed,
        "first attempt creates a regular file despite truthy missing-file directory handle");
  check(pairAuditCurrent.count==1 && pairAuditCurrent.result==PA_PENDING,"start saved before handshake");
  pairAuditStep(PA_COMMAND,true,1,0xFFFF);
  pairAuditEvent(PA_RX_SUMMARY,true,0,0xA3D8,0x5AF2,8,17,-47,10,false);
  pairAuditStep(PA_DONE,true,0,0xA3D8);
  check(pairAuditCurrent.result==PA_OK && pairAuditValid(pairAuditCurrent),"complete checksummed snapshot");
  auto report=pairingAuditReport(24);
  check(report.find("receive-summary")!=std::string::npos && report.find(",8,17,-47,10,")!=std::string::npos,
        "replies, overflow and radio quality exported");
  auto &raw=disk[PAIR_AUDIT_FILE];
  std::string stored(raw.begin(),raw.end());
  check(report.find("704000007719")==std::string::npos && stored.find("704000007719")==std::string::npos &&
        report.find("7719")!=std::string::npos,"full serial absent in flash and download");
  reboot();check(pairAuditSequence==1 && pairingAuditReport(24).find("complete")!=std::string::npos,
                 "completed attempt survives reboot");
  pairAuditBegin(1,"704000005525");pairAuditStep(PA_COMMAND,true,0,0xFFFF);reboot();
  check(pairAuditSequence==2 && pairingAuditReport(24).find("5525")!=std::string::npos,
        "interrupted attempt survives reboot as pending");
  for(int i=0;i<30;++i){pairAuditBegin(0,"704000007719");pairAuditStep(PA_DONE,false,0,0);}
  check(disk[PAIR_AUDIT_FILE].size()==24*sizeof(PairAuditRecord),"flash use bounded to 24 slots");
  reboot();check(pairAuditSequence==32,"sequence recovers after rotation");
  int valid=0;uint32_t oldest=999;
  for(size_t p=0;p<raw.size();p+=sizeof(PairAuditRecord)){
    PairAuditRecord r;memcpy(&r,raw.data()+p,sizeof(r));
    if(pairAuditValid(r)){++valid;oldest=min(oldest,r.sequence);}
  }
  check(valid==24 && oldest==9,"oldest attempts evicted, newest 24 retained");
  raw[((32-1)%24)*sizeof(PairAuditRecord)]^=1;reboot();
  check(pairAuditSequence==31,"torn or corrupt record rejected");
  pairAuditBegin(0,"704000007719");
  for(int i=0;i<25;++i)pairAuditStep(PA_COMMAND,true,i,0xFFFF);
  pairAuditStep(PA_DONE,false,0,0);
  check(pairAuditCurrent.count==20 && pairAuditCurrent.droppedEvents &&
        pairAuditCurrent.events[19].stage==PA_DONE && pairAuditCurrent.result==PA_FAILED,
        "event overflow preserves final outcome and records dropped count");
  writesFail=true;pairAuditBegin(0,"secret-not-a-serial");
  check(pairingAuditReport(24).find("WARNING")!=std::string::npos,"flash write failure visible");
  check(pairingAuditReport(24).find("secret-not-a-serial")==std::string::npos,"arbitrary input never persisted");
  return failed?1:0;
}
'''
with tempfile.TemporaryDirectory(prefix="pairing-audit-") as directory:
    cpp, binary = Path(directory)/"test.cpp", Path(directory)/"test"
    cpp.write_text(code)
    subprocess.run([os.environ.get("CXX", "g++"), "-std=c++11", "-Wall", "-Wextra",
                    "-Werror", "-I", str(root), str(cpp), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
