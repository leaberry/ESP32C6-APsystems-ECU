"""Test actual power-limit reply decoding for all supported inverter models."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[1]
source=(root/'ZIGBEE_QUERYING.ino').read_text(encoding='utf-8').split('int decodeQueryAnswer(int welke)',1)[1]
harness=r'''
#include <cstdint>
#include <cstdio>
#include <string>
#include <cstring>
#include <cstdlib>
#include <cassert>
#include <iostream>
#include <deque>
#include <cctype>
#include "fixtures/ds3_limit_readback.h"
#define F(x) x
#define CC2530_MAX_SERIAL_BUFFER_SIZE 1024
struct String:std::string{using std::string::string;String(int v):std::string(std::to_string(v)){}String(std::string s):std::string(s){}};
void consoleOut(String){}void delayMicroseconds(int){}
int readCounter=1,desiredThrottle[9];struct{int invType,calib;}Inv_Prop[9];
std::string reply;
std::deque<std::string> queued;
uint32_t clockMs=0;uint32_t millis(){return clockMs;}
char *readZB(char *s){clockMs+=500;if(!queued.empty()){reply=queued.front();queued.pop_front();}strcpy(s,reply.c_str());return s;}
char *split(char *s,const char *marker){char *p=strstr(s,marker);if(!p)return nullptr;*p=0;return p+strlen(marker);}
'''+ 'int decodeQueryAnswer(int welke)'+source+r'''
int main(){
 for(int type=0;type<3;++type)for(int watts:{20,100,300,500})for(int calibration:{-5,0,10}){
  Inv_Prop[0]={type,calibration};desiredThrottle[0]=watts;
  char hex[5];snprintf(hex,sizeof(hex),"%04X",(int)((watts+calibration)*(type==2?16.59:28.89)));
  reply=type==2?ds3LimitReply(watts,calibration):std::string("4481FBFB4DDE000000")+hex+"3B660000FEFE";
  assert(decodeQueryAnswer(0)==0);desiredThrottle[0]+=10;assert(decodeQueryAnswer(0)!=0);
 }
 for(const char *bad:{"4481","4481FBFB","not a reply"}){reply=bad;assert(decodeQueryAnswer(0)!=0);}
 // Actual production ordering: intermediate ACK, then the fragmented DS3 readback.
 Inv_Prop[0]={2,0};desiredThrottle[0]=20;
 queued={"4481FBFB06DE02000000000000FEFE",ds3LimitReply(20)};
 assert(decodeQueryAnswer(0)==0&&queued.empty());
 desiredThrottle[0]=500;
 queued={"4481FBFB06DE02000000000000FEFE",ds3LimitReply(500)};
 assert(decodeQueryAnswer(0)==0&&queued.empty());
 // ACK-only streams are bounded; an unrelated reply cannot imply success.
 reply="4481FBFB06DE02000000000000FEFE";auto before=clockMs;
 assert(decodeQueryAnswer(0)!=0&&clockMs-before<=4000);
 for(const char* bad:{"4481FBFB5CDDDE0104ZZZZ0013FEFE","4481FBFB5CDDDE01042067"}){
  reply=bad;assert(decodeQueryAnswer(0)!=0);
 }
 queued={"4481FBFB06DE02000000000000FEFE",ds3LimitReply(20)};
 assert(decodeQueryAnswer(0)!=0); // Real readback mismatch must still fail.
 // The previous fix rejected this complete, checksummed OpenAPS capture.
 desiredThrottle[0]=500;reply=ds3LimitCapture;
 assert(decodeQueryAnswer(0)==0);
 for(int variant:{0,4,0x7f}){
  reply=ds3LimitReply(500,0,variant);assert(decodeQueryAnswer(0)==0);
 }
 // Independently corrupt the frame boundaries, length, checksum and hex data.
 for(int kind=0;kind<7;++kind){
  reply=ds3LimitCapture;
  if(kind==0)reply[8]='5',reply[9]='B'; // declared length off by one
  if(kind==1)reply.resize(reply.size()-2);
  if(kind==2)reply[reply.size()-5]='0'; // checksum
  if(kind==3)reply[20]='Z';
  if(kind==4)reply += "00";
  if(kind==5)reply[reply.size()-1]='0';
  if(kind==6)reply.erase(20,1); // odd hex count
  assert(decodeQueryAnswer(0)!=0);
 }
 reply="4481FBFB05DDDE01002001E1FEFE"; // valid checksum, no full power field
 assert(decodeQueryAnswer(0)!=0);
 reply=ds3LimitCapture;reply.replace(30,4,"FEFE");ds3LimitChecksum(reply);
 assert(decodeQueryAnswer(0)==0); // Embedded FEFE is data, not the frame boundary.
 auto other=ds3LimitCapture;other.replace(12,2,"DD");ds3LimitChecksum(other);
 queued={other,ds3LimitCapture};assert(decodeQueryAnswer(0)==0&&queued.empty());
 readCounter=0;assert(decodeQueryAnswer(0)==50);
 std::cout<<"PASS power-limit replies: real DS3 0100 capture, metadata variants, checksum/length/page guards, all models and calibration\n";
}
'''
with tempfile.TemporaryDirectory() as tmp:
 p=Path(tmp);(p/'test.cpp').write_text(harness,encoding='utf-8')
 subprocess.run(['g++','-std=c++17','-I',str(root/'tools'),str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
