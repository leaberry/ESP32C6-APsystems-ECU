const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const assert = require('node:assert/strict');
const source = fs.readFileSync(path.join(__dirname, '../ANTENNA_UI.h'), 'utf8');
const script = source.split('<script>')[1].split('</script>')[0];
const elements = Object.fromEntries(['mode','board','useEnable','boardFields','advanced',
  'boardPhoto','presetHelp','enableFields','enablePin','enableHigh'].map(id => [id, {style:{}}]));
const context = vm.createContext({document:{getElementById:id => elements[id]}});
for (const mode of ['0','1','2']) for (const board of ['0','1']) for (const enabled of [false,true]) {
  elements.mode.value = mode; elements.board.value = board; elements.useEnable.checked = enabled;
  vm.runInContext(script,context);
  const managed = mode !== '0', advanced = managed && board === '1';
  assert.equal(elements.board.disabled,!managed);
  assert.equal(elements.boardFields.style.display, managed?'flex':'none');
  assert.equal(elements.advanced.disabled,!advanced);
  assert.equal(elements.advanced.style.display,advanced?'block':'none');
  assert.equal(elements.enablePin.disabled,!(advanced&&enabled));
  assert.equal(elements.enableHigh.disabled,!(advanced&&enabled));
  assert.equal(elements.boardPhoto.style.display,advanced?'none':'block');
}
assert.match(source,/https:\/\/files\.seeedstudio\.com\/[^" ]+\.png/);
console.log('PASS antenna form visibility and disabled fields for all 12 selection combinations');
