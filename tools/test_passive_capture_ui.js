const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const assert = require('node:assert/strict');
const source = fs.readFileSync(path.join(__dirname, '../WEB_UI.ino'), 'utf8');
const script = source.match(/<script>\s*(async function passiveStatus[\s\S]*?)<\/script>/)[1];

(async () => {
  const elements = {passiveStart: {}, passiveStop: {}, passiveStatus: {textContent: ''}};
  let fail = false, body;
  const context = {
    document: {getElementById: id => elements[id]},
    setInterval: (_, ms) => assert.equal(ms, 2000),
    fetch: async (url, options) => {
      if (fail) throw Error('Disconnected');
      if (url.endsWith('/status')) {
        assert.equal(options.cache, 'no-store');
        return {ok: true, text: async () => 'recording; packets=12/256'};
      }
      assert.equal(url, '/diagnostics/passive');
      assert.equal(options.method, 'POST');
      assert.equal(options.headers['X-ECU-Diagnostics'], '1');
      body = options.body;
      return {text: async () => 'Request queued'};
    }
  };
  vm.runInNewContext(script, context);
  await context.passiveStatus();
  assert.match(elements.passiveStatus.textContent, /recording; packets=12/);
  await elements.passiveStart.onclick(); assert.equal(body, 'start=1');
  await elements.passiveStop.onclick(); assert.equal(body, 'stop=1');
  fail = true;
  await context.passiveStatus(); assert.match(elements.passiveStatus.textContent, /Could not read/);
  await elements.passiveStart.onclick(); assert.match(elements.passiveStatus.textContent, /request failed/);
  console.log('PASS passive capture UI: status polling, start/stop POSTs and connection failures');
})().catch(error => {console.error(error); process.exit(1);});
