"""Replay issue #25 pairing formats (serial anonymized), with no radio traffic."""
from pathlib import Path
import subprocess, tempfile
root=Path(__file__).resolve().parents[1]
code=r'''
#include <cassert>
#include <vector>
#include <string>
#include <cstdio>
#include "PAIRING_PROTOCOL.h"
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
 puts("PASS strict DS3-H response parsing");
}
'''
with tempfile.TemporaryDirectory() as tmp:
 p=Path(tmp);(p/'test.cpp').write_text(code)
 subprocess.run(['g++','-std=c++11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I',str(root),str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
