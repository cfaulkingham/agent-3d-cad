// Pure CadShell tests; no DOM, browser or product runtime dependencies.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import vm from 'node:vm';
let checks = 0;
function test(name, action) { action(); checks++; console.log('PASS ' + name); }
const environment = { console, TextEncoder, requestAnimationFrame: () => 0, cancelAnimationFrame: () => {} };
for (const file of ['renderer.js', 'shell.js']) vm.runInNewContext(fs.readFileSync(new URL('../web/' + file, import.meta.url), 'utf8'), environment);
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

const mul = (m, v) => { const out = [0, 0, 0]; for (let r = 0; r < 3; r++) out[r] = m[0 * 4 + r] * v[0] + m[1 * 4 + r] * v[1] + m[2 * 4 + r] * v[2]; return out; };
const rounded = v => v.map(n => Math.round(n * 1e9) / 1e9 + 0);
test('six cube faces with unit normals and in-plane axes', () => {
  const faces = plain(Shell.CUBE_FACES);
  assert.equal(faces.length, 6);
  assert.deepEqual(faces.map(f => f.name).sort(), ['back', 'bottom', 'front', 'left', 'right', 'top']);
  for (const f of faces) {
    assert.equal(Math.hypot(...f.normal), 1);
    assert.equal(f.right.reduce((s, v, i) => s + v * f.normal[i], 0), 0);
    assert.equal(f.up.reduce((s, v, i) => s + v * f.normal[i], 0), 0);
  }
});
test('clicking a face centre looks at that face; borders add edge and corner views', () => {
  const d = (...args) => plain(Shell.cubeDirection(...args));
  assert.deepEqual(d('top', .5, .5), [0, 0, 1]);
  assert.deepEqual(d('front', .5, .5), [0, -1, 0]);
  assert.deepEqual(d('front', .95, .5), [1, -1, 0]);
  assert.deepEqual(d('front', .5, .05), [0, -1, 1]);
  assert.deepEqual(d('front', .95, .05), [1, -1, 1]);
  assert.deepEqual(d('top', .05, .95), [-1, -1, 1]);
  assert.throws(() => Shell.cubeDirection('side', .5, .5));
});
test('the cube matrix puts each face toward the viewer in its standard view, upright', () => {
  const views = { front: [0, 0], back: [Math.PI, 0], right: [-Math.PI / 2, 0], left: [Math.PI / 2, 0], top: [0, Math.PI / 2], bottom: [0, -Math.PI / 2] };
  for (const f of plain(Shell.CUBE_FACES)) {
    const [yaw, pitch] = views[f.name], view = plain(Shell.cubeMatrix({ yaw, pitch, zoom: 1, pan: [0, 0] })), face = plain(Shell.cubeFaceMatrix(f.name, 40));
    assert.deepEqual(rounded(mul(view, mul(face, [0, 0, 1]))), [0, 0, 1], f.name + ' normal faces the viewer');
    assert.deepEqual(rounded(mul(view, mul(face, [1, 0, 0]))), [1, 0, 0], f.name + ' text runs left to right');
    assert.deepEqual(rounded(mul(view, mul(face, [0, 1, 0]))), [0, 1, 0], f.name + ' text is upright');
  }
});

console.log(`viewer shell: ${checks} checks passed`);
