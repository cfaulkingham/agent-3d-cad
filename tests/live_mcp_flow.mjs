// Real native stdio server + actual app bridge/controller; no DOM/browser claim.
import assert from 'node:assert/strict';
import { spawn } from 'node:child_process';
import { mkdtempSync, readFileSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';
import { createInterface } from 'node:readline';
import vm from 'node:vm';
import { fileURLToPath } from 'node:url';
const root = fileURLToPath(new URL('../', import.meta.url));
for (const file of ['bridge.js', 'state.js', 'renderer.js']) vm.runInThisContext(readFileSync(`${root}/web/${file}`, 'utf8'), { filename: file });
const workspace = mkdtempSync(join(tmpdir(), 'cad-live-flow-'));
const exe = resolve(process.argv[2]);
const child = spawn(exe, ['serve', '--workspace', workspace], { stdio: ['pipe', 'pipe', 'pipe'] });
const waiting = new Map(); let next = 1, stderr = '', checks = 0;
const check = (value, message) => { assert.ok(value, message); checks++; };
child.stderr.on('data', value => stderr += value);
const lines = createInterface({ input: child.stdout });
lines.on('line', line => {
  try {
    const m = JSON.parse(line), waiter = waiting.get(m.id);
    if (waiter) { waiting.delete(m.id); clearTimeout(waiter.timer); m.error ? waiter.reject(Error(m.error.message)) : waiter.resolve(m.result); }
  } catch (error) { for (const waiter of waiting.values()) waiter.reject(error); }
});
child.on('exit', code => { for (const waiter of waiting.values()) { clearTimeout(waiter.timer); waiter.reject(Error(`Service exited ${code}: ${stderr}`)); } waiting.clear(); });
const rpc = (method, params = {}) => new Promise((resolve, reject) => {
  const id = next++, timer = setTimeout(() => { waiting.delete(id); reject(Error(`Timeout: ${method}: ${stderr}`)); }, 45000);
  waiting.set(id, { resolve, reject, timer }); child.stdin.write(JSON.stringify({ jsonrpc: '2.0', id, method, params }) + '\n');
});
const tool = async (name, args = {}) => CadBridge.value(await rpc('tools/call', { name, arguments: args }));
let listener; const messages = [];
const parent = { async postMessage(m) {
  if (!Object.hasOwn(m, 'id')) return;
  let response;
  try {
    if (m.method === 'ui/initialize') response = { protocolVersion: '2026-01-26', hostCapabilities: { message: {}, updateModelContext: {} }, hostContext: {} };
    else if (m.method === 'tools/call') response = await rpc(m.method, m.params);
    else if (m.method === 'ui/update-model-context' || m.method === 'ui/message') { messages.push(m); response = {}; }
    else throw Error('Unexpected app request ' + m.method);
    listener?.({ source: parent, origin: 'https://test-host.invalid', data: { jsonrpc: '2.0', id: m.id, result: response } });
  } catch (error) { listener?.({ source: parent, origin: 'https://test-host.invalid', data: { jsonrpc: '2.0', id: m.id, error: { code: -32603, message: error.message } } }); }
} };
const self = { parent, addEventListener(_, fn) { listener = fn; }, removeEventListener() { listener = null; } };
const bridge = new CadBridge(self), state = new CadLiveState(bridge);
async function untilReady(revision) {
  const deadline = Date.now() + 45000;
  while (Date.now() < deadline) {
    await state.pollOnce();
    if (state.value.status === 'ready' && state.value.payload.revision === revision) return;
    if (state.value.status === 'error') throw Error(state.value.error);
    await new Promise(resolve => setTimeout(resolve, 25));
  }
  throw Error(`Viewer did not reach revision ${revision}`);
}
try {
  await rpc('initialize', { protocolVersion: '2025-11-25', capabilities: { extensions: { 'io.modelcontextprotocol/ui': { mimeTypes: ['text/html;profile=mcp-app'] } } }, clientInfo: { name: 'native-live-flow', version: '1' } });
  child.stdin.write(JSON.stringify({ jsonrpc: '2.0', method: 'notifications/initialized' }) + '\n');
  const resources = await rpc('resources/read', { uri: 'ui://agent-3d-cad/viewer.html' });
  check(resources.contents[0].text.includes('CadLiveState'), 'live app served by native executable');
  const model = { schema_version: 1, units: 'mm', parameters: { height: 12 }, features: [{ id: 'base', type: 'box', size: [60, 40, { parameter: 'height' }] }], output: 'base' };
  await tool('cad_create', { document_id: 'plate', model });
  await bridge.initialize();
  const launch = await tool('cad_open', { document_id: 'plate', view_id: 'test_view' });
  state.attach(launch.view_id); await untilReady(1);
  check(state.value.model.output === 'base', 'feature tree arrives with first model');
  // Pure picking math uses the actual native tessellation; GPU rendering is separately tested.
  const evaluation = state.value.payload, prepared = CadRenderer.math.prepare(evaluation);
  const camera = { yaw: -.65, pitch: .6, zoom: 1.2, pan: [.08, -.04] };
  let picked = null;
  for (const edge of prepared.edges) {
    if (edge.points.length < 6) continue;
    const midpoint = [0, 1, 2].map(i => (edge.points[i] + edge.points[i + 3]) / 2);
    const p = CadRenderer.math.project(midpoint, camera, 800, 600);
    const hit = CadRenderer.math.pick(prepared, camera, 800, 600, p[0], p[1], 'edge');
    if (hit.id && !hit.ambiguous) { picked = hit.id; break; }
  }
  check(picked !== null, 'real native model yields an unambiguous visible edge');
  const reference = { document_id: 'plate', revision: 1, evaluation_id: evaluation.evaluation_id, feature_id: 'base', kind: 'edge', entity_id: picked };
  state.value.selection = { reference, geometry: evaluation.topology.edges.find(e => e.id === picked) };
  state.value.camera = camera;
  await state.publishContext();
  const context = await tool('cad_context', { view_id: 'test_view' });
  check(!context.stale && context.selection.entity_id === picked, 'agent reads actual selected edge');
  await state.sendPrompt('Round this edge to 1 mm.');
  const message = messages.find(m => m.method === 'ui/message');
  check(message.params.content[0].text.includes(evaluation.evaluation_id), 'Quick Edit carries exact evaluation');
  const resolved = await tool('cad_resolve_selection', reference);
  check(resolved.selector?.expected_count === 1, 'pick resolves to unique persistent design selector');
  await tool('cad_apply', { document_id: 'plate', expected_revision: 1, operations: [
    { op: 'add_feature', feature: { id: 'rounded', type: 'fillet', input: 'base', radius: 1, edges: resolved.selector } },
    { op: 'set_output', feature_id: 'rounded' }
  ] });
  check((await tool('cad_context', { view_id: 'test_view' })).stale, 'old context marked stale immediately after commit');
  await untilReady(2);
  check(state.value.selection === null, 'automatic reload clears old selection');
  assert.deepEqual(state.value.camera, camera); checks++;
  check(state.value.payload.summary.volume_mm3 < evaluation.summary.volume_mm3, 'selected fillet changes exact solid');
  const second = state.value.payload.evaluation_id;
  await assert.rejects(tool('cad_apply', { document_id: 'plate', expected_revision: 2, operations: [
    { op: 'replace_feature', id: 'rounded', feature: { id: 'rounded', type: 'fillet', input: 'base', radius: 10000, edges: resolved.selector } }
  ] })); checks++;
  await state.pollOnce();
  check(state.value.payload.evaluation_id === second, 'failed edit preserves visible saved revision');
  const record = await tool('cad_read', { document_id: 'plate' });
  check(record.revision === 2, 'failed edit preserves HEAD');
  console.log(`${checks} real MCP live-loop checks passed`);
} finally {
  state.dispose(); bridge.dispose(); child.stdin.end();
  if (child.exitCode === null) await new Promise(resolve => { child.once('exit', resolve); setTimeout(() => { if (child.exitCode === null) child.kill(); resolve(); }, 3000).unref(); });
  lines.close(); rmSync(workspace, { recursive: true, force: true });
}
