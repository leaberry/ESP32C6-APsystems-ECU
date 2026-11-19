const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const source = fs.readFileSync(path.join(__dirname,'../GRAPH_UI.h'),'utf8').split('R"graphjs(')[1].split(')graphjs"')[0];

function fixture() {
  const elements = {}, timers = new Map(), calls = [], windowEvents = {};
  let timerId = 0;
  class Element {
    constructor() { this.events = {}; this.value = ''; this.options = []; this.disabled = false; this.hidden = false; this.textContent = ''; }
    addEventListener(type, fn, options) { this.events[type] = fn; this.eventOptions ??= {}; this.eventOptions[type] = options; }
    replaceChildren(...options) { this.options = options; this.value = options[0].value; }
    querySelector(selector) { return selector === 'svg' ? (this.innerHTML?.includes('<svg') ? svg : null) : cursor; }
    setAttribute(key,value) { this[key] = value; }
    focus() { control.focused = this; }
    getBoundingClientRect() { return {left:-100,width:660}; }
  }
  const svg = new Element(), cursor = new Element();
  const root = {set innerHTML(value) {
    this.markup = value;
    for (const [,id] of value.matchAll(/id="ph-([^"]+)"/g)) elements[id] = new Element();
    elements.range.value = '3'; elements.source.value = '-1'; elements.enabled.hidden = true;
  },querySelector(selector) { return elements[selector.slice(4)]; }};
  const state = {enabled:true,pending:false,problem:'',bytes:1024,ram_bytes:128,poll_seconds:60,utc:20000*86400+120,
    inverters:['Inverter 1','Inverter 2'],days:[{day:20000,step:60}]};
  const control = {state, statusError:0, postError:0, dayError:false, empty:false, network:false, hang:false};
  const context = {module:{exports:{}}, AbortController,
    Option:class {constructor(text,value) {this.text=text;this.value=value;}},
    window:{addEventListener:(event,fn)=>windowEvents[event]=fn},
    setTimeout:(fn,ms)=>{timers.set(++timerId,{fn,ms});return timerId;}, clearTimeout:id=>timers.delete(id),
    fetch:async(url,options)=>{
      calls.push({url,options});
      if(control.network) throw new Error('Network unavailable');
      if(control.hang) return new Promise((resolve,reject)=>options.signal.addEventListener('abort',()=>reject(new Error('Aborted'))));
      if(url==='/api/graph/settings') return {ok:!control.postError,status:control.postError||202};
      if(url==='/api/graph') return {ok:!control.statusError,status:control.statusError||200,json:async()=>JSON.parse(JSON.stringify(state))};
      return {ok:!control.dayError,status:control.dayError?404:200,json:async()=>({day:20000,step:60,values:control.empty?[null,null,null]:[0,100,null]})};
    }};
  vm.runInNewContext(source,context);
  return {control,elements,svg,cursor,root,calls,timers,windowEvents,
    mount:()=>context.module.exports.mount(root),
    fire:async(id,event='click',arg={})=>elements[id].events[event](arg),
    tick:async(ms)=>{const entry=[...timers].find(([,t])=>t.ms===ms);assert(entry,`missing ${ms}ms timer`);timers.delete(entry[0]);return entry[1].fn();}};
}

