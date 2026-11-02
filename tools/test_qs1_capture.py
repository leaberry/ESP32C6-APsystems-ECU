"""Exercise production QS1 capture retention, download leases and probe isolation."""
import ast
from pathlib import Path
import subprocess
import tempfile
import struct
import sys
sys.dont_write_bytecode = True
from passive_capture_to_pcap import convert

root = Path(__file__).resolve().parents[1]
# Reuse the small HTTP/radio host fakes from the passive capture regression.
tree = ast.parse((root / 'tools/test_passive_capture.py').read_text(encoding='utf-8'))
preamble = next(n.value.value for n in tree.body if isinstance(n, ast.Assign)
                and any(isinstance(t, ast.Name) and t.id == 'code' for t in n.targets))
code = preamble.split('#include "PASSIVE_CAPTURE.ino"')[0]
code += r'''
#include "QS1_DIAGNOSTICS.ino"
int main(){
 uint8_t raw[]={5,2,0,7,0,0};
 AsyncWebServerRequest request;qs1CaptureDownload(&request);assert(request.status==409);
 assert(qs1CaptureBegin("806000000001",0xA3D8));
 assert(!qs1CaptureBegin("806000000002",0x1234));
 qs1CaptureStage("handshake",0xA3D8);
 assert(!qs1CaptureObserve(raw,sizeof(raw),99,16,-40,10));assert(qs1Capture->rx==0);
 assert(!qs1CaptureObserve(raw,sizeof(raw),101,16,-40,10));
 qs1CaptureTx(raw+1,3,100,true,0);
 qs1CaptureDownload(&request);assert(request.status==409);
 clockMs=110;qs1CaptureSetProbe(true);qs1CaptureStage("observe",0xA3D8);
 assert(qs1CaptureObserve(raw,sizeof(raw),111,16,-40,10));
 clockMs=120;qs1CaptureSetProbe(false);
 assert(qs1CaptureObserve(raw,sizeof(raw),115,16,-40,10));
 clockMs=121;assert(!qs1CaptureObserve(raw,sizeof(raw),121,16,-40,10));
 dropped=3;qs1CaptureEnd(false,true);
 qs1CaptureDownload(&request);assert(request.status==200&&qs1Readers==1);
 assert(!qs1CaptureBegin("806000000002",0x1234));
 String csv;uint8_t buffer[7];size_t n;
 while((n=request.response->fill(buffer,sizeof(buffer),csv.size())))csv.append((char*)buffer,n);
 assert(csv.find("target=806000000001")!=String::npos&&csv.find("queue_drops=3")!=String::npos);
 assert(csv.find("100,T,handshake,A3D8,0,16,0,0,0,050200070000")!=String::npos);
 assert(csv.find("115,R,observe,A3D8,1,16,-40,10,0,050200070000")!=String::npos);
 request.response.reset();assert(!qs1Readers);
 // Disconnect before finishing a download releases the same lease.
 qs1CaptureDownload(&request);request.response.reset();assert(!qs1Readers);
 clockMs=0xFFFFFFF0;assert(qs1CaptureBegin("806000000002",0x1234));
 qs1CaptureSetProbe(true);clockMs=10;qs1CaptureSetProbe(false);
 assert(qs1CaptureObserve(raw,sizeof(raw),5,16,-40,10));
 assert(!qs1CaptureObserve(raw,sizeof(raw),11,16,-40,10));
 clockMs=60011;assert(!qs1CaptureObserve(raw,sizeof(raw),5,16,-40,10));
 qs1CaptureEnd(false,true);
 Qs1Capture ring;
 for(uint32_t i=0;i<300;++i){Qs1Packet p={};p.at=i;p.direction='R';ring.add(p);}
 assert(ring.count()==256&&ring.rx==300&&ring.total-ring.count()==44);
 for(size_t i=0;i<64;++i)assert(ring.at(i).at==i);
 for(size_t i=64;i<256;++i)assert(ring.at(i).at==i+44);
 std::cout<<"PASS QS1 capture: raw RX/TX, first/latest retention, stale/wrap handling, probe isolation, leases and chunked/disconnected downloads\n";
}
'''
with tempfile.TemporaryDirectory() as directory:
    p = Path(directory)
    (p/'test.cpp').write_text(code, encoding='utf-8')
    subprocess.run(['g++','-std=c++17','-Wall','-Wextra','-Werror','-fsanitize=undefined','-I',str(root),str(p/'test.cpp'),'-o',str(p/'test')], check=True)
    subprocess.run([str(p/'test')], check=True)
    csv = p/'capture.csv'
    csv.write_text('# qs1-active-v1 started=100\nuptime_ms,direction,raw_hex\n110,R,050200020000\n100,T,050200010000\n101,S,\n')
    assert convert(csv,p/'capture.pcap') == 2
    packet = (p/'capture.pcap').read_bytes()
    assert packet[40:43] == bytes.fromhex('020001')
    assert struct.unpack_from('<IIII',packet,24) == (0,100000,3,3)
    print('PASS QS1 PCAP conversion: stage exclusion and TX/RX timestamp sorting')
