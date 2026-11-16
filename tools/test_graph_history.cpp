// Compile the production storage adapter against a fault-injected filesystem.
// A freestanding mode permits syntax checking with the installed cross-Clang.
// Execute the normal build with a host C++ compiler. No copied implementation.
#include <stdint.h>
#include <stddef.h>
#include "GRAPH_HISTORY.h"
#ifndef GRAPH_FREESTANDING_TEST
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#else
extern "C" int snprintf(char *, size_t, const char *, ...);
namespace std {
template<class T> class initializer_list {
  const T *start_; size_t size_;
 public: const T *begin() const { return start_; } const T *end() const { return start_+size_; }
};
}
extern "C" void *memcpy(void *d,const void *s,size_t n) {
  auto *a=(uint8_t*)d;auto *b=(const uint8_t*)s;while(n--)*a++=*b++;return d;
}
extern "C" void *memset(void *d,int c,size_t n) {
  auto *a=(uint8_t*)d;while(n--)*a++=(uint8_t)c;return d;
}
extern "C" size_t strlen(const char *s) { size_t n=0;while(s[n])++n;return n; }
static uint8_t ramAllocation[50000];
void *malloc(size_t n) { return n<=sizeof(ramAllocation)?ramAllocation:nullptr; }
void free(void*) {}
#endif
static bool equal(const char*a,const char*b) { while(*a&&*a==*b){++a;++b;}return *a==*b; }
struct String {
  char data[160] = {};
  String() = default;
  String(const char *s) { size_t n=strlen(s); if(n>159)n=159;memcpy(data,s,n); }
  String(unsigned long n) { char a[24];int i=0;do{a[i++]=char('0'+n%10);n/=10;}while(n);for(int k=0;k<i;++k)data[k]=a[i-k-1]; }
  String(unsigned int n):String((unsigned long)n){}
  String(int n):String((unsigned long)n){}
  const char *c_str() const { return data; }
  size_t length()const {return strlen(data);}
  long toInt()const {const char*p=data;bool neg=*p=='-';if(neg)++p;long n=0;while(*p>='0'&&*p<='9')n=n*10+*p++-'0';return neg?-n:n;}
  bool operator==(const char*s)const{return equal(data,s);}
  bool operator!=(const char*s)const{return !(*this==s);}
};
String operator+(const String&a,const String&b){String r=a;size_t n=r.length(),m=b.length();if(n+m>159)m=159-n;memcpy(r.data+n,b.data,m);return r;}
String operator+(const char*a,const String&b){return String(a)+b;}
struct FakeEntry {bool exists=false;String name;size_t size=0;uint8_t data[40000]={};};
static FakeEntry entries[35];
static int writeCalls=0,writeLimit=-1,renameCalls=0,renameFailure=0;
static int readLimit=-1;
static size_t externalBytes=0;
static constexpr int SeekSet=0;
struct File {
  int index=-1;size_t offset=0;
  bool isDirectory()const{return index==-2;}
  operator bool()const{return isDirectory()||(index>=0&&entries[index].exists);}
  size_t size()const{return index>=0&&*this?entries[index].size:0;}
  size_t available()const{return size()>offset?size()-offset:0;}
  size_t read(uint8_t*dst,size_t n){if(index<0||!*this||readLimit==0)return 0;if(readLimit>0)--readLimit;if(n>available())n=available();memcpy(dst,entries[index].data+offset,n);offset+=n;return n;}
  int read(){uint8_t c=0;return read(&c,1)?c:-1;}
  size_t write(const uint8_t*src,size_t n){++writeCalls;if(index<0||!*this||writeLimit==0||offset+n>40000)return 0;if(writeLimit>0)--writeLimit;memcpy(entries[index].data+offset,src,n);offset+=n;if(offset>entries[index].size)entries[index].size=offset;return n;}
  bool seek(size_t p,int){if(index<0||!*this||p>size())return false;offset=p;return true;}
  void flush(){} void close(){index=-1;}
};
struct FakeFS {
  int find(const String&name){for(int i=0;i<35;i++)if(entries[i].exists&&entries[i].name==name.c_str())return i;return -1;}
  bool exists(const String&name){return find(name)>=0;}
  // Arduino SPIFFS opens an absent read path as a directory handle. exists()
  // excludes those handles; their reads return zero and seeks fail.
  File open(const String&name,const char*mode){int i=find(name);if(*mode=='w'){if(i<0)for(int j=0;j<35;j++)if(!entries[j].exists){i=j;break;}if(i>=0){entries[i].exists=true;entries[i].name=name;entries[i].size=0;}}File f;f.index=i<0&&*mode=='r'?-2:i;return f;}
  bool remove(const String&name){int i=find(name);if(i<0)return false;entries[i].exists=false;return true;}
  bool rename(const String&a,const String&b){++renameCalls;if(renameFailure==renameCalls)return false;int i=find(a);if(i<0||exists(b))return false;entries[i].name=b;return true;}
  size_t totalBytes(){return 488*1024;}
  size_t usedBytes(){size_t n=externalBytes;for(auto&e:entries)if(e.exists)n+=e.size;return n;}
} SPIFFS;
using SemaphoreHandle_t=void *;
constexpr int portMAX_DELAY=-1,pdTRUE=1;
SemaphoreHandle_t xSemaphoreCreateMutex(){return reinterpret_cast<void *>(1);}
int xSemaphoreTake(SemaphoreHandle_t,int){return pdTRUE;}
void xSemaphoreGive(SemaphoreHandle_t){}
void yield(){}
struct {size_t freeHeap=200000;size_t getFreeHeap(){return freeHeap;}} ESP;
static uint32_t clockUtc=20000*86400U;
uint32_t ecuUtcNow(){return clockUtc;}
static bool timeRetrieved=true;
static bool fleetActive=false;
bool pollingRoundInProgress(){return fleetActive;}
static uint8_t inverterCount=2;
static uint32_t pollIntervalSeconds=60;
struct {char invID[13];} Inv_Prop[9]={{"123456789012"},{"123456789013"}};
struct {float pw_total;} Inv_Data[9];
bool isfinite(float p){return __builtin_isfinite(p);}
long lroundf(float p){return (long)(p+0.5f);}
struct JsonValue {template<class T>JsonValue&operator=(T){return *this;}template<class T>T to(){return T();}};
struct JsonObject{JsonValue operator[](const char*){return {};}};
struct JsonArray{template<class T>T add(){return T();}template<class T>void add(T){}};
struct JsonDocument{JsonValue operator[](const char*){return {};}};
void serializeJson(JsonDocument&,String&){}
struct AsyncResponseStream{template<class T>void print(T){}template<class...T>void printf(const char*,T...){}void addHeader(const char*,const char*){}};
using AsyncWebServerResponse=AsyncResponseStream;
static String exportedPath;
static bool exportFailed=false;
struct Param{String text;String value(){return text;}};
class AsyncWebServerRequest {
 public:
  int status=0;Param param;String key;
  bool hasParam(const char*k,bool=false){return key==k;}
  Param*getParam(const char*,bool=false){return &param;}
  template<class T>void send(int s,const char*,T){status=s;}
  void send(AsyncResponseStream*){status=200;}
  AsyncResponseStream*beginResponseStream(const char*){static AsyncResponseStream r;return &r;}
  AsyncWebServerResponse*beginResponse(FakeFS&,const String&p,const char*,bool){exportedPath=p;static AsyncWebServerResponse r;return exportFailed?nullptr:&r;}
};
#include "GRAPH_HISTORY.ino"
#define CHECK(condition) do {if(!(condition))return __LINE__;}while(0)
extern "C" int testGraphHistory() {
  using namespace graphHistory;
  CHECK(stepFor(5)==60&&stepFor(300)==300&&stepFor(301)==360&&stepFor(86400)==86400);
  CHECK(retentionStep(3,60)==60&&retentionStep(4,60)==300&&retentionStep(11,300)==3600);
  CHECK(retentionStep(4,600)==600);
  CHECK(accept(1299,1000)&&accept(1300,1000)&&!accept(900,1000)&&!accept(1000,1000));
  size_t worst=3*fileBytes(60)+7*fileBytes(300)+20*fileBytes(3600)+fileBytes(60);
  CHECK(worst<=budget&&worst<172000);
  graphBegin();graphLoop();CHECK(!graphEnabled&&!graphRam&&writeCalls==0);
  graphRecord(0);CHECK(writeCalls==0);
  graphPending=1;graphLoop();CHECK(graphEnabled&&graphRam&&graphRamHeader.step==60);
  int writes=writeCalls;
  Inv_Data[0].pw_total=100;graphRecord(0);CHECK(graphRam[0].watts==100);
  clockUtc+=30;Inv_Data[0].pw_total=999;graphRecord(0);CHECK(graphRam[0].watts==999);
  clockUtc+=30;Inv_Data[0].pw_total=200;graphRecord(0);CHECK(graphRam[9].watts==200);
  CHECK(writeCalls==writes&&graphRam[1].weight==0);
  pollIntervalSeconds=300;clockUtc+=60;graphRecord(0);CHECK(graphRam[18].weight==1);
  pollIntervalSeconds=60;
  // End of day: save once, coarsen later, and never retry writes every loop.
  clockUtc=20001*86400U;graphLoop();CHECK(SPIFFS.exists(graphPath(20000)));
  writes=writeCalls;graphLoop();graphLoop();CHECK(writeCalls==writes);
  File f=SPIFFS.open(graphPath(20000),"r");Header h={};CHECK(graphValidate(f,h)&&h.step==60);
  Cell row[9];CHECK(graphReadRow(f,h.step,row)&&row[0].watts==999&&row[1].weight==0);
  CHECK(graphReadRow(f,h.step,row)&&row[0].watts==200);f.close();
  writes=writeCalls;CHECK(!graphSaveDay(h,nullptr,h.step));CHECK(writeCalls==writes);
  // Age beyond three days; sample-weighted mean and missing channels survive.
  clockUtc=20004*86400U;graphLoop();f=SPIFFS.open(graphPath(20000),"r");
  CHECK(graphValidate(f,h)&&h.step==300);CHECK(graphReadRow(f,h.step,row));
  CHECK(row[0].watts==466&&row[0].weight==3&&row[1].weight==0);f.close();
  clockUtc=20011*86400U;graphLoop();f=SPIFFS.open(graphPath(20000),"r");
  CHECK(graphValidate(f,h)&&h.step==3600);CHECK(graphReadRow(f,h.step,row)&&row[0].watts==466&&row[0].weight==3);f.close();
  // Full flash does not consume reserve or spin retrying.
  graphRecord(0);
  externalBytes=SPIFFS.totalBytes();clockUtc+=86400;writes=writeCalls;graphLoop();
  CHECK(writeCalls==writes&&graphProblem[0]);graphLoop();CHECK(writeCalls==writes);
  externalBytes=0;
  // A failed replacement retains the previously valid day.
  f=SPIFFS.open(graphPath(20000),"r");CHECK(graphValidate(f,h));
  writeLimit=2;CHECK(!graphSaveDay(h,&f,7200));writeLimit=-1;
  f=SPIFFS.open(graphPath(20000),"r");CHECK(graphValidate(f,h)&&h.step==3600);f.close();
  // Rename failure also retains data, and orphaned temporary writes are cleared.
  f=SPIFFS.open(graphPath(20000),"r");CHECK(graphValidate(f,h));
  renameFailure=renameCalls+2;CHECK(!graphSaveDay(h,&f,7200));renameFailure=0;
  CHECK(graphRecover());f=SPIFFS.open(graphPath(20000),"r");CHECK(graphValidate(f,h)&&h.step==3600);f.close();
  CHECK(SPIFFS.rename(graphPath(20000),GRAPH_PREVIOUS));CHECK(graphRecover());
  CHECK(SPIFFS.exists(graphPath(20000))&&!SPIFFS.exists(GRAPH_PREVIOUS));
  // CRC rejects corruption; changing identity cannot mix inverter slots.
  int index=SPIFFS.find(graphPath(20000));entries[index].data[sizeof(Header)]^=1;
  f=SPIFFS.open(graphPath(20000),"r");CHECK(!graphValidate(f,h));f.close();
  Inv_Prop[0].invID[0]='9';graphLoop();CHECK(graphRamHeader.identity==graphIdentity()&&!graphRam[0].weight);
  // A full month remains bounded, with no per-poll filesystem writes.
  graphPending=0;graphLoop();CHECK(!graphEnabled&&!graphRam&&graphDiskBytes()==0);
  graphPending=1;graphLoop();
  for(int d=0;d<40;++d){
    Inv_Data[0].pw_total=100;graphRecord(0);clockUtc+=86400;graphLoop();
    CHECK(graphDiskBytes()<budget&&graphProblem[0]==0);
  }
  CHECK(!SPIFFS.exists(GRAPH_TEMP)&&!SPIFFS.exists(GRAPH_PREVIOUS));
  graphPending=0;graphLoop();CHECK(graphDiskBytes()==0&&!graphRam&&!SPIFFS.exists(GRAPH_SETTING));
  writes=writeCalls;clockUtc+=86400;graphLoop();graphRecord(0);CHECK(writeCalls==writes);
  // Low-memory enable fails visibly without affecting reporting or allocating.
  ESP.freeHeap=1000;graphPending=1;graphLoop();CHECK(!graphRam&&graphProblem[0]);
  graphPending=0;graphLoop();ESP.freeHeap=200000;

  // Healthy 90-second polls and jittered two-minute polls fill every bucket.
  // Responses straddling a boundary share their round's timestamp.
  for(uint32_t interval:{45U,90U,120U,300U}) {
    clockUtc=22000*86400U;pollIntervalSeconds=interval;graphPending=1;graphLoop();
    const uint32_t step=graphRamHeader.step;
    writes=writeCalls;fleetActive=true;
    for(uint32_t n=0;n<10;++n) {
      clockUtc=22000*86400U+n*interval+(n%2?0:1);graphPollRoundBegin();
      clockUtc+=2;Inv_Data[0].pw_total=100+n;graphRecord(0);
      clockUtc+=4;Inv_Data[1].pw_total=200+n;graphRecord(1);
    }
    for(uint32_t b=0;b<=9*interval/step;++b) CHECK(graphRam[b*9].weight&&graphRam[b*9+1].weight);
    CHECK(writeCalls==writes);
    fleetActive=false;graphPending=0;graphLoop();
  }
  clockUtc=23000*86400U;pollIntervalSeconds=120;graphPending=1;graphLoop();fleetActive=true;
  for(uint32_t n=0;n<4;++n) {
    clockUtc=23000*86400U+119+n*120;graphPollRoundBegin();
    clockUtc+=3;graphRecord(0);
    clockUtc+=5;if(n!=2)graphRecord(1);
  }
  CHECK(graphRam[0].weight&&graphRam[1].weight&&graphRam[9].weight&&graphRam[10].weight);
  CHECK(graphRam[18].weight&&!graphRam[19].weight); // A real missed inverter stays missing.
  CHECK(graphRam[27].weight&&graphRam[28].weight);
  const uint16_t prior=graphRam[27].watts;
  clockUtc-=120;graphPollRoundBegin();Inv_Data[0].pw_total=999;graphRecord(0);
  CHECK(graphRam[27].watts==prior); // Backward clock correction cannot rewrite older buckets.
  clockUtc=23001*86400U;graphLoop(); // Persist a two-minute day, then reopen it.
  f=SPIFFS.open(graphPath(23000),"r");const char *reason;uint32_t actual;
  CHECK(graphInspect(f,h,reason,actual)&&h.step==120&&actual==h.crc);f.close();
  CHECK(graphValidationFailure.count==0);
  // A stale round after midnight cannot leak its readings into the new day.
  graphRecord(1);CHECK(!graphRam[1].weight);fleetActive=false;
  // A transient read error is distinguishable from actual CRC corruption.
  f=SPIFFS.open(graphPath(23000),"r");readLimit=1;
  CHECK(!graphInspect(f,h,reason,actual)&&equal(reason,"data_read_failed"));
  graphRememberFailure(graphPath(23000),f,h,reason,actual);readLimit=-1;f.close();
  CHECK(equal(graphValidationFailure.reason,"data_read_failed")&&graphValidationFailure.day==23000);
  f=SPIFFS.open(graphPath(23000),"r");CHECK(graphValidate(f,h));f.close();
  index=SPIFFS.find(graphPath(23000));entries[index].data[sizeof(Header)]^=1;
  graphRecord(0);clockUtc=23002*86400U;graphLoop();
  CHECK(equal(graphValidationFailure.reason,"crc_mismatch"));
  CHECK(graphValidationFailure.expectedCrc!=graphValidationFailure.actualCrc);
  CHECK(SPIFFS.exists(graphPath(23000))&&SPIFFS.exists(graphPath(23001)));
  CHECK(graphValidationFailure.bytes==fileBytes(120));
  // A truncated file produces a different failure and is also preserved.
  entries[index].size-=1;
  AsyncWebServerRequest status;graphSendStatus(&status);
  CHECK(equal(graphValidationFailure.reason,"invalid_header_or_size"));
  clockUtc=23003*86400U;graphLoop();
  CHECK(equal(graphValidationFailure.reason,"invalid_header_or_size")&&SPIFFS.exists(graphPath(23000)));
  AsyncWebServerRequest download;download.key="slot";
  download.param.text=String(23000%31);writes=writeCalls;graphSendFile(&download);
  CHECK(download.status==200&&exportedPath==graphPath(23000).c_str()&&writeCalls==writes);
  exportFailed=true;graphSendFile(&download);CHECK(download.status==500);exportFailed=false;
  for(const char *bad:{"","-1","31","999","../settings","1x","x"}) {
    download.param.text=bad;graphSendFile(&download);CHECK(download.status==400);
  }
  download.param.text=String(23003%31);graphSendFile(&download);CHECK(download.status==404);
  graphPending=0;graphLoop();CHECK(graphValidationFailure.count==0);
  // Repeat the reporter's two-minute archive layout through a month of saves,
  // coarsening and expiry, then simulate a restart with the files intact.
  clockUtc=24000*86400U;pollIntervalSeconds=120;graphPending=1;graphLoop();
  for(unsigned d=0;d<40;++d) {
    for(unsigned n=0;n<8;++n) {
      clockUtc=(24000+d)*86400U+119+n*120;graphRecord(0);graphRecord(1);
    }
    clockUtc=(24001+d)*86400U;graphLoop();
    CHECK(!graphProblem[0]&&!graphValidationFailure.count&&graphDiskBytes()<budget);
    for(unsigned age=1;age<=30&&age<=d+1;++age) {
      f=SPIFFS.open(graphPath(24001+d-age),"r");CHECK(graphValidate(f,h));f.close();
    }
  }
  const size_t savedBytes=graphDiskBytes();
  free(graphRam);graphRam=nullptr;graphRamHeader={};graphRamHasSamples=false;
  graphEnabled=false;graphBegin();graphLoop();
  CHECK(graphEnabled&&graphRam&&graphDiskBytes()==savedBytes&&!graphProblem[0]);
  f=SPIFFS.open(graphPath(24039),"r");CHECK(graphValidate(f,h)&&h.step==120);f.close();
  graphPending=0;graphLoop();
  // Match the reported layout: one minute-resolution day, two two-minute
  // days, and today's RAM. Eleven status scans used to report 297 failures.
  clockUtc=20770*86400U;pollIntervalSeconds=60;graphPending=1;graphLoop();
  for(uint32_t day=20770;day<20773;++day) {
    graphRecord(0);pollIntervalSeconds=120;clockUtc=(day+1)*86400U;graphLoop();
    CHECK(!graphProblem[0]&&!graphValidationFailure.count);
  }
  CHECK(graphDiskBytes()==51912);
  f=SPIFFS.open(graphPath(20743),"r");
  CHECK(f&&f.isDirectory()&&!SPIFFS.exists(graphPath(20743))&&!f.seek(0,SeekSet));f.close();
  writes=writeCalls;
  for(int scan=0;scan<11;++scan) graphSendStatus(&status);
  CHECK(status.status==200&&!graphProblem[0]&&!graphValidationFailure.count&&writeCalls==writes);
  AsyncWebServerRequest dayRequest;dayRequest.key="day";dayRequest.param.text=String(20743);
  graphSendDay(&dayRequest);CHECK(dayRequest.status==404&&!graphValidationFailure.count);
  download.param.text="4";graphSendFile(&download);
  CHECK(download.status==404&&!graphValidationFailure.count&&writeCalls==writes);
  dayRequest.param.text=String(20770);graphSendDay(&dayRequest);CHECK(dayRequest.status==200);
  // Existing corrupt and empty files must still warn and remain exportable.
  index=SPIFFS.find(graphPath(20771));entries[index].data[sizeof(Header)]^=1;
  dayRequest.param.text=String(20771);graphSendDay(&dayRequest);
  CHECK(dayRequest.status==404&&equal(graphValidationFailure.reason,"crc_mismatch"));
  CHECK(graphValidationFailure.count==1&&graphValidationFailure.day==20771);
  for(int scan=0;scan<11;++scan) graphSendStatus(&status);
  CHECK(graphValidationFailure.count==1&&graphValidationFailure.day==20771);
  f=SPIFFS.open(graphPath(20743),"w");f.close();writes=writeCalls;
  graphSendStatus(&status);
  CHECK(equal(graphValidationFailure.reason,"header_read_failed")&&graphValidationFailure.bytes==0);
  CHECK(equal(graphValidationFailure.file,"/graph-4.bin")&&graphValidationFailure.count==2);
  graphSendFile(&download);CHECK(download.status==200&&SPIFFS.exists(graphPath(20743))&&writeCalls==writes);
  CHECK(graphDiskBytes()==51912);
  graphPending=0;graphLoop();CHECK(graphDiskBytes()==0&&!graphValidationFailure.count);
  return 0;
}
#ifndef GRAPH_FREESTANDING_TEST
int main(){int result=testGraphHistory();if(result)std::printf("FAIL line %d\n",result);return result?1:0;}
#endif
