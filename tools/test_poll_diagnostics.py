"""Exercise production poll recorder with byte-backed flash and fake clocks."""
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

using std::min;
struct String : std::string {
  using std::string::string;
  String(const std::string &s):std::string(s){}
  String(int n):std::string(std::to_string(n)){}
  void toCharArray(char *out,size_t n){snprintf(out,n,"%s",c_str());}
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
int writeAttempts=0;
struct File {
  std::string name;
  bool directory=false;
  size_t position=0;
  explicit operator bool() const {return !name.empty();}
  bool isDirectory() const {return directory;}
  size_t size(){return directory ? 0 : disk[name].size();}
  bool seek(size_t p){if(directory || p>size())return false;position=p;return true;}
  size_t write(const uint8_t *p,size_t n){
    ++writeAttempts;
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

#define IRAM_ATTR
using portMUX_TYPE=int;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(x) (void)(x)
#define portEXIT_CRITICAL(x) (void)(x)
#define portENTER_CRITICAL_ISR(x) (void)(x)
#define portEXIT_CRITICAL_ISR(x) (void)(x)
#include "POLL_DIAGNOSTICS.h"
bool flightRecorderEnabled=false,timeRetrieved=true;
uint32_t esp_random(){return 12345;}
void yield(){}
struct Inv {char invSerial[13];char invID[5];} Inv_Prop[9]={{"409000043225","15A3"}};
bool apsRadioLoadPeer(const char*,uint16_t *pan,uint16_t *source){*pan=0xA3D8;*source=0xA315;return true;}
'''
code += (root / 'POLL_DIAGNOSTICS.ino').read_text(encoding='utf-8')
code += r'''
int zigbeeUp=1,errorCode=0,decodes=0,failDecodes=0,telemetry=0;
bool polled[9]={},sendOk=true;
int64_t inverterLastPollSuccess[9]={};
String ECU_REVERSE(){return "80971B01A3D8";}
void delayMicroseconds(int){} void delay(int n){tick+=n;}
void consoleOut(String){} void diagnosticsAppend(String){} void empty_serial2(){}
bool sendZB(char*){return sendOk;}
int decodePollAnswer(int){return decodes++<failDecodes?50:0;}
void energyRecordTelemetry(int){++telemetry;}void haTelemetry(int){}void mqttPoll(int){}
'''
code += (root / 'ZIGBEE_POLLING.ino').read_text(encoding='utf-8')
code += r'''
const int WL_CONNECTED=3;
struct {int status(){return WL_CONNECTED;}void reconnect(){}} WiFi;
struct {uint32_t getFreeHeap(){return 100000;}} ESP;
uint32_t flightWifiEventHandled=0,flightWifiEventCount=0,flightWifiLostAtMs=0;
uint32_t flightLastReconnectMs=0,flightLastWriteMs=0;
bool flightStationManaged=false,flightWifiWasConnected=false;
const uint32_t FLIGHT_INTERVAL_MS=60000;
enum {FLIGHT_WIFI_LOST,FLIGHT_WIFI_RESTORED,FLIGHT_WIFI_RECONNECT,FLIGHT_LOW_MEMORY,FLIGHT_HEARTBEAT};
void flightWrite(uint8_t){if(!writesFail)flightLastWriteMs=millis();}
'''
flight_source = (root / 'FLIGHT_RECORDER.ino').read_text(encoding='utf-8')
start = flight_source.index('void flightRecorderLoop()')
end = flight_source.index('String flightRecorderReport(', start)
code += flight_source[start:end]
code += r'''
int failed=0;
void check(bool ok,const char *name){std::cout<<(ok?"PASS ":"FAIL ")<<name<<'\n';failed+=!ok;}
void attempt(bool success=true){pollDiagnosticsStart(0,success?1:2);pollDiagnosticsCount(PD_CCA,3);pollDiagnosticsFinish(true,success?0:50);}
void reboot(){
 pollDiagSequence=pollDiagSaved=0;pollDiagActive=false;pollDiagWriteFailed=false;pollDiagPendingLost=0;
 memset(pollDiagRing,0,sizeof(pollDiagRing));memset(pollDiagPersist,0,sizeof(pollDiagPersist));
 pollDiagnosticsBegin();
}
int main(){
 pollDiagnosticsBegin();
 check(!SPIFFS.exists(POLL_DIAG_FILE),"startup/download do not create flash log");
 pollDiagnosticsCount(PD_CCA,100);
 pollDiagnosticsStart(0,1);
 pollDiagnosticsRaw(0xFFFF,0xA315);pollDiagnosticsRaw(0xA3D8,0xA315);
 pollDiagnosticsCount(PD_CCA,2);pollDiagnosticsRawDrop();pollDiagnosticsReply(3);pollDiagnosticsReply(0);
 tick+=3200;pollDiagnosticsFinish(false,50);
 auto r=pollDiagRing[0];
 check(r.counters[PD_CCA]==2&&r.counters[PD_RAW_DROP]==1&&r.counters[PD_TARGET_RX]==1&&
       r.counters[PD_TARGET_ASDU]==1&&r.replyMask==9&&r.result==50&&!r.txOk&&r.elapsedMs>=3200,
       "real attempt records counter deltas, target PAN/source, identities, timeout, failed TX");
 check(pollDiagValid(r),"record checksum valid");
 pollDiagnosticsFlush();check(!SPIFFS.exists(POLL_DIAG_FILE),"disabled flight recorder performs no writes");
 flightRecorderEnabled=true;pollDiagnosticsFlush();
 check(!SPIFFS.exists(POLL_DIAG_FILE),"enabling does not retroactively persist disabled attempts");
 attempt(false);check(!SPIFFS.exists(POLL_DIAG_FILE),"attempt completion buffers without flash writes");
 pollDiagnosticsFlush();
 check(SPIFFS.exists(POLL_DIAG_FILE)&&pollDiagSaved==2,"heartbeat flush persists enabled attempts");
 auto text=pollDiagnosticsReport(256);
 check(text.find("flash,2,")!=std::string::npos&&text.find("ram,1,")!=std::string::npos&&
       text.find("409000043225")==std::string::npos,"report merges flash/RAM without full serial");
 flightRecorderEnabled=false;reboot();
 check(pollDiagSequence==2&&pollDiagnosticsReport(256).find("flash,2,")!=std::string::npos,
       "persistent log survives reboot and remains downloadable when disabled");
 flightRecorderEnabled=true;
 for(size_t i=0;i<POLL_DIAG_RAM+3;++i)attempt();
 check(pollDiagPendingLost==3,"pending RAM overflow explicitly counted");
 pollDiagnosticsFlush();check(!pollDiagWriteFailed,"overflow survivors flush");
 for(size_t i=0;i<POLL_DIAG_SLOTS+5;++i){attempt();pollDiagnosticsFlush();}
 check(disk[POLL_DIAG_FILE].size()==POLL_DIAG_SLOTS*sizeof(PollDiagRecord),"persistent storage remains bounded");
 uint32_t last=pollDiagSequence;reboot();check(pollDiagSequence==last,"sequence recovers after ring rotation");
 text=pollDiagnosticsReport(256);size_t rows=0,pos=0;
 while((pos=text.find("\nflash,",pos))!=std::string::npos){++rows;++pos;}
 check(rows==256&&sizeof(PollDiagRecord)==160,"full download retains 256 records in a 40 KiB file");
 auto &bytes=disk[POLL_DIAG_FILE];bytes[((last-1)%POLL_DIAG_SLOTS)*sizeof(PollDiagRecord)]^=1;
 reboot();check(pollDiagSequence==last-1,"corrupt newest record rejected during recovery");
 writesFail=true;attempt();pollDiagnosticsFlush();
 check(pollDiagnosticsReport(3).find("storage_write_failed=1")!=std::string::npos&&
       pollDiagPersist[(pollDiagSequence-1)%POLL_DIAG_RAM],"failed writes visible and pending record retained");
 writesFail=false;pollDiagnosticsFlush();check(pollDiagSaved==pollDiagSequence,"pending record retries on next flush");
 // Unsigned millisecond and counter deltas survive wrap within an attempt.
 tick=0xfffffff0;pollDiagTotals[PD_CCA]=0xfffffffe;pollDiagnosticsStart(0,1);
 tick=20;pollDiagnosticsCount(PD_CCA,5);pollDiagnosticsFinish(true,0);
 r=pollDiagRing[(pollDiagSequence-1)%POLL_DIAG_RAM];
 check(r.elapsedMs==36&&r.counters[PD_CCA]==5,"millis and counter wrap handled");
 decodes=0;failDecodes=1;telemetry=0;polling(0);
 auto first=pollDiagRing[(pollDiagSequence-2)%POLL_DIAG_RAM];
 auto second=pollDiagRing[(pollDiagSequence-1)%POLL_DIAG_RAM];
 check(first.attempt==1&&first.result==50&&second.attempt==2&&second.result==0&&
       decodes==2&&polled[0]&&telemetry==1,"production polling records timeout then recovery without duplicate telemetry");
 decodes=0;failDecodes=2;telemetry=0;sendOk=false;polling(0);
 second=pollDiagRing[(pollDiagSequence-1)%POLL_DIAG_RAM];
 check(decodes==2&&!polled[0]&&telemetry==0&&!second.txOk&&second.result==50,
       "production polling preserves exhausted retry and TX failure outcomes");
 decodes=0;failDecodes=0;sendOk=true;polling(0);
 check(decodes==1&&polled[0],"production polling does not add a retry on success");
 tick=100000;writesFail=true;writeAttempts=0;flightRecorderLoop();
 int initialWrites=writeAttempts;
 tick=100050;flightRecorderLoop();
 check(initialWrites>0&&writeAttempts==initialWrites,"real flight loop rate-limits poll storage failure to once per minute");
 tick=160001;flightRecorderLoop();
 check(writeAttempts>initialWrites,"real flight loop retries pending batch after one minute");
 flightRecorderEnabled=false;writesFail=false;initialWrites=writeAttempts;tick=230000;flightRecorderLoop();
 check(writeAttempts==initialWrites,"real flight loop performs no companion writes when recording is disabled");
 return failed?1:0;
}
'''
with tempfile.TemporaryDirectory(prefix="poll-diagnostics-") as directory:
    cpp, binary = Path(directory)/"test.cpp", Path(directory)/"test"
    cpp.write_text(code, encoding="utf-8")
    subprocess.run([os.environ.get("CXX", "g++"), "-std=c++11", "-Wall", "-Wextra",
                    "-Werror", "-I", str(root), str(cpp), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
