const fs=require('node:fs'),path=require('node:path'),vm=require('node:vm'),assert=require('node:assert/strict');
const source=fs.readFileSync(path.join(__dirname,'../SETTINGS_UI.h'),'utf8');
const script=source.split('<script>')[1].split('</script>')[0];
function setup(){
 const elements=Object.fromEntries(['settingsFile','validateSettings','applySettings','settingsPreview','settingsStatus','restoreNetwork','restoreAntenna'].map(id=>[id,{disabled:id==='applySettings',checked:false,textContent:'',files:[],handlers:{},addEventListener(event,fn){this.handlers[event]=fn}}]));
 const calls=[];let fail=false,confirm=true;
 const context=vm.createContext({document:{getElementById:id=>elements[id]},confirm:()=>confirm,fetch:async(url,options)=>{
  calls.push({url,options});return {ok:!fail,text:async()=>fail?'Storage error':'Restore staged',json:async()=>({fleet:'<b>Roof</b>',ecuId:'123456789ABC',inverters:2,replacement:true,radioIEEE:'0807060504030201'})};
 }});
 vm.runInContext(script,context);
 return {elements,calls,setFail:v=>fail=v,setConfirm:v=>confirm=v};
}
(async()=>{
 for(const network of [false,true])for(const antenna of [false,true]){
  const {elements:e,calls,setConfirm,setFail}=setup();
  assert(e.applySettings.disabled);await e.validateSettings.onclick();assert.equal(calls.length,0);
  e.settingsFile.files=[{size:10,text:async()=>'{"backup":1}'}];e.restoreNetwork.checked=network;e.restoreAntenna.checked=antenna;
  await e.validateSettings.onclick();assert(!e.applySettings.disabled);assert(e.settingsPreview.textContent.includes('<b>Roof</b>'));
  assert(!('innerHTML' in e.settingsPreview));
  setConfirm(false);await e.applySettings.onclick();assert.equal(calls.length,1);
  setConfirm(true);setFail(true);await e.applySettings.onclick();assert(!e.applySettings.disabled);assert.equal(e.settingsStatus.textContent,'Storage error');
  setFail(false);await e.applySettings.onclick();assert(e.applySettings.disabled);
  const request=calls.at(-1);assert.equal(request.url,'/settings/restore');
  assert.equal(request.options.headers['X-ECU-Network'],network?'restore':undefined);
  assert.equal(request.options.headers['X-ECU-Antenna'],antenna?'restore':undefined);
  assert.equal(request.options.headers['X-ECU-Restore'],'confirmed');
 }
 const {elements:e,calls}=setup();e.settingsFile.files=[{size:32769,text:async()=>''}];await e.validateSettings.onclick();assert.equal(calls.length,0);
 let release;e.settingsFile.files=[{size:10,text:()=>new Promise(resolve=>release=resolve)}];
 const pending=e.validateSettings.onclick();e.settingsFile.handlers.change();release('{}');await pending;
 assert(e.applySettings.disabled); // A stale validation response cannot authorize another file.
 assert.match(source,/passwords in plain text/);assert.match(source,/href="\/energy"/);
 console.log('PASS settings UI: preview, optional sections, confirmation, errors, size limit and stale selection');
})().catch(error=>{console.error(error);process.exit(1)});
