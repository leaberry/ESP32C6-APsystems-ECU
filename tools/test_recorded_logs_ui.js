const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const assert = require('node:assert/strict');
const source = fs.readFileSync(path.join(__dirname, '../WEB_UI.ino'), 'utf8');
const script = source.match(/<script>\s*(document\.getElementById\('clearRecordedLogs'\)[\s\S]*?)<\/script>/)[1];

(async () => {
  for (const status of [200, 500, 401, 'network']) {
    let handler;
    const button = {disabled: false, addEventListener: (_, fn) => {handler = fn;}};
    const message = {textContent: ''};
    const context = {
      document: {getElementById: id => id === 'clearRecordedLogs' ? button : message},
      fetch: async (url, options) => {
        assert.equal(button.disabled, true);
        assert.equal(url, '/diagnostics/clear-recorded-logs');
        assert.equal(options.method, 'POST');
        assert.equal(options.headers['X-ECU-Diagnostics'], '1');
        if (status === 'network') throw Error('Connection lost');
        return {ok: status === 200, status, text: async () => status === 200 ? 'Logs cleared' : 'Deletion failed'};
      }
    };
    vm.runInNewContext(script, context);
    await handler.call(button);
    assert.equal(button.disabled, false);
    assert.match(message.textContent, status === 200 ? /Logs cleared/ : /Could not clear/);
    if (status === 401) assert.match(message.textContent, /sign in again/);
  }
  console.log('PASS clear-recorded-logs button: authenticated POST header, busy state, success, storage failure, auth failure and connection loss');
})().catch(error => {console.error(error); process.exit(1);});
