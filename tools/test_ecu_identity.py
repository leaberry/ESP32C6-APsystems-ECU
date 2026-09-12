"""Run actual identity boot/storage logic with ArduinoJson and fake flash/NVS.

Requires g++; ARDUINOJSON_INCLUDE can point to ArduinoJson's src directory.
"""
from pathlib import Path
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
include = Path(os.environ.get('ARDUINOJSON_INCLUDE',
                             Path.home() / 'Arduino/libraries/ArduinoJson/src'))
source = (root / 'ECU_IDENTITY.ino').read_text().replace('#include <nvs.h>', '')
harness = r'''
#include <ArduinoJson.h>
#include <cassert>
#include <cstring>
#include <string>
#include <map>
#include <iostream>
#include <algorithm>
#define F(x) x
struct { void println(const char *) {} void print(const char *) {} } Serial;
using nvs_iterator_t = void *;
using esp_err_t = int;
const int ESP_ERR_NVS_NOT_FOUND = 1, NVS_TYPE_ANY = 0;
int nvsResult = ESP_ERR_NVS_NOT_FOUND;
int nvs_entry_find(const char *,const char *,int,void **) { return nvsResult; }
void nvs_release_iterator(void *) {}
uint32_t rng = 42, randomCalls = 0;
uint32_t esp_random() { ++randomCalls; return rng = rng*1664525U+1013904223U; }
char ECU_ID[13] = "D8A3011B9780";
// Read the production inverter record layout below, not a parallel definition.
'''
includes = (root / 'AAA_INCLUDES.h').read_text()
end = includes.index('} inverters;') + len('} inverters;')
start = includes.rfind('typedef struct', 0, end)
harness += includes[start:end]
harness += r'''
std::map<std::string,std::string> files;
std::string failOpen;
bool shortWrite = false;
int renameCalls = 0, failRenameAt = 0;
struct File {
  std::string name;
  size_t pos = 0;
  operator bool() const { return !name.empty(); }
  size_t size() const { return name.empty() ? 0 : files.at(name).size(); }
  int read() { return pos<size() ? (unsigned char)files[name][pos++] : -1; }
  size_t read(uint8_t *out,size_t n) {
    size_t take = std::min(n,size()-pos);
    memcpy(out,files[name].data()+pos,take); pos+=take; return take;
  }
  size_t readBytes(char *out,size_t n) { return read((uint8_t *)out,n); }
  size_t write(const uint8_t *p,size_t n) {
    if(shortWrite) return 0;
    files[name].append((const char *)p,n); return n;
  }
  size_t write(uint8_t c) { return write(&c,1); }
  void flush() {} void close() {}
};
struct {
  bool exists(const char *p) { return files.count(p); }
  File open(const char *p,const char *mode) {
    if(failOpen==p) return {};
    if(mode[0]=='w') files[p].clear();
    if(!files.count(p)) return {};
    File f; f.name=p; return f;
  }
  bool remove(const char *p) { return files.erase(p); }
  bool rename(const char *a,const char *b) {
    if(++renameCalls==failRenameAt || !files.count(a) || files.count(b)) return false;
    files[b]=files[a];files.erase(a);return true;
  }
} SPIFFS;
const char *fleetName="Test fleet", *userPwd="test-password";
int inverterCount=0, pollOffset=0, pollIntervalSeconds=300;
bool Polling=true, sunspecEnabled=true, flightRecorderEnabled=false;
size_t strlcpy(char *out,const char *in,size_t n) { snprintf(out,n,"%s",in);return strlen(in); }
'''
storage = (root / 'SPIFFS_RW.ino').read_text()
harness += storage[storage.index('void basisConfigDocument('):storage.index('void mqttConfigsave(')]
harness += source
harness += r'''
void reset() {
  files.clear();failOpen.clear();shortWrite=false;renameCalls=failRenameAt=0;
  nvsResult=ESP_ERR_NVS_NOT_FOUND;strcpy(ECU_ID,ECU_DEFAULT_ID);randomCalls=0;
}
void record(const char *path,const char *id) {
  inverters inv;strlcpy(inv.invID,id,sizeof(inv.invID));
  files[path]=std::string((const char *)&inv,sizeof(inv));
}
int main() {
  reset();ecuIdentityBegin();std::string generated=ECU_ID;
  assert(generated!=ECU_DEFAULT_ID && generated.size()==12 && randomCalls==2);
  JsonDocument saved;assert(!deserializeJson(saved,files[ECU_CONFIG_PATH]));
  assert(saved["ECU_ID"].as<std::string>()==generated);
  // Simulate reading the saved configuration on a second boot.
  strcpy(ECU_ID,saved["ECU_ID"].as<const char *>());ecuIdentityBegin();
  assert(generated==ECU_ID && randomCalls==2);
  basisConfigsave();assert(!deserializeJson(saved,files[ECU_CONFIG_PATH]));
  assert(saved["ECU_ID"].as<std::string>()==generated && saved["fleetName"]==fleetName);
  for(int i=0;i<10000;++i) {
    char id[13];ecuIdentityGenerate(id);
    assert(strlen(id)==12 && strcmp(id,ECU_DEFAULT_ID));
    assert(strncmp(id,"0000",4) && strncmp(id,"FFFF",4));
    assert(std::string(id).find_first_not_of("0123456789ABCDEF")==std::string::npos);
  }
  reset();strcpy(ECU_ID,"ABCDEF123456");ecuIdentityBegin();
  assert(!randomCalls && files.empty());
  reset();strcpy(ECU_ID,"d8a3011b9780");ecuIdentityBegin();assert(randomCalls==2);
  for(const char *path : {"/Inv_Prop8.str","/Inv_Prop0.str.pair","/Inv_Prop0.str.pair-old"}) {
    reset();record(path,"1234");ecuIdentityBegin();
    assert(!strcmp(ECU_ID,ECU_DEFAULT_ID) && !randomCalls);
  }
  reset();record("/Inv_Prop7.str","0000");ecuIdentityBegin();assert(randomCalls==2);
  reset();record("/Inv_Prop7.str","1111");ecuIdentityBegin();assert(!randomCalls);
  reset();files["/Inv_Prop4.str"]="truncated";ecuIdentityBegin();assert(!randomCalls);
  reset();record("/Inv_Prop0.str","0000");failOpen="/Inv_Prop0.str";
  ecuIdentityBegin();assert(!randomCalls);
  for(int result : {0,2}) { // Orphan NVS peer or storage error.
    reset();nvsResult=result;ecuIdentityBegin();assert(!randomCalls);
  }
  const std::string original=R"({"ECU_ID":"D8A3011B9780","Polling":false,"unknown":{"keep":42}})";
  reset();files[ECU_CONFIG_PATH]=original;ecuIdentityBegin();
  assert(!deserializeJson(saved,files[ECU_CONFIG_PATH]));
  assert(saved["Polling"]==false && saved["unknown"]["keep"]==42);
  for(int failure=0;failure<4;++failure) {
    reset();files[ECU_CONFIG_PATH]=original;
    if(failure==0) failOpen=ECU_CONFIG_TEMP;
    if(failure==1) shortWrite=true;
    if(failure==2) failRenameAt=1;
    if(failure==3) failRenameAt=2;
    ecuIdentityBegin();assert(!strcmp(ECU_ID,ECU_DEFAULT_ID));
    assert(files[ECU_CONFIG_PATH]==original);
  }
  reset();files[ECU_CONFIG_PATH]="bad json";ecuIdentityBegin();
  assert(!strcmp(ECU_ID,ECU_DEFAULT_ID) && files[ECU_CONFIG_PATH]=="bad json");
  reset();files[ECU_CONFIG_OLD]=original;files[ECU_CONFIG_TEMP]="unfinished";
  assert(ecuIdentityRecoverConfig() && files[ECU_CONFIG_PATH]==original);
  reset();files[ECU_CONFIG_OLD]=original;failRenameAt=1;
  assert(!ecuIdentityRecoverConfig());
  std::cout<<"PASS ECU identity: generation, reboot, custom IDs, pairing guards, config preservation, write failures and interrupted-save recovery\n";
}
'''
with tempfile.TemporaryDirectory() as directory:
    cpp, binary = Path(directory) / 'test.cpp', Path(directory) / 'test'
    cpp.write_text(harness)
    subprocess.run([os.environ.get('CXX','g++'), '-std=c++11', '-Wall', '-Wextra',
                    '-Werror', '-I', str(include), str(cpp), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
