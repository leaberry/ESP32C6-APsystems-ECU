"""Run both production recorders through disable, clear, restart and reuse."""
import ast
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
# Reuse the byte-backed filesystem/Arduino fake without executing its test runner.
tree = ast.parse((root/'tools/test_poll_diagnostics.py').read_text(encoding='utf-8'))
code = next(ast.literal_eval(node.value) for node in tree.body if isinstance(node, ast.Assign)
            and any(isinstance(t, ast.Name) and t.id == 'code' for t in node.targets))
code += r'''
#include <cassert>
#include <cmath>
#include <ctime>
#define FILE_READ "r"
#define FILE_WRITE "w"
int healthLocks=0,maxHealthLocks=0;
constexpr int pdTRUE=1,WL_CONNECTED=3;
int* xSemaphoreCreateRecursiveMutex(){return xSemaphoreCreateMutex();}
int xSemaphoreTakeRecursive(int*,int){++healthLocks;maxHealthLocks=std::max(healthLocks,maxHealthLocks);return pdTRUE;}
void xSemaphoreGiveRecursive(int*){assert(healthLocks>0);--healthLocks;}
struct {template<class... T>void printf(T...){} template<class T>void println(T){}} Serial;
struct {int status(){return WL_CONNECTED;}int RSSI(){return -50;}void reconnect(){}} WiFi;
struct {uint32_t getFreeHeap(){return 160000;}uint32_t getMinFreeHeap(){return 150000;}} ESP;
constexpr int MALLOC_CAP_8BIT=1;
uint32_t heap_caps_get_largest_free_block(int){return 80000;}
void* sunspecTaskHandle=nullptr;
int uxTaskGetStackHighWaterMark(void*){return 512;}
int rawRadioStackHighWaterWords(){return 512;}
int lastWifiDisconnectReason=0;
float systemTemperatureCurrentC(){return 45;}
bool pollingRoundInProgress(){return false;}
uint8_t pollSchedulerCurrentInverter(){return 0xff;}
int esp_reset_reason(){return 3;}
'''
code += (root/'POLL_DIAGNOSTICS.ino').read_text(encoding='utf-8')
code += (root/'FLIGHT_RECORDER.ino').read_text(encoding='utf-8')
code += r'''
int main(){
 flightRecorderBegin();pollDiagnosticsBegin();assert(disk.empty());
 flightRecorderSetEnabled(true);assert(flightRecorderEnabled&&pollDiagEnabled&&flightSequence==1);
 assert(disk[FLIGHT_FILE].size()==720*sizeof(FlightRecord));
 pollDiagnosticsStart(0,1);pollDiagnosticsFinish(true,0);pollDiagnosticsFlush();
 assert(disk.count(POLL_DIAG_FILE));
 flightRecorderSetEnabled(false);assert(!flightRecorderEnabled&&!pollDiagEnabled);
 // Simulate reboot while disabled: retained health records still download.
 auto savedSequence=flightSequence;flightSequence=0;flightStorageReady=false;
 auto writes=writeAttempts;flightRecorderBegin();assert(flightSequence==savedSequence&&writeAttempts==writes);
 assert(flightRecorderReport(720).find("enabled")!=String::npos);
 disk["/poll-diagnostics-v1.bin"]={1};disk["/settings.json"]={2};disk["/history.bin"]={3};
 assert(flightRecorderClear());assert(writeAttempts==writes&&flightSequence==0&&pollDiagSequence==0);
 assert(disk.size()==2&&disk.count("/settings.json")&&disk.count("/history.bin"));
 assert(flightRecorderReport(720).find("no-records")!=String::npos);
 tick+=120000;flightRecorderLoop();assert(writeAttempts==writes&&disk.size()==2);
 flightRecorderSetEnabled(true);assert(flightSequence==1&&flightRecorderEnabled&&pollDiagEnabled);
 assert(flightRecorderClear());assert(!flightStorageReady&&!disk.count(FLIGHT_FILE));
 writes=writeAttempts;tick+=100;flightRecorderLoop();assert(writeAttempts==writes);
 tick+=60000;flightRecorderLoop();assert(flightSequence==1&&flightStorageReady&&disk.count(FLIGHT_FILE));
 assert(flightRecorderReport(720).find("heartbeat")!=String::npos);
 removeFail=true;assert(!flightRecorderClear());assert(flightSequence==1&&disk.count(FLIGHT_FILE));
 removeFail=false;assert(flightRecorderClear());
 // Failed allocation retries once per heartbeat, not on every main-loop pass.
 writesFail=true;tick+=60000;flightRecorderLoop();writes=writeAttempts;
 tick+=100;flightRecorderLoop();assert(writeAttempts==writes);
 writesFail=false;tick+=60000;flightRecorderLoop();assert(flightStorageReady&&flightSequence==1);
 assert(healthLocks==0&&maxHealthLocks>=2);
 std::cout<<"PASS both production recorders: default off, retained downloads after restart, deletion, preserved files/settings, enabled recreation and allocation retry bounds\n";
}
'''
with tempfile.TemporaryDirectory() as tmp:
    p = Path(tmp)
    (p/'test.cpp').write_text(code, encoding='utf-8')
    subprocess.run(['g++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-I', str(root),
                    str(p/'test.cpp'), '-o', str(p/'test')], check=True)
    subprocess.run([str(p/'test')], check=True)
