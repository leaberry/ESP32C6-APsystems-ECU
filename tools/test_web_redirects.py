"""Audit redirect destinations against GET routes and exercise saved return URLs."""
from pathlib import Path
import re
import subprocess
import sys
import tempfile
from urllib.parse import urlsplit

root = Path(__file__).resolve().parents[1]
# Preserve C++ strings (including HTML/JS raw strings), excluding commented-out code.
tokens = re.compile(r'R"(?P<delimiter>[^()\\\s]{0,16})\([\s\S]*?\)(?P=delimiter)"|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|//[^\n]*|/\*[\s\S]*?\*/')
def uncomment(text):
    return tokens.sub(lambda m: '\n' * m[0].count('\n') if m[0].startswith(('//', '/*')) else m[0], text)

sources = {p.name: uncomment(p.read_text(encoding='utf-8'))
           for p in root.iterdir() if p.suffix in ('.ino', '.h')}
route_pattern = re.compile(r'server\.on\("([^"]+)"\s*,\s*(HTTP_\w+)')
application = ''.join(sources[name] for name in ('SETTINGS_BACKUP.ino', 'HA_CONFIG.ino', 'ASYSERVER.ino'))
app_routes = route_pattern.findall(application)
portal_routes = route_pattern.findall(sources['PORTAL_WIFI.ino'])
get_routes = {p for p, method in app_routes if method in ('HTTP_GET', 'HTTP_ANY')}
portal_get = {p for p, method in portal_routes if method in ('HTTP_GET', 'HTTP_ANY')}
checked = []
def check_target(file, target):
    parsed = urlsplit(target)
    routes = portal_get if file == 'PORTAL_WIFI.ino' else get_routes
    assert parsed.path in routes, f'{file}: redirect {target!r} has no GET route'
    if parsed.netloc:
        assert target in ('http://192.168.4.1/', 'http://{adres0}/'), (file, target)
    if parsed.fragment:
        assert parsed.path == '/mqtt' and parsed.fragment == 'home-assistant'
        assert 'id="home-assistant"' in sources['MQTT_CONFIG_UI.h']
    checked.append((file, target))

for file, source in sources.items():
    for target in re.findall(r'->redirect\(\s*"([^"]+)"', source):
        check_target(file, target)
    html = source.replace('\\"', '"').replace("\\'", "'")
    for target in re.findall(r'location\.href\s*=\s*[\'"]((?:/|http://)[^\'"]*)', html):
        check_target(file, target)
    for target in re.findall(r'\burl=(/[^\s\'"<>]*)', html):
        check_target(file, target)
    for target in re.findall(r'String\s+\w*[Rr]eturnUrl\s*=\s*"([^"]+)"', source):
        check_target(file, target)
    # Any future computed server redirect must receive an explicit audit here.
    for expression in re.findall(r'->redirect\(\s*([^\n;]+)', source):
        assert expression.startswith(('"', 'webReturnDestination(')), (file, expression)

for target in re.findall(r'"(/[^"\n]*)"', sources['WEB_NAVIGATION.h']):
    check_target('WEB_NAVIGATION.h', target)
form = sources['handeforms.ino']
check_target('handeforms.ino', re.search(r'String toReturn = "([^"]+)"', form)[1])
server = sources['ASYSERVER.ino']
data_handler = server.split('server.on("/api/data"', 1)[1].split('server.on(', 1)[0]
assert not re.search(r'strlcpy\(requestUrl', data_handler), 'telemetry must not overwrite return URL'
select_handler = server.split('server.on("/inverter/select"', 1)[1].split('server.on(', 1)[0]
assert '"/inverter/select?welke=" + selection' in select_handler
assert 'strlcpy(requestUrl, selectionReturnUrl.c_str()' in select_handler
assert server.count('webReturnDestination(requestUrl, inverterCount)') == 2
# Same-method parent paths can shadow child routes in ESPAsyncWebServer.
for routes in (app_routes, portal_routes):
    for i, (parent, method) in enumerate(routes):
        if parent == '/':
            continue
        for child, other_method in routes[i+1:]:
            assert not (child.startswith(parent + '/') and
                        (method == other_method or 'HTTP_ANY' in (method, other_method))), (parent, child)
assert len(checked) >= 50, 'audit unexpectedly lost redirect coverage'
print(f'Checked {len(checked)} redirect/return destinations against {len(get_routes)} application GET routes and {len(portal_get)} portal routes')

if '--static' not in sys.argv:
    harness = r'''
#include "WEB_NAVIGATION.h"
#include <cassert>
#include <string>
int main(){
 for(const char *url:{"/", "/menu", "/mqtt", "/journal", "/settings", "/inverter-details?inv=1", "/inverter/select?welke=1", "/inverter/select?welke=99"})
  assert(std::string(webReturnDestination(url,2))==url);
 for(const char *url:{"", "/details?inv=0", "/MENU", "/api/data", "/back", "/reboot", "/setup", "/inverter/throttle", "/inverter/select", "/inverter-details", "/inverter-details?inv=2", "/inverter-details?inv=-1", "/inverter-details?inv=0&x=1", "//example.com", "http://example.com/", "/';alert(1)//"})
  assert(std::string(webReturnDestination(url,2))=="/");
 assert(std::string(webReturnDestination(nullptr,2))=="/");
 assert(std::string(webReturnDestination("/inverter/select?welke=99",9))=="/");
 assert(std::string(webReturnDestination("/inverter-details?inv=0",0))=="/");
 assert(std::string(webReturnDestination("/inverter-details?inv=8",9))=="/inverter-details?inv=8");
}
'''
    with tempfile.TemporaryDirectory() as tmp:
        p = Path(tmp)
        (p / 'test.cpp').write_text(harness, encoding='utf-8')
        subprocess.run(['g++', '-std=c++17', '-I'+str(root), str(p / 'test.cpp'), '-o', str(p / 'test')], check=True)
        subprocess.run([str(p / 'test')], check=True)
    print('Saved return URLs: query parameters, stale indices, obsolete paths, action loops and invalid URLs passed')
