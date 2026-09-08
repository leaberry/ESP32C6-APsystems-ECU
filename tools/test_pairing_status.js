// Run the real wait-page script with deterministic status responses. Node only.
const fs = require('node:fs');
const vm = require('node:vm');
const assert = require('node:assert/strict');
const path = require('node:path');
const html = fs.readFileSync(path.join(__dirname, '../HTML.h'), 'utf8');
const page = html.split('const char WAIT_PAIR[]')[1];
const script = page.split('<script>')[1].split('</script>')[0]
  .replace('check();setInterval(check,2500);', '');
(async () => {
  for (const [state, id, badge] of [
    ['pairing', '0869', 'Pairing'],
    ['failed', '0869', 'Failed'],
    ['failed', '0000', 'Failed'],
    ['success', 'F25A', 'Paired'],
  ]) {
    const elements = {badge: {textContent: 'Pairing'}, title: {}, message: {}};
    const context = vm.createContext({
      document: {getElementById: id => elements[id]},
      fetch: async () => ({json: async () => ({state, invID: id})}),
    });
    vm.runInContext(script, context);
    await context.check();
    assert.equal(elements.badge.textContent, badge);
    console.log(`PASS pairing status ${state}, retained ID ${id}`);
  }
})().catch(error => { console.error(error); process.exitCode = 1; });