(async()=>{
  const f=fixture(); await f.mount();
  assert.equal(f.elements.toggle.disabled,false);
  assert(f.root.markup.includes('touch-action:auto'));
  assert(f.root.markup.indexOf('id="ph-refresh"')<f.root.markup.indexOf('id="ph-enabled"'),'refresh must remain visible while history is off');
  f.svg.events.pointerdown({clientX:-100+(64+(3*86400-120)/(3*86400)*916)/1000*660});
  assert(f.elements.tip.textContent.includes('0 W'),'tap must inspect real zero readings');
  assert.equal(f.svg.eventOptions.pointerdown.passive,true);
  assert.equal(f.svg.eventOptions.pointermove.passive,true);
  f.svg.events.pointermove({clientX:546.8});
  assert(f.elements.tip.textContent.includes('No reading'));
  await f.fire('toggle');
  assert.equal(f.elements.confirm.hidden,false);
  assert.equal(f.control.focused,f.elements.cancel);
  await f.fire('cancel');
  assert.equal(f.elements.confirm.hidden,true);
  assert.equal(f.control.focused,f.elements.toggle);
  assert.equal(f.calls.filter(c=>c.url.endsWith('/settings')).length,0,'cancel must not POST');
  await f.fire('toggle'); await f.fire('confirm','keydown',{key:'Escape'});
  assert.equal(f.elements.confirm.hidden,true);
  assert.equal(f.calls.filter(c=>c.url.endsWith('/settings')).length,0,'Escape must not POST');
  await f.fire('toggle'); await f.fire('clear');
  assert.equal(f.calls.filter(c=>c.url.endsWith('/settings')).length,1);
  assert.equal(f.calls.at(-1).options.body,'enabled=0');
  assert.equal(f.elements.toggle.disabled,true);
  await f.fire('toggle');
  assert.equal(f.calls.filter(c=>c.url.endsWith('/settings')).length,1,'pending must block duplicate writes');
  f.control.state.pending=true; await f.tick(1500);
  assert.equal(f.elements.toggle.disabled,true);
  f.control.state.pending=false; f.control.state.enabled=false; await f.tick(1500);
  assert.equal(f.elements.enabled.hidden,true);
  assert.equal(f.elements.toggle.textContent,'Enable detailed history');
  await f.fire('toggle');
  assert.equal(f.elements.confirm.hidden,true,'enabling does not need the destructive confirmation');
  assert.equal(f.calls.at(-1).options.body,'enabled=1');
  f.control.state.enabled=true; await f.tick(1500);
  assert.equal(f.elements.enabled.hidden,false);

  for(const code of [401,500]) {
    const g=fixture(); await g.mount(); g.control.postError=code; await g.fire('toggle'); await g.fire('clear');
    assert(g.elements.message.textContent.includes(code===401?'Administrator':'Could not save'));
    assert.equal(g.elements.enabled.hidden,false,'failed settings request must not imply disabled history');
    assert.equal(g.elements.toggle.disabled,true);
    assert.equal(g.elements.refresh.disabled,false);
    g.control.postError=0; await g.fire('refresh'); assert.equal(g.elements.toggle.disabled,false);
  }
  const h=fixture(); h.control.statusError=503; await h.mount();
  assert(h.elements.message.textContent.includes('503'));
  assert.equal(h.elements.toggle.disabled,true,'unknown initial state must not permit a setting write');
  assert.equal(h.elements.refresh.disabled,false);
  await h.fire('toggle'); assert.equal(h.calls.filter(c=>c.url.endsWith('/settings')).length,0);
  h.control.statusError=0; await h.fire('refresh'); assert.equal(h.elements.toggle.disabled,false);
  h.control.dayError=true; await h.fire('refresh'); assert(h.elements.message.textContent.includes('1 history day(s) unavailable'));
  h.control.dayError=false; h.control.empty=true; await h.fire('refresh'); assert(h.elements.plot.innerHTML.includes('No readings in this range'));
  h.control.empty=false; h.control.state.problem='History write failed; previous file retained.';
  await h.fire('refresh'); assert.equal(h.elements.message.textContent,h.control.state.problem);
  h.control.network=true; await h.fire('refresh'); assert(h.elements.message.textContent.includes('Network unavailable'));
  h.control.network=false; await h.fire('refresh'); assert.equal(h.elements.toggle.disabled,false);

  for(const duringPost of [false,true]) {
    const g=fixture(); await g.mount(); g.control.hang=true;
    if(duringPost) await g.fire('toggle');
    const pending=duringPost?g.fire('clear'):g.fire('refresh');
    const inFlight=g.calls.length;
    await g.fire('toggle'); await g.fire('refresh');
    assert.equal(g.calls.length,inFlight,'busy requests must not permit overlapping actions');
    await g.tick(10000); await pending;
    assert(g.elements.message.textContent.includes('timed out'));
    assert.equal(g.elements.toggle.disabled,true);
    assert.equal(g.elements.refresh.disabled,false);
    g.control.hang=false; await g.fire('refresh'); assert.equal(g.elements.toggle.disabled,false);
  }
  const g=fixture(); await g.mount(); g.windowEvents.pagehide();
  const before=g.calls.length; await g.fire('refresh'); assert.equal(g.calls.length,before);
  console.log('Graph controls: taps, cancel/confirm, queued settings, failures, timeouts and recovery passed.');
})().catch(error=>{console.error(error);process.exitCode=1;});
