// Run with Node.js: exercise the actual embedded page scripts with a small DOM.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const root = path.resolve(__dirname, '..');

async function main() {
  const html = fs.readFileSync(path.join(root, 'DETAILSPAGE.h'), 'utf8');
  const script = html.match(/<script>([\s\S]*?)<\/script>/)[1];
  assert.match(html, /id="output-limit"[^>]*hidden/);
  for (const type of [0, 1, 2, 3]) {
    const elements = new Map();
    for (const m of html.matchAll(/id="([^"]+)"/g)) elements.set(m[1], {hidden: true});
    const phaseElements = [elements.get('acv1'), elements.get('acv2')];
    const n = {type, inv: 0, serial: '901000010817', polled: true, sid: '1234',
      acv: 234, acv0: 234, acv1: 235.6, acv2: 236.7,
      panel_count: [1, 3].includes(type) ? 4 : 2,
      throttle_supported: type !== 3, pow: [100, 200, 300, 400],
      dcv: [43, 43, 39, 39], dcc: [2, 3, 4, 5], en: [1, 2, 3, 4]};
    const context = vm.createContext({URLSearchParams, location: {search: '?inv=0'},
      document: {getElementById: id => {
        assert(elements.has(id), `missing DOM element ${id}`);
        return elements.get(id);
      }, querySelectorAll: () => phaseElements},
      fetch: async () => ({json: async () => n}), setInterval: () => {}});
    vm.runInContext(script, context);
    await vm.runInContext('load()', context);
    assert.equal(elements.get('state').textContent, 'Online');
    assert.equal(elements.get('model').textContent, ['YC600', 'QS1', 'DS3', 'QT2'][type]);
    assert.equal(elements.get('output-limit').hidden, type === 3);
    assert.equal(elements.get('qt2-note').hidden, type !== 3);
    assert.equal(elements.get('acv1').hidden, type !== 3);
    assert.equal(elements.get('acv-label').textContent, type === 3 ? 'L1 voltage' : 'Voltage');
    assert.equal((elements.get('panels').innerHTML.match(/<article/g) || []).length, n.panel_count);
  }
  const config = fs.readFileSync(path.join(root, 'AAA_INVERTERS_UI.h'), 'utf8');
  const configScript = config.match(/const char INV_SCRIPT\[\][\s\S]*?R"=====\(([\s\S]*?)\)====="/)[1];
  const span = {style: {display: ''}}, select = {value: '3'};
  const context = vm.createContext({document: {getElementById: id => id === 'sel' ? select : span}});
  vm.runInContext(configScript, context);
  for (const type of ['0', '1', '2', '3']) {
    select.value = type;
    vm.runInContext('myFunction()', context);
    assert.equal(span.style.display, ['1', '3'].includes(type) ? 'inline' : 'none');
  }
  console.log('PASS embedded details/configuration scripts for every inverter model');
}
main().catch(error => {console.error(error);process.exitCode = 1;});
