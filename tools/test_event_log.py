"""Exercise the production log writer and renderer, including issue #24's crash."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
includes = (root/'AAA_INCLUDES.h').read_text(encoding='utf-8')
start = includes.rindex('typedef struct', 0, includes.index('} logEvent;'))
struct = includes[start:includes.index('} logEvent;')+len('} logEvent;')]
source = (root/'AAA_LOG.ino').read_text(encoding='utf-8')
source = source[source.index('void Update_Log('):]
escape = (root/'WEB_UI.ino').read_text(encoding='utf-8').split('\nString ecuPageStart')[0]
code = r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>
#include <iostream>
using String=std::string;
#define F(x) x
using byte=uint8_t;
constexpr int Log_MaxEvents=18;
time_t ecuNow(){return 0;}
int ecuDay(time_t){return 21;}int ecuHour(time_t){return 12;}
int ecuMinute(time_t){return 0;}int ecuSecond(time_t){return 0;}
''' + struct + r'''
struct {uint8_t before[32];logEvent events[Log_MaxEvents];uint8_t after[32];} memory;
#define Log_Events memory.events
byte logNr=0;
bool Log_MaxReached=false;
''' + escape + source + r'''
void guards(){for(auto b:memory.before)assert(b==0x5a);for(auto b:memory.after)assert(b==0x5a);}
int main(){
 memset(&memory,0x5a,sizeof(memory));
 for(int i=0;i<Log_MaxEvents-1;++i)Update_Log(1,"ok");
 // Exact last-slot write that used to overwrite adjacent String's pointer with 'aile'.
 Update_Log(2,"throttle inv 0 failed");guards();
 assert(logNr==0&&Log_MaxReached);
 assert(!strcmp(Log_Events[17].message,"throttle inv 0 failed"));
 assert(putList("rows").find("throttle inv 0 failed")!=String::npos);
 // Longer messages must truncate with a terminator, at every slot and after wrapping.
 const String longMessage(4096,'X');
 for(int i=0;i<2*Log_MaxEvents;++i){
  int slot=logNr;Update_Log(4,longMessage.c_str());guards();
  assert(strlen(Log_Events[slot].message)==sizeof(Log_Events[slot].message)-1);
  assert(Log_Events[slot].date[sizeof(Log_Events[slot].date)-1]==0 || strlen(Log_Events[slot].date)<sizeof(Log_Events[slot].date));
 }
 String rendered=putList("rows");size_t count=0,pos=0;
 while((pos=rendered.find("</tr>",pos))!=String::npos){++count;pos+=5;}
 assert(count==18&&rendered.size()>1536); // Former fixed renderer buffer was too small.
 Update_Log(99,"<&\"'>");rendered=putList("rows");
 assert(rendered.find("unknown")!=String::npos&&rendered.find("&lt;&amp;&quot;&#39;&gt;")!=String::npos);
 Update_Log(1,nullptr);assert(Log_Events[(logNr+17)%18].message[0]==0);guards();
 assert(putList("other").empty());
 std::cout<<"PASS event log: crash-triggering last slot, long messages, ring wrap, complete HTML and escaping\n";
}
'''
with tempfile.TemporaryDirectory() as tmp:
    p = Path(tmp)
    (p/'test.cpp').write_text(code, encoding='utf-8')
    subprocess.run(['g++', '-std=c++17', '-Wall', '-Wextra', '-fsanitize=undefined',
                    str(p/'test.cpp'), '-o', str(p/'test')], check=True)
    subprocess.run([str(p/'test')], check=True)
