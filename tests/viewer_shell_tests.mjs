// Pure CadShell tests; no DOM, browser or product runtime dependencies.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import vm from 'node:vm';
let checks = 0;
function test(name, action) { action(); checks++; console.log('PASS ' + name); }
const environment = { console };
vm.runInNewContext(fs.readFileSync(new URL('../web/shell.js', import.meta.url), 'utf8'), environment);
const Shell = environment.CadShell;
// Values built inside the vm context carry a foreign prototype; compare plain data.
const plain = value => JSON.parse(JSON.stringify(value));

test('theme: a host theme wins, otherwise the OS preference, otherwise light', () => {
  assert.equal(Shell.resolveTheme({ theme: 'dark' }, false), 'dark');
  assert.equal(Shell.resolveTheme({ theme: 'light' }, true), 'light');
  assert.equal(Shell.resolveTheme({}, true), 'dark');
  assert.equal(Shell.resolveTheme({ theme: 'sepia' }, false), 'light');
  assert.equal(Shell.resolveTheme(undefined, false), 'light');
  assert.equal(Shell.resolveTheme(null, true), 'dark');
});

test('layout breakpoints follow the spec table', () => {
  assert.deepEqual(plain(Shell.layoutMode(1280)), { scene: 'open', dock: 'icons', compact: false });
  assert.deepEqual(plain(Shell.layoutMode(900)), { scene: 'open', dock: 'icons', compact: false });
  assert.deepEqual(plain(Shell.layoutMode(899)), { scene: 'chip', dock: 'icons', compact: false });
  assert.deepEqual(plain(Shell.layoutMode(560)), { scene: 'chip', dock: 'icons', compact: false });
  assert.deepEqual(plain(Shell.layoutMode(559)), { scene: 'chip', dock: 'menu', compact: true });
  assert.deepEqual(plain(Shell.layoutMode(360)), { scene: 'chip', dock: 'menu', compact: true });
  assert.deepEqual(plain(Shell.layoutMode(NaN)), { scene: 'chip', dock: 'menu', compact: true });
});

console.log(`viewer shell: ${checks} checks passed`);
