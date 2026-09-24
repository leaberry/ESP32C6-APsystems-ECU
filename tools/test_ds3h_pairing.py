"""Replay issue #25 pairing formats (serial anonymized), with no radio traffic."""
from pathlib import Path
import subprocess, tempfile
root=Path(__file__).resolve().parents[1]
code=r''' 
#include <cassert>
#include <vector>
#include <string>
#include <cstdio>
#include "PAIRING_DIAGNOSTICS.h"
std::vector<uint8_t> hex(const char* text){
 std::vector<uint8_t> b;
 for(size_t i=0;i<strlen(text);i+=2){unsigned v;sscanf(text+i,"%2x",&v);b.push_back(v);}
 return b;
}
int main(){
 uint8_t target[6];assert(pairSerialBytes("725000000001",target));
 // Original 41-byte direct FF0E format; target serial anonymized only.
 auto b=hex("28618872FFFF0000B5D848000000B5D80F2100140101050F1400FF0E72500000000100B8200000B50B");
 auto announcement=hex("20418870FFFFFFFFB5D80910FCFFB5D80120010000005072AFA90861000011B40A");
 PairReply reply;
 assert(checkPairReply(b.data(),b.size(),target,reply)==PR_MATCH);
 assert(reply.pan==0xffff&&reply.source==0xd8b5&&reply.id==0xd8b5&&reply.payloadBytes==13&&reply.prefix==0xff0e);
 assert(checkPairReply(announcement.data(),announcement.size(),target,reply)==PR_ANNOUNCEMENT);
 for(size_t n=0;n<b.size();++n)assert(!decodePairReply(b.data(),n,target,reply));
 for(int pos:{1,6,8,10,12,14,18,19,20,22,24,26,27,28,33}){
  auto bad=b;bad[pos]^=0x80;assert(!decodePairReply(bad.data(),bad.size(),target,reply));
 }
 auto bad=b;bad.insert(bad.end()-2,0);++bad[0];assert(!decodePairReply(bad.data(),bad.size(),target,reply));
 PairReplySession session;assert(session.begin("725000000001",0xa3d8));
 session.observe(b.data(),b.size());session.verify();session.observe(b.data(),b.size());assert(!session.success());
 auto operating=b;operating[4]=0xd8;operating[5]=0xa3;
 session.observe(operating.data(),operating.size());assert(session.success());
 auto conflict=operating;conflict[8]=conflict[14]=0x34;conflict[9]=conflict[15]=0x12;
 session.observe(conflict.data(),conflict.size());assert(!session.success());
 PairDiagnosticCapture capture;
 capture.begin(false,100);capture.stage(0,0xffff);capture.observe(b.data(),b.size(),target,120,-75,10);
 assert(capture.phases[0].frames==0);
 capture.begin(true,100);capture.stage(1,0xffff);
 for(uint32_t i=0;i<100;++i)capture.observe(b.data(),b.size(),target,100+i,-75,10);
 auto p=capture.phases[1];assert(p.frames==100&&p.relevant==100&&p.matched==100);
 assert(p.samples[0].elapsed==0&&p.samples[1].elapsed==1&&p.samples[2].elapsed==98&&p.samples[3].elapsed==99);
 assert(p.samples[0].payloadBytes==13&&p.samples[0].status[1]==0xb8);
 capture.stage(5,0xa3d8);capture.observe(announcement.data(),announcement.size(),target,300,-77,9);
 assert(capture.phases[5].samples[0].identity==2&&capture.phases[5].rejected[PR_ANNOUNCEMENT]==1);
 assert(capture.phases[1].relevant==100);
 capture.active=false;capture.observe(b.data(),b.size(),target,500,-75,10);assert(capture.phases[5].frames==1);
 capture.begin(true,900);assert(capture.phases[1].frames==0);
 puts("PASS DS3-H capture: extended status, strict envelopes/identity, discovery vs verification, conflict, bounded per-phase diagnostics and disabled/reset behavior");
}
'''
with tempfile.TemporaryDirectory() as tmp:
 p=Path(tmp);(p/'test.cpp').write_text(code)
 subprocess.run(['g++','-std=c++11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I',str(root),str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
