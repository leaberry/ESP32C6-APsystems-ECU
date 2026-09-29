"""Exercise the actual clear-logs HTTP handler before any file can be removed."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root/'ASYSERVER.ino').read_text(encoding='utf-8')
start = source.index('server.on("/diagnostics/clear-recorded-logs", HTTP_POST, ')
handler = source[start:source.index('\n});', start)+4].split('HTTP_POST, ', 1)[1][:-2]
trace_start = source.index('server.on("/diagnostics/pairing-trace", HTTP_GET, ')
trace_handler = source[trace_start:source.index('\n});', trace_start)+4].split('HTTP_GET, ', 1)[1][:-2]
code = r'''
#include <cassert>
#include <string>
#include <iostream>
using String=std::string;
const char* pswd="fake";
bool denied=false,clearOk=true;
int clears=0;
bool checkRemote(String){return denied;}
bool traceOk=true;bool pairingTraceClear(){return traceOk;}
bool flightRecorderClear(){++clears;return clearOk;}
struct Ip {String toString(){return "test";}};
struct Client {Ip remoteIP(){return {};}};
struct Header {String text="1";String value(){return text;}};
struct AsyncWebServerRequest {
 bool auth=true,header=true;int status=0;String message;Client c;Header h;
 Client* client(){return &c;}
 bool authenticate(const char*,const char*){return auth;}
 void requestAuthentication(){status=401;}
 void redirect(const char* p){status=302;message=p;}
 bool hasHeader(const char*){return header;}
 Header* getHeader(const char*){return &h;}
 void send(int code,const char*,const char* text){status=code;message=text;}
};
int downloads=0; void pairingTraceDownload(AsyncWebServerRequest* r){++downloads;r->send(200,"text/plain","trace");}
auto traceAction = ''' + trace_handler + r''';
auto clearAction = ''' + handler + r''';
int main(){
 AsyncWebServerRequest r;
 denied=true;traceAction(&r);assert(r.status==302&&!downloads);denied=false;
 r.auth=false;traceAction(&r);assert(r.status==401&&!downloads);r.auth=true;
 traceAction(&r);assert(r.status==200&&downloads==1);
 denied=true;clearAction(&r);assert(r.status==302&&clears==0);denied=false;
 r.auth=false;clearAction(&r);assert(r.status==401&&clears==0);r.auth=true;
 r.header=false;clearAction(&r);assert(r.status==400&&clears==0);r.header=true;
 r.h.text="0";clearAction(&r);assert(r.status==400&&clears==0);r.h.text="1";
 traceOk=false;clearAction(&r);assert(r.status==500&&clears==1);traceOk=true;clears=0;
 clearOk=false;clearAction(&r);assert(r.status==500&&clears==1);
 clearOk=true;clearAction(&r);assert(r.status==200&&clears==2);
 std::cout<<"PASS clear-recorded-logs route: remote restrictions, administrator authentication, action header, deletion failure and success\n";
}
'''
with tempfile.TemporaryDirectory() as tmp:
    p = Path(tmp)
    (p/'test.cpp').write_text(code, encoding='utf-8')
    subprocess.run(['g++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                    str(p/'test.cpp'), '-o', str(p/'test')], check=True)
    subprocess.run([str(p/'test')], check=True)
