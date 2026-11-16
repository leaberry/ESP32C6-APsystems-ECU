"""Exercise the real settings handler and identity storage across a restart."""
from pathlib import Path
import os
import subprocess
import tempfile
root=Path(__file__).resolve().parents[1]
scope={'__file__':str(root/'tools/test_ecu_identity.py')}
prefix=(root/'tools/test_ecu_identity.py').read_text().split("harness += r'''\nvoid reset()")[0]
exec(compile(prefix,'<identity-fakes>','exec'),scope)
code=scope['harness'].replace('const char *fleetName="Test fleet", *userPwd="test-password";',
                            'const char *fleetName="Test fleet"; char userPwd[33]="test-password";')
code+=r'''
#include <cctype>
struct String:std::string {
 using std::string::string;String(const std::string&s):std::string(s){}
 bool isEmpty()const{return empty();}int toInt()const{return atoi(c_str());}
 void toUpperCase(){for(char &c:*this)c=toupper(c);}
 bool startsWith(const char*s)const{return rfind(s,0)==0;}
};
struct Param {String text;String value(){return text;}};
struct AsyncWebServerRequest {
 std::map<std::string,Param> params;int status=0;String body;
 bool hasParam(const char*s,bool){return params.count(s);}
 Param*getParam(const char*s,bool){return &params.at(s);}
 void send(int s,const char*,String text){status=s;body=text;}
 void redirect(const char*){status=302;}
};
char pswd[33]="admin-password";int actionFlag=0;bool pairingActive=false;
bool pairReceiveActive(){return pairingActive;}
int pollingClampSeconds(int n){return n;}void flightRecorderSetEnabled(bool value){flightRecorderEnabled=value;}
void wifiConfigsave(){}
enum class AsyncAuthType {AUTH_BASIC};
struct {void closeAll(){}void setAuthentication(const char*,const char*,AsyncAuthType){}} ws;
String ecuPageStart(const char*,const char*){return "";}String ecuPageEnd(){return "";}
'''
source=(root/'CONFIG_BASIS.ino').read_text()
code+=source[:source.index('void zendPageBasis(')]
code+=source[source.index('void handleBasisSave('):]
code+=r'''
void reset(){
 files.clear();failOpen.clear();shortWrite=false;renameCalls=failRenameAt=0;
 ecuIdentityPending=ecuIdentityExplicit=false;actionFlag=0;pairingActive=false;
 strcpy(ECU_ID,"123456789ABC");basisConfigsave();
}
AsyncWebServerRequest request(const char *id){
 AsyncWebServerRequest r;r.params={{"ecuid",{id}},{"pollsec",{"300"}},{"polling",{"on"}}};return r;
}
int main(){
 reset();auto same=request(ECU_ID);handleBasisSave(&same);assert(same.status==302&&!actionFlag&&!ecuIdentityPending);
 reset();auto change=request("d8a3011b9780");change.params["pollsec"]={"120"};handleBasisSave(&change);
 assert(change.status==200&&actionFlag==10&&ecuIdentityPending);
 assert(!strcmp(ECU_ID,"123456789ABC")); // In-flight commands still use the live PAN's identity.
 JsonDocument saved;assert(!deserializeJson(saved,files[ECU_CONFIG_PATH]));
 assert(saved["ECU_ID"]=="D8A3011B9780" && saved["pollIntervalSeconds"]==120);basisConfigsave();
 assert(!deserializeJson(saved,files[ECU_CONFIG_PATH])&&saved["ECU_ID"]=="D8A3011B9780");
 auto repeat=request("ABCDEF123456");handleBasisSave(&repeat);assert(repeat.status==409);
 ecuIdentityPending=false;strcpy(ECU_ID,saved["ECU_ID"].as<const char*>());ecuIdentityBegin();
 assert(!strcmp(ECU_ID,"D8A3011B9780")&&ecuIdentityExplicitChoice());
 for(int busy=0;busy<2;++busy){reset();pairingActive=busy==0;actionFlag=busy?60:0;auto r=request("ABCDEF123456");handleBasisSave(&r);assert(r.status==409&&!ecuIdentityPending&&!strcmp(ECU_ID,"123456789ABC"));}
 for(const char *id:{"000011223344","FFFF11223344","123"}){reset();auto r=request(id);handleBasisSave(&r);assert(r.status==400&&!actionFlag);}
 reset();auto before=files[ECU_CONFIG_PATH];auto oldInterval=pollIntervalSeconds;
 failOpen=ECU_CONFIG_TEMP;auto r=request("ABCDEF123456");r.params["pollsec"]={"60"};
 r.params["currentadminpw"]={"admin-password"};r.params["adminpw"]={"changed-password"};r.params["adminpwconfirm"]={"changed-password"};
 handleBasisSave(&r);
 assert(r.status==500&&!actionFlag&&!ecuIdentityPending&&!strcmp(ECU_ID,"123456789ABC"));
 assert(files[ECU_CONFIG_PATH]==before);
 assert(pollIntervalSeconds==oldInterval && !strcmp(pswd,"admin-password"));
 std::cout<<"PASS ECU ID settings: atomic deferred change, live identity retained, restart, explicit default, busy/invalid requests and save failure\n";
}
'''
with tempfile.TemporaryDirectory() as directory:
    p=Path(directory);(p/'test.cpp').write_text(code)
    subprocess.run(['g++','-std=c++11','-Wall','-Wextra','-Werror','-I',str(root),'-I',str(scope['include']),str(p/'test.cpp'),'-o',str(p/'test')],check=True)
    subprocess.run([str(p/'test')],check=True)
