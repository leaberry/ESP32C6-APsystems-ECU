"""Replay issue #8 through the firmware matcher and pairing orchestration.

Uses the real RADIO_TRACE.ino, pairing(), and sendZB(), with deterministic
radio/time/storage substitutes. Requires python3 and g++; transmits nothing.
"""
from pathlib import Path
import json
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def function(filename, signature):
    text = (ROOT / filename).read_text()
    start = text.index(signature)
    opening = text.index("{", start)
    depth, end = 1, opening + 1
    while depth:
        depth += (text[end] == "{") - (text[end] == "}")
        end += 1
    return text[start:end]


fixture = json.loads((ROOT / "tools/fixtures/issue8-pairing.json").read_text())
harness = r'''
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>
#include "PAIRING_PROTOCOL.h"
#include "PAIRING_AUDIT.h"
using std::min;
struct String : std::string {
  using std::string::string;
  String(const std::string &s) : std::string(s) {}
  String(int n) : std::string(std::to_string(n)) {}
  void toCharArray(char *p, size_t n) const { snprintf(p,n,"%s",c_str()); }
};
#define F(x) x
#define portENTER_CRITICAL(x) (void)(x)
#define portEXIT_CRITICAL(x) (void)(x)
using portMUX_TYPE = int;
#define portMUX_INITIALIZER_UNLOCKED 0
using esp_err_t = int;
const int ESP_OK = 0;
std::vector<std::string> logs;
void diagnosticsAppend(String s) { logs.push_back(s); }
void consoleOut(String s) { logs.push_back(s); }
void pairAuditEvent(PairAuditStage, bool, uint8_t, uint16_t, uint16_t, uint16_t, uint16_t, int8_t, uint8_t, bool) {}
void pairAuditStep(PairAuditStage, bool, uint8_t, uint16_t) {}
size_t strlcpy(char *out, const char *in, size_t n) {
  snprintf(out,n,"%s",in); return strlen(in);
}
uint16_t currentPan = 0xA3D8, zbOperationalPan = 0xA3D8;
bool promiscuous = false, radioOk = true;
bool rawRadioSetPan(uint16_t pan) { currentPan=pan; return radioOk; }
bool rawRadioSetPromiscuous(bool value) { promiscuous=value; return radioOk; }
bool apsUsePairingPan(bool pair) { return rawRadioSetPan(pair ? 0xFFFF : zbOperationalPan); }
bool apsUseSpecificPan(uint16_t pan,const char *) { return rawRadioSetPan(pan); }
bool savedPeerKnown=false, replyOnSavedPan=false;
bool apsRadioLoadPeer(const char *,uint16_t *pan,uint16_t *source) {
  *pan=0x01CE; *source=0x5AF2; return savedPeerKnown;
}
struct Inverter { char invSerial[13]="704000007719"; char invID[5]="0869"; };
Inverter Inv_Prop[1];
int inverterCount=1, apsExpectedWhich=-1, saves=0, normalOps=0, queryCount=0;
bool saveOk=true, replyOnOperating=true, dropAll=false, injectConflict=false;
bool injectAnnouncement=false;
int failTx=-1, txCount=0;
uint16_t lastCluster=0;
uint16_t savedPan=0, savedSource=0;
bool saveVerifiedPairing(int which, const char *id, uint16_t pan, uint16_t source) {
  ++saves;
  if (!saveOk) return false;
  savedPan=pan; savedSource=source;
  strlcpy(Inv_Prop[which].invID,id,5); return true;
}
String ECU_REVERSE() { return "80971B01A3D8"; }
void empty_serial2() {}
void sendNO() { ++normalOps; }
void checkCoordinator() {}
void delay(unsigned);
bool sendZB(char[]);
std::vector<uint8_t> unhex(const std::string &s) {
  std::vector<uint8_t> b;
  for(size_t i=0;i<s.size();i+=2) b.push_back(std::stoul(s.substr(i,2),nullptr,16));
  return b;
}
int failures=0;
void check(bool ok, const char *name) {
  std::cout << (ok ? "PASS " : "FAIL ") << name << '\n'; failures += !ok;
}
bool submitRawAps(uint16_t dst, uint8_t dep, uint8_t sep, uint16_t cluster,
                  const uint8_t *payload, uint16_t len, uint8_t radius, uint8_t opts) {
  lastCluster=cluster;
  ++txCount;
  check(dst==0xFFFF && dep==20 && sep==20 && radius==15 && opts==0,
        "pair command addressing");
  auto expected = unhex(cluster==0x020D ? "704000007719FFFF10FFFF80971B01A3D8" :
                       cluster==0x010F ? "704000007719A3D810FFFF80971B01A3D8" :
                       cluster==0x0101 ? "80971B01A3D8" : "704000007719");
  check(std::vector<uint8_t>(payload,payload+len)==expected,"exact command ASDU");
  check(!promiscuous,"MAC acknowledgement remains enabled");
  if(currentPan==0xA3D8) ++queryCount;
  return txCount!=failTx;
}
'''
harness += (ROOT / "RADIO_TRACE.ino").read_text()
for signature in ("static uint8_t hexNibble(", "static uint8_t hexByte(",
                  "static uint16_t hexLe16(", "bool sendZB("):
    harness += "\n" + function("ZIGBEE_A_TRANSPORT.ino", signature)
