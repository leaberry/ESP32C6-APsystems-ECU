const fs=require('node:fs'),path=require('node:path'),vm=require('node:vm'),assert=require('node:assert/strict');
const script=fs.readFileSync(path.join(__dirname,'../MQTT_CONFIG_UI.h'),'utf8').split('<script>')[1].split('</script>')[0];
(async()=>{
for(const scenario of ['success','denied','busy','network','timeout']){
 let click,calls=[];const elements={};
 for(const id of ['haEnabled','legacyEnabled','haFields','legacyFields','brokerResult'])elements[id]={addEventListener(){}};
 for(const id of ['broker','port','user','password'])elements[id]={value:id==='password'?'':id};
 elements.testBroker={addEventListener:(_,f)=>click=f};
 const fetch=async(url,opts)=>{calls.push([url,opts]);if(scenario==='network')throw Error('Offline');if(scenario==='busy')return {ok:false,text:async()=> 'Another test is running'};
 return {ok:true,json:async()=>url.includes('?')?{running:scenario==='timeout',message:scenario==='success'?'Connection successful.':'Connection failed: broker denied authorization.'}:{id:7}};};
 vm.runInNewContext(script,{document:{getElementById:id=>elements[id]},URLSearchParams,fetch,setTimeout:fn=>fn()});
 await click();assert.equal(elements.testBroker.disabled,false);
 assert(calls[0][1].body.has('mqtPas'));assert.equal(calls[0][1].body.get('mqtPas'),'');
 assert.equal(calls[0][1].method,'POST');
 if(scenario==='success')assert.match(elements.brokerResult.textContent,/successful/);
 if(scenario==='denied')assert.match(elements.brokerResult.textContent,/denied/);
 if(scenario==='busy')assert.match(elements.brokerResult.textContent,/Another test/);
 if(scenario==='network')assert.match(elements.brokerResult.textContent,/Offline/);
 if(scenario==='timeout')assert.match(elements.brokerResult.textContent,/too long/);
}
console.log('Connection-test UI success, broker refusal, busy, network failure and timeout passed');
})();
