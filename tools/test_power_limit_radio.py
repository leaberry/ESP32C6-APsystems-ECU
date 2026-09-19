"""Exercise real power command construction, parsing and radio frame submission."""
from pathlib import Path
import subprocess
import tempfile
root = Path(__file__).resolve().parents[1]
def function(path, signature):
    text = (root / path).read_text(encoding='utf-8')
    start = text.index(signature)
    end = text.index('{', start) + 1
    depth = 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]
harness = r'''
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <iostream>
using std::min;
struct String:std::string {
 using std::string::string;
 String(std::string s):std::string(s){} String(int n):std::string(std::to_string(n)){}
 void toCharArray(char* p,size_t n){snprintf(p,n,"%s",c_str());}
};
struct {template<typename... A>void printf(A...){} void println(String){}} Serial;
void consoleOut(String){} void diagnosticsAppend(String){}
String ECU_REVERSE(){return "80971B01A3D8";}
int inverterCount=3,zigbeeUp=1,errorCode=0,desiredThrottle[3]={100,100,100};
struct Inv {char invID[5];char invSerial[13];int invType,calib;};
Inv Inv_Prop[3]={{"F25A","704000007719",2,0},{"E0BA","703000673792",2,0},{"15A3","409000043225",0,0}};
uint32_t peers[3]={0xA3D85AF2,0xA3D8BAE0,0xA3D8A315};
bool radioPreference(const char* serial,uint32_t* peer,bool){
 for(int i=0;i<3;++i)if(!strcmp(serial,Inv_Prop[i].invSerial)){*peer=peers[i];return *peer!=0;}return false;
}
bool encrypted=false,encryptOk=true,radioOk=true,panOk=true;
bool apsInverterUsesEncryption(int){return encrypted;} bool apsAllInvertersEncrypted(){return encrypted;}
bool apsEncryptOutgoing(int,const uint8_t* a,uint16_t n,uint8_t* out,size_t,size_t* len,bool){memcpy(out,a,n);*len=n;return encryptOk;}
uint16_t rawCurrentPan=0x9999;
uint8_t rawMacSequence=0,rawNwkSequence=0,rawApsCounter=0,rawExtendedAddress[8]={};
int apsExpectedWhich=-1,decoded=0,failAt=0;
bool rawRadioSetPan(uint16_t p){rawCurrentPan=p;return panOk;}
std::vector<std::vector<uint8_t>> frames;
bool radioTransmit(const uint8_t* b,size_t n,bool,const char*){frames.emplace_back(b,b+n);return radioOk && frames.size()!=static_cast<size_t>(failAt);}
bool waitSerial2Available(){return true;}void empty_serial2(){}
int decodeQueryAnswer(int){++decoded;return 0;}
'''
for path,sig in [('PAIRING_PROTOCOL.h','inline bool pairValidAddress('),
 ('ZIGBEE_A_TRANSPORT.ino','static uint8_t hexNibble('),
 ('ZIGBEE_A_TRANSPORT.ino','static uint8_t hexByte('),
 ('ZIGBEE_A_TRANSPORT.ino','static uint16_t hexLe16('),
 ('ZIGBEE_A_TRANSPORT.ino','static void putLe16(')]:
    harness += '\n'+function(path,sig)+'\n'
harness += r'''
int apsFindInverter(uint16_t dst,const uint8_t*){
 for(int i=0;i<3;++i)if(hexLe16(Inv_Prop[i].invID)==dst)return i;return -1;
}
'''
for path,sig in [('ZIGBEE_A_TRANSPORT.ino','static bool isPowerControl('),
 ('ZIGBEE_A_TRANSPORT.ino','static bool submitRawAps('),
 ('ZIGBEE_A_TRANSPORT.ino','bool sendZB('),('SETPOWER.ino','bool setMaxPower(')]:
    harness += '\n'+function(path,sig)+'\n'
harness += r'''
uint16_t le(const std::vector<uint8_t>& f,int p){return f[p]|(f[p+1]<<8);}
void rejected(){frames.clear();decoded=0;assert(!setMaxPower(0));assert(frames.empty()&&decoded==0);}
int main(){
 for(int model=0;model<3;++model)for(int target=0;target<3;++target){
  Inv_Prop[target].invType=model;frames.clear();assert(setMaxPower(target));assert(frames.size()==3);
  for(auto& f:frames){assert(f[0]==0x61&&le(f,3)==0xA3D8&&le(f,5)==(peers[target]&0xffff));
   assert(le(f,11)==(peers[target]&0xffff)&&f[25]==0x00);}
 }
 // A learned radio source can differ from the logical legacy ID.
 peers[0]=0xCE971234;frames.clear();assert(setMaxPower(0));assert(le(frames[0],3)==0xCE97&&le(frames[0],5)==0x1234);
 peers[0]=0;rejected();peers[0]=0xffff1234;rejected();peers[0]=0xA3D8ffff;rejected();
 peers[0]=peers[1];rejected();peers[0]=0xA3D85AF2;
 strcpy(Inv_Prop[1].invID,"F25A");rejected();strcpy(Inv_Prop[1].invID,"E0BA");
 strcpy(Inv_Prop[0].invID,"FFFF");rejected();strcpy(Inv_Prop[0].invID,"F25A");
 encrypted=true;encryptOk=false;rejected();encryptOk=true;frames.clear();assert(setMaxPower(0));assert(frames[0][25]==0);
 encrypted=false;panOk=false;rejected();panOk=true;
 radioOk=false;frames.clear();decoded=0;assert(!setMaxPower(0)&&frames.size()==1&&decoded==0);radioOk=true;
 for(int n=1;n<=3;++n){failAt=n;frames.clear();decoded=0;assert(!setMaxPower(0)&&frames.size()==static_cast<size_t>(n)&&decoded==0);}failAt=0;
 zigbeeUp=0;rejected();zigbeeUp=1;assert(!setMaxPower(-1)&&!setMaxPower(3));
 // Pairing and normal polling stay broadcast, including their APS delivery mode.
 for(auto command:{"24020FFFFFFFFFFFFFFFFF14FFFF140C0201000F0600704000007719",
                   "2401F25A1414060001000F0680971B01A3D8"}){
  std::string s=command;frames.clear();assert(sendZB(s.data()));assert(frames.size()==1);
  auto& f=frames[0];assert(f[0]==0x41&&le(f,5)==0xffff&&le(f,11)==0xffff&&f[25]==8);
 }
 std::cout<<"PASS directed power control: all models, same-PAN targets, readback, invalid peers, encryption and TX failures, unchanged polling/pairing\n";
}
'''
with tempfile.TemporaryDirectory() as tmp:
    p=Path(tmp)
    (p/'test.cpp').write_text(harness,encoding='utf-8')
    subprocess.run(['g++','-std=c++17',str(p/'test.cpp'),'-o',str(p/'test')],check=True)
    subprocess.run([str(p/'test')],check=True)
