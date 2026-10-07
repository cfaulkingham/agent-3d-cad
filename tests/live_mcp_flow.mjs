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
async function untilReady(revision, controller = state) {
  const deadline = Date.now() + 45000;
  while (Date.now() < deadline) {
    await controller.pollOnce();
    if (controller.value.status === 'ready' && controller.value.payload.revision === revision) return;
    if (controller.value.status === 'error') throw Error(controller.value.error);
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
  // Visibility travels through the actual controller/bridge/native context path.
  const assembly = JSON.parse(readFileSync(`${root}/examples/assembly.create.json`, 'utf8'));
  await tool('cad_create', assembly);
  state.invalidate();
  await tool('cad_show', { view_id: 'test_view', document_id: assembly.document_id });
  await untilReady(1);
  const beforeVisibility = await tool('cad_read', { document_id: assembly.document_id });
  const assemblyEvaluation = state.value.payload.evaluation_id;
  await state.setHiddenParts(['cover']); await state.publishContext();
  check((await tool('cad_context', { view_id: 'test_view' })).hidden_part_ids.join() === 'cover', 'Hide persists through native context');
  check(state.value.payload.evaluation_id === assemblyEvaluation, 'Hide does not rebuild or change evaluation identity');
  const cover = state.value.payload.topology.faces.find(f => f.part_id === 'cover');
  const coverReference = { document_id: assembly.document_id, revision: 1, evaluation_id: assemblyEvaluation,
    feature_id: state.value.payload.feature_id, kind: 'face', entity_id: cover.id };
  await assert.rejects(tool('cad_viewer', { action: 'context', view_id: 'test_view', evaluation_id: assemblyEvaluation,
    selection: coverReference })); checks++;
  await state.isolatePart('spacer_a'); await state.publishContext();
  assert.deepEqual([...state.value.hidden_part_ids].sort(), ['base', 'cover', 'spacer_b']); checks++;
  check(CadLiveState.promptText(state.snapshot()).includes('"hidden_part_ids"'), 'Agent context includes visibility state');
  const reopened = new CadLiveState(bridge); reopened.attach('test_view');
  try {
    await untilReady(1, reopened);
    assert.deepEqual([...reopened.value.hidden_part_ids].sort(), ['base', 'cover', 'spacer_b']); checks++;
  } finally { reopened.dispose(); }
  await state.showAll(); await state.publishContext();
  state.update({ selection: { reference: coverReference, geometry: cover } });
  await state.setPartVisible('cover', false); await state.publishContext();
  check(state.value.selection === null, 'Hiding the selected part clears the selection');
  check((await tool('cad_context', { view_id: 'test_view' })).selection === null, 'Hidden selection is cleared in agent context');
  assert.deepEqual(await tool('cad_read', { document_id: assembly.document_id }), beforeVisibility); checks++;
  await state.isolatePart('spacer_a'); await state.publishContext();
  await tool('cad_apply', { document_id: assembly.document_id, expected_revision: 1,
    operations: [{ op: 'set_parameter', name: 'plate_width', value: 64 }] });
  await untilReady(2); await state.publishContext();
  assert.deepEqual([...state.value.hidden_part_ids].sort(), ['base', 'cover', 'spacer_b']); checks++;
  const reduced = structuredClone(assembly.model.features.find(f => f.type === 'assembly'));
  reduced.parts = reduced.parts.filter(p => p.id !== 'spacer_b');
  reduced.mates = reduced.mates.filter(m => m.parent !== 'spacer_b' && m.child !== 'spacer_b');
  await tool('cad_apply', { document_id: assembly.document_id, expected_revision: 2,
    operations: [{ op: 'replace_feature', id: reduced.id, feature: reduced }] });
  await untilReady(3); await state.publishContext();
  assert.deepEqual([...state.value.hidden_part_ids].sort(), ['base', 'cover']); checks++;
  await state.setHiddenParts(['base', 'cover', 'spacer_a']); await state.publishContext();
  check((await tool('cad_context', { view_id: 'test_view' })).hidden_part_ids.length === 3, 'All-hidden view remains valid and recoverable');
  await state.showAll(); await state.publishContext();
  check((await tool('cad_context', { view_id: 'test_view' })).hidden_part_ids.length === 0, 'Show all restores every part');
  state.invalidate(); await tool('cad_show', { view_id: 'test_view', document_id: 'plate' }); await untilReady(2);
  check(state.value.hidden_part_ids.length === 0, 'Retargeting resets assembly visibility');
  console.log(`${checks} real MCP live-loop checks passed`);
} finally {
  state.dispose(); bridge.dispose(); child.stdin.end();
  if (child.exitCode === null) await new Promise(resolve => { child.once('exit', resolve); setTimeout(() => { if (child.exitCode === null) child.kill(); resolve(); }, 3000).unref(); });
  lines.close(); rmSync(workspace, { recursive: true, force: true });
}
