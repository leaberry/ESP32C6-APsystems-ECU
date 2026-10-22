"""Exercise manual fleet dispatch and completion through the production scheduler."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
harness = r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>
using String=std::string;
#define String(n) std::to_string(n)
#define F(s) s
constexpr int YC600_MAX_NUMBER_OF_INVERTERS=9;
uint32_t tick=1000,pollIntervalSeconds=300;
uint32_t millis(){return tick;}
bool Polling=false,daylightPolling=true,timeRetrieved=true,locationConfigured=true,dayTime=false;
int currentUtcOffsetMinutes=0,pollOffset=0,zigbeeUp=1,actionFlag=0;
float lati=0,longi=0;
struct sunMoon{bool init(int,float,float){return true;}};
time_t ecuNow(){return 100000;}
time_t ecuSunRise(sunMoon&,time_t t){return t+3600;}
struct{char invID[5];}Inv_Prop[9];
bool polled[9],succeed=true,lastSuccess=false;
int calls=0,completions=0,rounds=0;
void flightRecorderActivity(const char*){}
void consoleOut(const std::string&){}
void eventSend(int){}
void inverterInfoMaybeQuery(int){}
void pollingRoundBegin(){++rounds;}
void pollingForRound(int i){++calls;polled[i]=succeed;}
bool pollingRoundSucceeded(){return succeed;}
void haPollRoundFinished(bool ok){++completions;lastSuccess=ok;}
'''
harness += (root / 'POLL_SCHEDULER.ino').read_text(encoding='utf-8')
harness += r'''
void finish(){for(int i=0;i<12;++i){tick+=250;pollSchedulerLoop();}}
int main(){
 for(auto &p:Inv_Prop)strcpy(p.invID,"0000");
 strcpy(Inv_Prop[0].invID,"1234");strcpy(Inv_Prop[2].invID,"5678");
 pollSchedulerBegin();tick+=400000;pollSchedulerLoop();assert(!pollingWorkPending()&&calls==0);
 // Manual fleet work runs with automatic polling disabled, even at night.
 pollSchedulerRequest(true);assert(pollingWorkPending()&&!pollingRoundInProgress());
 pollSchedulerLoop();assert(pollingRoundInProgress()&&rounds==1&&calls==0);
 actionFlag=47;pollSchedulerLoop();assert(calls==0);actionFlag=0;
 finish();assert(!pollingWorkPending()&&calls==2&&completions==1&&lastSuccess);
 assert(pollingLastSuccessfulEpoch()==ecuNow());
 // Failed rounds notify completion once and do not repeat without a request.
 succeed=false;pollSchedulerRequest(true);pollSchedulerLoop();finish();
 assert(calls==4&&completions==2&&!lastSuccess);finish();assert(completions==2);
 // A radio failure before dispatch drops the request without a false completion.
 zigbeeUp=0;pollSchedulerRequest(true);pollSchedulerLoop();
 assert(!pollingWorkPending()&&completions==2&&calls==4);zigbeeUp=1;
 pollSchedulerRequest(false);pollSchedulerLoop();assert(!pollingWorkPending()&&calls==4);
 Polling=true;dayTime=true;succeed=true;tick+=400000;pollSchedulerLoop();finish();
 assert(calls==6&&completions==3&&lastSuccess);
 puts("Manual/automatic scheduler dispatch, busy handling and completion tests passed");
}
'''
with tempfile.TemporaryDirectory() as tmp:
    folder = Path(tmp)
    (folder / 'test.cpp').write_text(harness, encoding='utf-8')
    subprocess.run(['g++', '-std=c++17', str(folder / 'test.cpp'), '-o', str(folder / 'test')], check=True)
    subprocess.run([str(folder / 'test')], check=True)
