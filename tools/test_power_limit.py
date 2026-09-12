"""Test actual power-limit reply decoding for all supported inverter models."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[1]
source=(root/'ZIGBEE_QUERYING.ino').read_text(encoding='utf-8').split('int decodeQueryAnswer(int welke)',1)[1]
harness=r'''
#include <string>
#include <cstring>
#include <cstdlib>
#include <cassert>
#include <iostream>
#define F(x) x
#define CC2530_MAX_SERIAL_BUFFER_SIZE 1024
struct String:std::string{using std::string::string;String(int v):std::string(std::to_string(v)){}String(std::string s):std::string(s){}};
void consoleOut(String){}void delayMicroseconds(int){}
int readCounter=1,desiredThrottle[9];struct{int invType,calib;}Inv_Prop[9];
std::string reply;
char *readZB(char *s){strcpy(s,reply.c_str());return s;}
char *split(char *s,const char *marker){char *p=strstr(s,marker);if(!p)return nullptr;*p=0;return p+strlen(marker);}
'''+ 'int decodeQueryAnswer(int welke)'+source+r'''
int main(){
 for(int type=0;type<3;++type)for(int watts:{20,100,300,500})for(int calibration:{-5,0,10}){
  Inv_Prop[0]={type,calibration};desiredThrottle[0]=watts;
  char hex[5];snprintf(hex,sizeof(hex),"%04X",(int)((watts+calibration)*(type==2?16.59:28.89)));
  reply=type==2?std::string("4481FBFB5CDDDE0104")+hex+"0013FEFE":std::string("4481FBFB4DDE000000")+hex+"3B660000FEFE";
  assert(decodeQueryAnswer(0)==0);desiredThrottle[0]+=10;assert(decodeQueryAnswer(0)!=0);
 }
 for(const char *bad:{"4481","4481FBFB","not a reply"}){reply=bad;assert(decodeQueryAnswer(0)!=0);}
 readCounter=0;assert(decodeQueryAnswer(0)==50);
 std::cout<<"PASS power-limit replies: YC600/QS1/DS3, calibration, mismatch, truncated and missing replies\n";
}
'''
with tempfile.TemporaryDirectory() as tmp:
 p=Path(tmp);(p/'test.cpp').write_text(harness,encoding='utf-8')
 subprocess.run(['g++','-std=c++17',str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
