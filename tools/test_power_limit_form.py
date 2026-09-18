"""Run the production throttle form branch and confirmation renderer on the host."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
form = (root / 'handeforms.ino').read_text(encoding='utf-8')
form = 'bool handleForms(AsyncWebServerRequest *request) {\n' + form.split('// the request is something like', 1)[1].split('\n', 1)[1]
server = (root / 'ASYSERVER.ino').read_text(encoding='utf-8')
confirm = 'void confirm()' + server.split('void confirm()', 1)[1].split('double round2', 1)[0]
harness = r'''
#include <cassert>
#include "WEB_NAVIGATION.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <iostream>
#define F(x) x
struct String:std::string {
 using std::string::string;
 String(std::string s):std::string(s){} String(int n):std::string(std::to_string(n)){}
 int toInt()const{return atoi(c_str());}
 bool startsWith(const char *s)const{return rfind(s,0)==0;}
 int indexOf(const char *s)const{auto n=find(s);return n==npos?-1:(int)n;}
};
struct {template<class T>void print(T){} template<class T>void println(T){}}Serial;
struct AsyncWebParameter{String key,text;String name()const{return key;}String value()const{return text;}};
struct AsyncWebServerRequest{
 std::vector<AsyncWebParameter> values;int status=0;
 bool hasParam(const char *key){for(auto &p:values)if(p.key==key)return true;return false;}
 const AsyncWebParameter *getParam(int i){return &values.at(i);}
 const AsyncWebParameter *getParam(const char *key){for(auto &p:values)if(p.key==key)return &p;abort();}
 int params(){return values.size();}String arg(const char *key){return getParam(key)->value();}
 void send(int code,const char*,const char*){status=code;}
};
int inverterCount=2,desiredThrottle[9]={},actionFlag=0;
char requestUrl[100]={};String toSend;
size_t strlcpy(char *d,const char *s,size_t n){snprintf(d,n,"%s",s);return strlen(s);}
''' + form + confirm + r'''
int main(){
 for(int slot:{0,1})for(int watts:{20,100,237,500}){
  AsyncWebServerRequest r{{{"INV",String(slot)},{"pMax",String(watts)}}};
  assert(handleForms(&r)&&r.status==0);
  assert(actionFlag==240+slot&&desiredThrottle[slot]==watts);
  assert(String(requestUrl)=="/inverter-details?inv="+String(slot));
  confirm();assert(toSend.find("location.href='/inverter-details?inv="+String(slot)+"'")!=std::string::npos);
 }
 for(auto values:{std::vector<AsyncWebParameter>{{"pMax","100"}},
      {{"INV","-1"},{"pMax","100"}},{{"INV","2"},{"pMax","100"}},
      {{"INV","0"},{"pMax","19"}},{{"INV","0"},{"pMax","501"}}}){
  AsyncWebServerRequest r{values};actionFlag=0;desiredThrottle[0]=123;
  assert(!handleForms(&r)&&r.status==400&&actionFlag==0&&desiredThrottle[0]==123);
 }
 std::cout<<"Throttle form queues correct slot/value and returns to registered details page; invalid inputs rejected\n";
}
'''
assert 'server.on("/inverter-details", HTTP_GET' in server
with tempfile.TemporaryDirectory() as tmp:
    p = Path(tmp)
    (p / 'test.cpp').write_text(harness, encoding='utf-8')
    subprocess.run(['g++', '-std=c++17', '-I'+str(root), str(p / 'test.cpp'), '-o', str(p / 'test')], check=True)
    subprocess.run([str(p / 'test')], check=True)