harness += "\n" + function("ZIGBEE_PAIR.ino", "bool pairing(")
harness += '\nconst char *captured[] = {\n' + ',\n'.join(
    json.dumps(frame) for frame in fixture['frames']) + '\n};\n'
harness += r'''
void observe(std::vector<uint8_t> b) {
  radioTraceObserve(b.data(),b.size(),16,-47,10);
}
void delay(unsigned duration) {
  if(duration!=4700 || dropAll) return;
  // More relay traffic than fits in the diagnostic buffer BEFORE the reply.
  for(int n=0;n<80;++n) observe(unhex(captured[0]));
  if(lastCluster!=0x020C && lastCluster!=0x010F) return;
  auto b=unhex(lastCluster==0x020C ? captured[16] : captured[37]);
  if(currentPan!=0xFFFF) {
    if(currentPan==0xA3D8 ? !replyOnOperating : !replyOnSavedPan) return;
    b[4]=currentPan & 0xFF; b[5]=currentPan >> 8;
    if(injectAnnouncement) {
      b=unhex(captured[44]);
      auto serial=unhex("704000007719");
      std::copy(serial.begin(),serial.end(),b.begin()+20);
      b[26]=0x34;b[27]=0x12;
    }
  }
  observe(b);
  if(injectConflict && currentPan==0xA3D8) {
    b[8]=b[14]=0x33; b[9]=b[15]=0x22; observe(b);
  }
}
void reset() {
  pairReceiveStop();
  saves=normalOps=queryCount=txCount=0;failTx=-1;
  saveOk=replyOnOperating=radioOk=true;
  dropAll=injectConflict=injectAnnouncement=savedPeerKnown=replyOnSavedPan=false;
  savedPan=savedSource=0;
  strlcpy(Inv_Prop[0].invID,"0869",5);
}
int main() {
  uint8_t target[6]; pairSerialBytes("704000007719",target);
  int matched=0;
  for(const char *hex:captured) {
    PairReply r;auto b=unhex(hex);
    if(decodePairReply(b.data(),b.size(),target,r)) {
      ++matched;
      check(r.pan==0xFFFF && r.source==0x5AF2 && r.id==0x5AF2,
            "issue #8 direct reply uses source, not FF1A/FFFF or trailing metadata");
    }
  }
  check(matched==8,"exactly eight target replies; no relay/other inverter matches");
  auto valid=unhex(captured[16]);
  for(size_t size=0;size<valid.size();++size) {
    PairReply r;
    check(!decodePairReply(valid.data(),size,target,r),"reject truncated frame");
  }
  for(int pos : {0,1,2,6,10,12,14,18,19,20,22,24,28,33}) {
    auto b=valid;b[pos]^=0x80;PairReply r;
    check(!decodePairReply(b.data(),b.size(),target,r),"reject malformed/foreign/security header");
  }
  PairReplySession session;
  check(!session.begin("70400000771Z",0xA3D8),"reject invalid serial");
  session.begin("704000007719",0xA3D8);
  session.observe(valid.data(),valid.size());
  check(session.heard && !session.success(),"rendezvous contact is not pairing success");
  session.verify();session.observe(valid.data(),valid.size());
  check(!session.success(),"late FFFF reply cannot confirm operating PAN");
  auto b=valid;b[4]=0xD8;b[5]=0xA3;
  session.observe(b.data(),b.size());
  check(session.success(),"fresh operating-PAN response confirms identity");
  reset();
  check(pairing(0) && saves==1 && queryCount==3 && normalOps==1 &&
        savedPan==0xA3D8 && savedSource==0x5AF2 && !strcmp(Inv_Prop[0].invID,"F25A") &&
        radioTraceDropped>0 && !pairReceiveActive() && currentPan==0xA3D8,
        "complete four commands + verification + save despite trace overflow");
  check(std::any_of(logs.begin(),logs.end(),[](const std::string &line) {
          return line.find("MAC RX ")!=std::string::npos && line.find("data=")!=std::string::npos;
        }), "raw packet hex remains in transient diagnostics");
  check(std::any_of(logs.begin(),logs.end(),[](const std::string &line) {
          return line.find("24020FFFFFFFFFFFFFFFFF14FFFF140D02")!=std::string::npos;
        }), "raw transmitted pairing commands remain available");
  reset();injectAnnouncement=true;
  check(pairing(0) && !strcmp(Inv_Prop[0].invID,"3412"),
        "legacy announcement compatibility ID remains supported");
  reset();replyOnOperating=false;
  check(!pairing(0) && !saves && !normalOps && !strcmp(Inv_Prop[0].invID,"0869"),
        "rendezvous-only replies fail without changing previous pairing");
  reset();replyOnOperating=false;savedPeerKnown=replyOnSavedPan=true;
  check(pairing(0) && savedPan==0x01CE && currentPan==0xA3D8,
        "verify a previously saved network using fresh target evidence");
  reset();replyOnOperating=false;savedPeerKnown=true;
  check(!pairing(0) && !saves,"saved route alone cannot report success");
  reset();dropAll=true;
  check(!pairing(0) && !saves && currentPan==0xA3D8,"timeout restores radio");
  reset();injectConflict=true;
  check(!pairing(0) && !saves,"conflicting identity sources fail closed");
  reset();saveOk=false;
  check(!pairing(0) && saves==1 && !normalOps && !strcmp(Inv_Prop[0].invID,"0869"),
        "persistence failure is not success");
  for(int phase=1;phase<=7;++phase) {
    reset();failTx=phase;
    check(!pairing(0) && !saves && !normalOps && currentPan==0xA3D8 &&
          !pairReceiveActive(),"each handshake/verification TX failure aborts safely");
  }
  reset();radioOk=false;
  check(!pairing(0) && !saves && !pairReceiveActive(),"radio setup failure cleans up session");
  reset();check(!pairing(-1) && !pairing(1),"invalid index rejected");
  return failures ? 1 : 0;
}
'''
with tempfile.TemporaryDirectory(prefix="pairing-path-") as directory:
    cpp, binary = Path(directory)/"test.cpp", Path(directory)/"test"
    cpp.write_text(harness)
    subprocess.run([os.environ.get("CXX", "g++"), "-std=c++11", "-Wall", "-Wextra",
                    "-Werror", "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
                    "-I", str(ROOT), str(cpp), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)

# Exercise the actual two-store commit helper with SPIFFS's no-overwrite rename
# semantics. A POSIX-style fake would miss a failure on every already-saved unit.
storage = harness[:harness.index("bool saveVerifiedPairing(")] + r'''
#include <map>
std::map<std::string,std::vector<uint8_t>> files;
bool openOk=true, writeOk=true, nvsOk=true, hasPeer=true;
int renameCount=0, failRenameAt=-1;
uint16_t peerPan=0x1234, peerSource=0x5678;
struct File {
  std::string name;
  explicit operator bool() const { return !name.empty(); }
  size_t write(const uint8_t *p,size_t n) {
    if(!writeOk) return 0;
    files[name]=std::vector<uint8_t>(p,p+n);return n;
  }
  void flush() {} void close() {}
};
struct FakeSpiffs {
  File open(String name,const char *) { return File{openOk ? name : ""}; }
  bool exists(String name) { return files.count(name); }
  bool remove(String name) { return files.erase(name); }
  bool rename(String from,String to) {
    if(++renameCount==failRenameAt || !files.count(from) || files.count(to)) return false;
    files[to]=files[from];files.erase(from);return true;
  }
} SPIFFS;
bool apsRadioLoadPeer(const char *, uint16_t *pan,uint16_t *src) {
  *pan=peerPan;*src=peerSource;return hasPeer;
}
bool apsRadioRememberPeer(const char *,uint16_t pan,uint16_t src) {
  if(!nvsOk) return false;
  hasPeer=true;peerPan=pan;peerSource=src;return true;
}
bool apsRadioForgetPeer(const char *) { hasPeer=false;return true; }
'''
start = storage.index("bool apsRadioLoadPeer(")
end = storage.index("struct Inverter", start)
storage = storage[:start] + storage[end:]
storage += function("ZIGBEE_PAIR.ino", "bool saveVerifiedPairing(")
storage += r'''
int main() {
  int failures=0;
  for(int scenario=0;scenario<7;++scenario) {
    files.clear();files["/Inv_Prop0.str"]={1,2,3};
    strlcpy(Inv_Prop[0].invID,"0869",5);
    openOk=scenario!=1;writeOk=scenario!=2;nvsOk=scenario!=3;
    hasPeer=scenario!=6;peerPan=0x1234;peerSource=0x5678;
    renameCount=0;failRenameAt=scenario==4 ? 1 : scenario>=5 ? 2 : -1;
    bool ok=saveVerifiedPairing(0,"F25A",0xA3D8,0x5AF2);
    bool pass=scenario==0 ? ok && !strcmp(Inv_Prop[0].invID,"F25A") &&
        peerPan==0xA3D8 && peerSource==0x5AF2 && files.size()==1 :
        !ok && !strcmp(Inv_Prop[0].invID,"0869") &&
        files["/Inv_Prop0.str"]==std::vector<uint8_t>({1,2,3}) && files.size()==1 &&
        (scenario==6 ? !hasPeer : peerPan==0x1234 && peerSource==0x5678);
    std::cout << (pass ? "PASS " : "FAIL ") << "storage scenario " << scenario << '\n';
    failures+=!pass;
  }
  return failures ? 1 : 0;
}
'''
with tempfile.TemporaryDirectory(prefix="pairing-storage-") as directory:
    cpp, binary = Path(directory)/"test.cpp", Path(directory)/"test"
    cpp.write_text(storage)
    subprocess.run([os.environ.get("CXX", "g++"), "-std=c++11", "-Wall", "-Wextra",
                    "-Werror", "-I", str(ROOT), str(cpp), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
