const fs=require('node:fs'),vm=require('node:vm'),assert=require('node:assert/strict');
const path=require('node:path');
const read=name=>fs.readFileSync(path.join(__dirname,'..',name),'utf8');
const source=read('MQTT_CONFIG_UI.h'),script=source.split('<script>')[1].split('</script>')[0];
for(const ha of [false,true]) for(const legacy of [false,true]) {
 const elements={haEnabled:{checked:ha},legacyEnabled:{checked:legacy},haFields:{},legacyFields:{}};
 for(const id of ['haEnabled','legacyEnabled'])elements[id].addEventListener=(_,f)=>elements[id].change=f;
 vm.runInNewContext(script,{document:{getElementById:id=>elements[id]}});
 for(const [toggle,fields]of [['haEnabled','haFields'],['legacyEnabled','legacyFields']]){
  assert.equal(elements[fields].disabled,!elements[toggle].checked);
  assert.equal(elements[fields].hidden,!elements[toggle].checked);
  elements[toggle].checked=!elements[toggle].checked;elements[toggle].change();
  assert.equal(elements[fields].disabled,!elements[toggle].checked);
 }
}
const ota=read('OTA.h');assert.match(ota,/id="saveToday" type="checkbox" checked/);
for(const checked of [true,false])for(const code of [200,400,500]){
 let submit,xhr;const button={disabled:false};
 const elements={upload:{addEventListener:(_,f)=>submit=f},image:{files:['firmware']},saveToday:{checked},reboot:{style:{}},status:{},progress:{}};
 class XHR {constructor(){xhr=this;this.upload={}}open(method,url){this.url=url}send(){}}
 vm.runInNewContext(ota.split('<script>')[1].split('</script>')[0],{document:{getElementById:id=>elements[id]},XMLHttpRequest:XHR,FormData:class{append(){}}});
 submit({preventDefault(){},target:{querySelector:()=>button}});
 assert.equal(xhr.url,'/firmware/upload?saveToday='+(checked?'1':'0'));assert.equal(button.disabled,true);
 xhr.status=code;xhr.responseText='failure';xhr.onload();assert.equal(button.disabled,false);
 assert.equal(elements.reboot.style.display,code===200?'inline-flex':'none');
}
const menu=read('AAA_MENUPAGE.h');const titles=[...menu.matchAll(/<strong>([^<]+)<\/strong>/g)].map(x=>x[1]);
assert.equal(titles[0],'System information');assert.deepEqual(titles.slice(-4),['Settings backup','Energy history','Diagnostic snapshot','Firmware update']);
assert.equal(titles.filter(x=>x==='MQTT').length,1);assert(!titles.includes('Home Assistant'));
assert.match(menu,/href="\/basicconfig#ecuid">\{ecuid\}/);
console.log('MQTT switches, OTA checkbox/errors and menu checks passed');
