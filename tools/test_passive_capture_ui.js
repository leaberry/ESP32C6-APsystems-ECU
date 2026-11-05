const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const assert = require('node:assert/strict');
const source = fs.readFileSync(path.join(__dirname, '../WEB_UI.ino'), 'utf8');
const script = source.match(/<script>\s*(let passiveBusy[\s\S]*?)<\/script>/)[1];
const packet = n => `${100+n},16,-40,200,050200070000\n`;
const header = (next, running, extra='') => `# aps-passive-v1 firmware=test session=41 channel=16 started_ms=100 ended_ms=${running?0:900100} elapsed_ms=${running?100:900000} packets=${next} next=${next} running=${running} pending=0 buffer_drops=0 queue_drops=0 reason=${running?'recording':'time limit'} ${extra}\n`;

async function scenario(batches) {
  const elements = Object.fromEntries(['passiveStart','passiveStop','passiveSave','passiveStatus'].map(id => [id,{textContent:'',disabled:false}]));
  const requests=[], blobs=[];
  let unload;
  const context = {
    document: {getElementById:id=>elements[id],createElement:()=>({click(){}})},
    window: {addEventListener:(name,fn)=>{assert.equal(name,'beforeunload');unload=fn;}},
    setInterval:()=>0,
    setTimeout:(fn,ms)=>{if(ms<8000)queueMicrotask(fn);return 1;},clearTimeout:()=>{},
    AbortController,Blob,
    URL:{createObjectURL:blob=>{blobs.push(blob);return 'blob:test';},revokeObjectURL:()=>{}},
    confirm:()=>true,
    fetch:async (url,options)=>{
      assert.equal(options.cache,'no-store');
      if(url.endsWith('/status'))return {ok:true,status:200,text:async()=>'idle'};
      if(options.method==='POST'){
        assert.equal(options.headers['X-ECU-Diagnostics'],'1');
        return {ok:true,status:202,text:async()=>options.body==='start=1'?'41':'Stop requested.'};
      }
      requests.push(url);
      assert.ok(batches.length,'Unexpected extra request');
      const reply=batches.shift();
      if(reply instanceof Error)throw reply;
      return {ok:reply.status===undefined||reply.status===200,status:reply.status||200,text:async()=>reply.text};
    }
  };
  vm.runInNewContext(script,context);
  await context.passiveStatus();
  await elements.passiveStart.onclick();
  return {context,elements,requests,blobs,unload};
}
(async()=>{
  const result=await scenario([
    new Error('Temporary disconnect'),
    {text:header(1,1)+packet(1).trimEnd()}, // Truncated response must not advance.
    {text:header(1,1)+packet(1)},
    {text:header(2,0)+packet(2)}
  ]);
  assert.deepEqual(result.requests.map(url=>new URL('http://ecu'+url).searchParams.get('after')),['0','0','0','1']);
  assert.match(result.elements.passiveStatus.textContent,/900 seconds, 2 packets/);
  assert.equal(result.elements.passiveStart.disabled,false);
  assert.equal(result.elements.passiveStop.disabled,true);
  result.elements.passiveSave.onclick();
  const csv=await result.blobs[0].text();
  assert.equal(csv.split(packet(1)).length-1,1);
  assert.equal(csv.split(packet(2)).length-1,1);
  assert.match(csv,/ended_ms=900100/);
  assert.match(csv,/# browser_download packets=2 complete=1/);
  let prevented=false;result.unload({preventDefault(){prevented=true;}});assert.equal(prevented,false);
  const partial=await scenario([{text:header(1,1)+packet(1)},{status:409,text:'Expired session'}]);
  partial.unload({preventDefault(){prevented=true;}});assert.equal(prevented,true);
  partial.elements.passiveSave.onclick();
  assert.match(await partial.blobs[0].text(),/complete=0/);
  assert.match(partial.elements.passiveStatus.textContent,/partial file/);
  const failed=await scenario([{text:header(0,0,'reason=radio setup failed')}]);
  failed.elements.passiveSave.onclick();
  assert.match(await failed.blobs[0].text(),/packets=0/);
  console.log('PASS capture UI: interrupted/truncated transfers, exact cursor retries, complete and partial downloads, navigation guard and empty capture');
})().catch(error=>{console.error(error);process.exit(1);});
