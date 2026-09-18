const fs=require('node:fs'),vm=require('node:vm'),assert=require('node:assert/strict');
const path=require('node:path');
const source=fs.readFileSync(path.join(__dirname,'..','DETAILSPAGE.h'),'utf8');
const script=source.split('<script>')[1].split('</script>')[0];
assert.match(source, /action="\/inverter\/throttle"/);
assert.match(source, /name="pMax"[^>]*min="20" max="500" step="1" required/);
(async()=>{
  const elements={};
  const get=id=>elements[id]??=( {value:'',textContent:'',className:'',addEventListener(event,callback){this[event]=callback}} );
  let refresh,fail=true;
  const data={inv:1,pwMax:100,pow:[50,50],dcv:[30,30],dcc:[2,2],en:[10,10]};
  vm.runInNewContext(script,{
    URLSearchParams,location:{search:'?inv=1'},document:{getElementById:get},
    fetch:async url=>{assert.equal(url,'/api/data?Inverter=1');if(fail)throw Error('offline');return {json:async()=>data}},
    setInterval:f=>{refresh=f},
  });
  await new Promise(setImmediate);
  assert.equal(get('limit').value,'');
  fail=false;await refresh();
  assert.equal(get('inv').value,1);assert.equal(get('limit').value,100);
  data.pwMax=500;await refresh();assert.equal(get('limit').value,500);
  get('limit').value='237';get('limit').input();await refresh();
  assert.equal(get('limit').value,'237','telemetry refresh must preserve an edit');
  get('limit').value='';await refresh();assert.equal(get('limit').value,'');
  console.log('Power-limit form: target inverter, first successful load, arbitrary whole watts and edit preservation passed');
})().catch(error=>{console.error(error);process.exitCode=1});
