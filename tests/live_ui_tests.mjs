// Tests the actual bundled bridge/controller without a browser or network shim.
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import vm from 'node:vm';
import { fileURLToPath } from 'node:url';
const root = fileURLToPath(new URL('../', import.meta.url));
for (const file of ['bridge.js', 'renderer.js', 'state.js']) vm.runInThisContext(readFileSync(`${root}/web/${file}`, 'utf8'), { filename: file });
let checks = 0;
const check = (condition, label) => { assert.ok(condition, label); checks++; };
const payload = (revision = 1) => ({ schema_version: 1, document_id: 'part', revision, evaluation_id: `eval_${revision}`, feature_id: 'base', draft: false, mesh: {}, topology: {}, summary: {} });
const ready = (revision = 1) => ({ state: 'ready', document_id: 'part', revision, evaluation_id: `eval_${revision}`, model: { features: [{ id: 'base', type: 'box' }], parameters: {} } });
function host() {
  let listener; const sent = [];
  const parent = { postMessage(message, origin) { sent.push({ message, origin }); } };
  const self = { parent, addEventListener(_, fn) { listener = fn; }, removeEventListener() { listener = null; } };
  return { self, parent, sent, emit(data, source = parent, origin = 'https://host.example') { listener?.({ source, origin, data: { jsonrpc: '2.0', ...data } }); } };
}
{
  const h = host(), bridge = new CadBridge(h.self), init = bridge.initialize();
  const id = h.sent[0].message.id;
  check(h.sent[0].message.method === 'ui/initialize', 'Apps initialization handshake');
  h.emit({ id, result: { protocolVersion: '2026-01-26' } }, {});
  check(bridge.pending.has(id), 'unrelated window cannot complete handshake');
  h.emit({ id, result: { protocolVersion: '2026-01-26', hostCapabilities: { message: {} } } });
  await init;
  check(h.sent.at(-1).message.method === 'ui/notifications/initialized', 'ready notification follows handshake');
  bridge.reportSize(800, 576); bridge.reportSize(800, 576); bridge.reportSize(NaN, 576);
  check(h.sent.filter(s => s.message.method === 'ui/notifications/size-changed').length === 1, 'host size notification deduplicated and finite');
  assert.deepEqual(h.sent.at(-1).message.params, { width: 800, height: 576 }); checks++;
  const call = bridge.tool('cad_list'); const request = h.sent.at(-1).message;
  check(h.sent.at(-1).origin === 'https://host.example', 'outbound origin pinned after handshake');
  h.emit({ id: request.id, result: { structuredContent: { documents: ['spoof'] } } }, h.parent, 'https://evil.example');
  check(bridge.pending.has(request.id), 'changed origin ignored');
  h.emit({ id: request.id, result: { structuredContent: { documents: [] }, isError: false } });
  assert.deepEqual(await call, { documents: [] }); checks++;
  const failure = bridge.tool('cad_context');
  h.emit({ id: h.sent.at(-1).message.id, result: { isError: true, structuredContent: { error: { code: 'stale_selection', message: 'Selection is stale' } } } });
  await assert.rejects(failure, error => error.code === 'stale_selection'); checks++;
  await assert.rejects(bridge.request('ui/message', {}, 5), /Check the chat/); checks++;
  check(h.sent.filter(s => s.message.method === 'ui/message').length === 1, 'uncertain delivery never retries');
  let early; bridge.on('ui/notifications/tool-result', result => early = result);
  h.emit({ method: 'ui/notifications/tool-result', params: { structuredContent: { view_id: 'main' } } });
  check(early.structuredContent.view_id === 'main', 'tool result notification delivered');
  let replay; bridge.on('ui/notifications/tool-result', result => replay = result);
  check(replay === early, 'early launch notification retained');
  const waiting = bridge.request('tools/call', {});
  h.emit({ id: 999, method: 'ui/resource-teardown', params: {} });
  await assert.rejects(waiting, /closed/); checks++;
  check(bridge.closed && bridge.pending.size === 0, 'teardown releases pending requests');
}
function mock() {
  const calls = [], messages = []; let revision = 1;
  const bridge = { capabilities: { message: { image: {} }, updateModelContext: {} },
    async tool(name, args) {
      calls.push({ name, args });
      if (name === 'cad_context') return { stale: false, document_id: 'part', evaluation_id: `eval_${revision}`, selection: null };
      if (args.action === 'sync') return ready(revision);
      if (args.action === 'mesh') { const data = JSON.stringify(payload(revision)); return { data, offset: 0, next_offset: null, total_bytes: data.length }; }
      if (args.action === 'context') return {};
      throw Error('Unexpected tool');
    }, async request(method, params) { messages.push({ method, params }); return {}; }
  };
  const state = new CadLiveState(bridge); state.attach('main');
  return { bridge, state, calls, messages, advance() { revision++; } };
}
{
  const m = mock(); await m.state.pollOnce();
  const tool = m.bridge.tool;
  m.bridge.tool = (name, args) => args.action === 'sync' ? Promise.resolve({ state: 'loading', document_id: 'other_part', revision: 1 }) : tool(name, args);
  await m.state.pollOnce();
  check(m.state.value.payload === null && m.state.value.model === null && m.state.value.camera === null, 'retarget never labels the previous model as the new one');
}
{
  const m = mock(), tool = m.bridge.tool;
  const camera = { yaw: 2, pitch: .4, zoom: 3, pan: [.2, -.1] };
  m.bridge.tool = (name, args) => name === 'cad_context' ? Promise.resolve({ stale: false, document_id: 'part', evaluation_id: 'eval_1', camera, selection: null }) : tool(name, args);
  await m.state.pollOnce(); await m.state.deliveryQueue;
  assert.deepEqual(m.state.value.camera, camera); checks++;
  assert.deepEqual(m.calls.find(c => c.args.action === 'context').args.camera, camera); checks++;
}
for (const headRevision of [1, 2]) {
  const m = mock(), tool = m.bridge.tool;
  if (headRevision === 2) m.advance();
  const camera = { yaw: .2, pitch: .4, zoom: 7, pan: [.3, -.2] };
  m.bridge.tool = (name, args) => name === 'cad_context' ? Promise.resolve({ stale: true, document_id: 'part', head_revision: headRevision,
    revision: 1, evaluation_id: 'eval_previous_build', camera,
    selection: { document_id: 'part', revision: 1, evaluation_id: 'eval_previous_build', feature_id: 'base', kind: 'face', entity_id: 'face-1' } }) : tool(name, args);
  await m.state.pollOnce(); await m.state.deliveryQueue;
  assert.deepEqual(m.state.value.camera, camera); checks++;
  check(m.state.value.selection === null, 'Reopening after build or revision change restores camera without reviving a stale pick');
  assert.deepEqual(m.calls.find(c => c.args.action === 'context').args.camera, camera); checks++;
  m.state.dispose();
}
{
  const m = mock(), tool = m.bridge.tool;
  m.bridge.tool = (name, args) => name === 'cad_context' ? Promise.resolve({ stale: true, document_id: 'different', head_revision: 1,
    camera: { yaw: 2, pitch: .4, zoom: 7, pan: [.3, -.2] } }) : tool(name, args);
  await m.state.pollOnce();
  check(m.state.value.camera === null && m.state.value.payload === null, 'A context retargeted during opening never supplies another document camera');
  m.state.dispose();
}
{
  const m = mock(); await m.state.pollOnce();
  check(m.state.value.status === 'ready' && m.state.value.payload.revision === 1, 'loads validated transfer');
  const selected = { reference: { document_id: 'part', revision: 1, evaluation_id: 'eval_1', feature_id: 'base', kind: 'edge', entity_id: 'edge-1' }, geometry: {} };
  m.state.value.selection = selected; m.state.value.camera = { yaw: 1, pitch: .5, zoom: 2, pan: [.1, .2] };
  const camera = structuredClone(m.state.value.camera);
  await m.state.sendPrompt('Round this edge to 1 mm', 'data:image/png;base64,YQ==');
  const sent = m.messages.find(m => m.method === 'ui/message');
  check(sent.params.role === 'user' && sent.params.content[0].text.includes('edge-1'), 'selection travels with user message');
  check(sent.params.content[1].type === 'image', 'explicit screenshot attachment');
  const context = m.calls.filter(c => c.args.action === 'context').at(-1);
  check(context.args.selection.revision === 1 && context.args.prompt.includes('Round'), 'same context persisted for agent');
  const staleSnapshot = m.state.snapshot('another edit');
  m.advance(); await m.state.pollOnce();
  check(m.state.value.payload.revision === 2 && m.state.value.selection === null, 'new revision clears selection');
  assert.deepEqual(m.state.value.camera, camera); checks++;
  await assert.rejects(m.state.saveContext(staleSnapshot), /changed/); checks++;
  const before = m.calls.filter(c => c.args.action === 'mesh').length;
  await m.state.pollOnce(); check(m.calls.filter(c => c.args.action === 'mesh').length === before, 'unchanged evaluation does not redownload');
  m.state.dispose(); await m.state.pollOnce(); check(m.state.closed, 'closed viewer stops polling');
}
{
  const m = mock(); let syncs = 0;
  const tool = m.bridge.tool;
  m.bridge.tool = async (name, args) => {
    if (args.action === 'sync' && ++syncs === 2) return { state: 'loading', document_id: 'part', revision: 2 };
    return tool(name, args);
  };
  await m.state.pollOnce();
  check(m.state.value.payload === null, 'revision advancing during transfer never exposes old picks');
}
{
  for (const bad of [
    { data: '{}', offset: 1, next_offset: null, total_bytes: 2 },
    { data: '{}', offset: 0, next_offset: null, total_bytes: 3 },
    { data: '{}', offset: 0, next_offset: 0, total_bytes: 4 },
    { data: '{}', offset: 0, next_offset: null, total_bytes: 65 * 1024 * 1024 },
    { data: 'é', offset: 0, next_offset: null, total_bytes: 1 }
  ]) {
    const m = mock(), tool = m.bridge.tool;
    m.bridge.tool = (name, args) => args.action === 'mesh' ? Promise.resolve(bad) : tool(name, args);
    await m.state.pollOnce(); check(m.state.value.status === 'error' && m.state.value.payload === null, 'malformed chunk rejected');
  }
}
{
  const m = mock(), tool = m.bridge.tool; let release;
  m.bridge.tool = (name, args) => args.action === 'mesh' ? new Promise(r => release = () => r({ data: JSON.stringify(payload()), offset: 0, next_offset: null, total_bytes: JSON.stringify(payload()).length })) : tool(name, args);
  const poll = m.state.pollOnce(); await new Promise(r => setTimeout(r, 0));
  m.state.attach('second'); release(); await poll;
  check(m.state.value.view_id === 'second' && m.state.value.payload === null, 'late result cannot replace a newly opened view');
}
{
  const m = mock(); await m.state.pollOnce();
  m.bridge.tool = async () => { throw Error('Selection no longer current'); };
  await assert.rejects(m.state.sendPrompt('Round this edge'), /no longer current/); checks++;
  check(!m.messages.some(m => m.method === 'ui/message'), 'invalid native context prevents sending edit');
}
{
  const m = mock(); m.bridge.request = async () => { throw Error('Context capability rejected'); };
  await m.state.pollOnce(); await m.state.deliveryQueue; await Promise.resolve();
  check(m.state.value.status === 'ready', 'host context failure does not invalidate valid geometry');
  check(m.state.value.context_error === 'Context capability rejected', 'host context error remains visible');
}
{
  const m = mock(); let release;
  m.bridge.request = () => new Promise(resolve => release = resolve);
  await m.state.pollOnce();
  check(!m.state.busy && m.state.value.status === 'ready', 'host context acknowledgment never blocks HEAD polling');
  m.advance(); await m.state.pollOnce();
  check(m.state.value.payload.revision === 2, 'new revision loads while host context acknowledgment is pending');
  m.state.dispose(); release?.({});
}
// Native lock contention is a normal race between polling and context writes.
// Exercise each transfer stage, then require a complete current evaluation.
for (const stage of ['sync', 'mesh', 'cad_context']) {
  const m = mock(), tool = m.bridge.tool; let conflicted = false;
  m.bridge.tool = async (name, args) => {
    if (!conflicted && (stage === 'cad_context' ? name === stage : args.action === stage)) {
      conflicted = true; throw Object.assign(Error('Document lock held'), { code: 'workspace_busy' });
    }
    return tool(name, args);
  };
  await m.state.pollOnce();
  check(m.state.value.status === 'loading' && m.state.value.error === null && m.state.value.payload === null, `${stage} lock contention waits without publishing old picks`);
  await m.state.pollOnce();
  check(m.state.value.status === 'ready' && m.state.value.payload.revision === 1, `${stage} recovers after the lock is released`);
  m.state.dispose();
}
for (const code of ['workspace_busy', 'queue_full', 'stale_selection']) {
  const m = mock(); await m.state.pollOnce(); await m.state.deliveryQueue;
  m.state.value.selection = { reference: { entity_id: 'edge-1' } };
  m.state.value.camera = { yaw: 1, pitch: .5, zoom: 2, pan: [.1, .2] };
  const camera = structuredClone(m.state.value.camera), first = m.state.value.payload, tool = m.bridge.tool;
  m.advance(); let conflicted = false;
  m.bridge.tool = async (name, args) => {
    if (!conflicted && args.action === (code === 'stale_selection' ? 'mesh' : 'sync')) {
      conflicted = true; throw Object.assign(Error('Temporary view conflict'), { code });
    }
    return tool(name, args);
  };
  await m.state.pollOnce();
  check(m.state.value.status === 'loading' && m.state.value.error === null && m.state.value.selection === null && m.state.value.payload === first, `${code} retains rendered geometry with old picks disabled`);
  await assert.rejects(m.state.sendPrompt('Round this edge'), /finish loading/); checks++;
  check(!m.messages.some(message => message.method === 'ui/message'), `${code} cannot send an edit during recovery`);
  await m.state.pollOnce();
  check(m.state.value.status === 'ready' && m.state.value.payload.revision === 2 && m.state.value.selection === null, `${code} retries the complete current transfer`);
  assert.deepEqual(m.state.value.camera, camera); checks++;
  m.state.dispose();
}
{
  const m = mock();
  m.bridge.tool = async () => { throw Object.assign(Error('Bad stored view'), { code: 'storage_error' }); };
  await m.state.pollOnce();
  check(m.state.value.status === 'error' && m.state.value.error === 'Bad stored view', 'persistent storage failure is not hidden as a temporary conflict');
}
{
  const m = mock(), tool = m.bridge.tool;
  m.bridge.tool = (name, args) => args.action === 'sync' ? Promise.resolve({ state: 'loading', document_id: 'part', revision: 1, error: { code: 'workspace_busy', message: 'Dispatcher busy' } }) : tool(name, args);
  await m.state.pollOnce();
  check(m.state.value.status === 'loading' && m.state.value.error === null, 'native loading response does not display a transient error alert');
}
// The tests above mock bridge.tool. These drive CadLiveState through the real
// CadBridge request/receive/value path with MCP CallToolResult replies.
const toolResult = (value, isError = false) => ({ content: [{ type: 'text', text: JSON.stringify(value) }], structuredContent: value, isError });
function nativeBridge(respond) {
  let listener; const calls = [];
  const parent = { postMessage(message) {
    if (message.method !== 'tools/call') return;
    calls.push(message.params);
    queueMicrotask(() => listener?.({ source: parent, origin: 'https://host.example',
      data: { jsonrpc: '2.0', id: message.id, result: respond(message.params.name, message.params.arguments) } }));
  } };
  const self = { parent, addEventListener(_, fn) { listener = fn; }, removeEventListener() { listener = null; } };
  return { bridge: new CadBridge(self), calls };
}
{
  const syncError = { view_id: 'main', document_id: 'broken', revision: 1, state: 'error', changed: false,
    error: { code: 'kernel_failure', message: 'Feature taper failed to build', details: { feature_id: 'taper' } } };
  assert.deepEqual(CadBridge.value(toolResult(syncError)), syncError); checks++;
  assert.deepEqual(CadBridge.value({ content: [{ type: 'text', text: JSON.stringify(syncError) }] }), syncError); checks++;
  assert.throws(() => CadBridge.value(toolResult({ error: syncError.error }, true)), error => error.code === 'kernel_failure' && error.details.feature_id === 'taper'); checks++;
  assert.throws(() => CadBridge.value({ structuredContent: { error: syncError.error } }), error => error.code === 'kernel_failure'); checks++;
}
{
  let head = { document_id: 'part', revision: 1, failing: false }, meshFailure = null;
  const evaluation = () => `eval_${head.document_id}_${head.revision}`;
  const { bridge, calls } = nativeBridge((name, args) => {
    if (name === 'cad_context') return toolResult({ view_id: 'main', document_id: head.document_id, revision: head.revision, evaluation_id: evaluation(),
      head_revision: head.revision, stale: false, selection: null, hidden_part_ids: [] });
    if (args.action === 'sync') {
      const identity = { view_id: 'main', document_id: head.document_id, revision: head.revision, changed: true };
      if (head.failing) return toolResult({ ...identity, changed: false, state: 'error',
        error: { code: 'kernel_failure', message: `Feature taper failed in ${head.document_id}`, details: { feature_id: 'taper' } } });
      return toolResult({ ...identity, state: 'ready', evaluation_id: evaluation(), feature_id: 'base', summary: {}, hidden_part_ids: [],
        model: { features: [{ id: 'base', type: 'box' }], parameters: {} } });
    }
    if (args.action === 'mesh') {
      if (meshFailure) { const failure = meshFailure; meshFailure = null; return toolResult({ error: failure }, true); }
      const data = JSON.stringify({ ...payload(head.revision), document_id: head.document_id, evaluation_id: evaluation() });
      return toolResult({ data, offset: 0, next_offset: null, total_bytes: data.length });
    }
    if (args.action === 'context') return toolResult({ view_id: 'main', document_id: head.document_id, stale: false, selection: args.selection, hidden_part_ids: args.hidden_part_ids });
    return toolResult({ error: { code: 'invalid_argument', message: 'Unexpected tool', details: {} } }, true);
  });
  const state = new CadLiveState(bridge); state.attach('main');
  await state.pollOnce(); await state.deliveryQueue;
  check(state.value.status === 'ready' && state.value.payload.document_id === 'part', 'real bridge loads the first document');
  state.value.camera = { yaw: 1, pitch: .5, zoom: 2, pan: [0, 0] };
  state.value.selection = { reference: { entity_id: 'edge-1' }, geometry: {} };
  head = { document_id: 'broken', revision: 1, failing: true };
  await state.pollOnce();
  check(state.value.status === 'error' && state.value.error === 'Feature taper failed in broken', 'native sync error state reaches the controller with its message');
  check(state.value.document_id === 'broken' && state.value.payload === null && state.value.model === null && state.value.camera === null,
    'switching to a failing document clears the previous model instead of labelling it as the new one');
  check(state.value.selection === null && state.value.hidden_part_ids.length === 0, 'failed retarget clears picks and visibility');
  head = { document_id: 'other', revision: 1, failing: false };
  await state.pollOnce(); await state.deliveryQueue;
  check(state.value.status === 'ready' && state.value.error === null && state.value.payload.document_id === 'other' && state.value.document_id === 'other',
    'retargeting after an error loads the next document');
  const loaded = state.value.payload;
  state.value.selection = { reference: { entity_id: 'edge-1' }, geometry: {} };  // picks must be disabled by the failure, not already null
  head = { document_id: 'other', revision: 2, failing: true };
  await state.pollOnce();
  check(state.value.status === 'error' && state.value.payload === loaded && state.value.selection === null && state.value.revision === 2,
    'same-document evaluation failure keeps the last solid visible with picks disabled');
  head = { document_id: 'other', revision: 3, failing: false }; meshFailure = { code: 'stale_selection', message: 'Superseded during transfer', details: {} };
  await state.pollOnce();
  check(state.value.status === 'loading' && state.value.error === null && state.value.payload === loaded, 'tool-level isError still rejects through the real bridge');
  await state.pollOnce(); await state.deliveryQueue;
  check(state.value.status === 'ready' && state.value.payload.revision === 3, 'polling recovers after errors without resending edits');
  check(calls.every(call => call.name === 'cad_viewer' || call.name === 'cad_context'), 'recovery only issues read-only view calls');
  state.dispose(); bridge.dispose();
}
function visibilityMock(initial = [], partIds = ['base', 'cover']) {
  const m = mock(), tool = m.bridge.tool; let hidden = initial, ids = partIds;
  m.bridge.tool = async (name, args) => {
    const result = await tool(name, args);
    if (args.action === 'context') hidden = [...args.hidden_part_ids];
    if (args.action === 'sync' || name === 'cad_context') result.hidden_part_ids = [...hidden];
    if (args.action === 'mesh') {
      const data = JSON.parse(result.data);
      data.summary.assembly = { parts: ids.map(id => ({ id })), mates: [] };
      data.topology = { faces: ids.map((id, i) => ({ id: `face-${i + 1}`, part_id: id })), edges: [] };
      result.data = JSON.stringify(data); result.total_bytes = result.data.length;
    }
    return result;
  };
  return { ...m, saved: () => hidden, external(ids) { hidden = ids; }, removeCover() { ids = ['base']; m.advance(); } };
}
{
  const m = visibilityMock([], ['left/base', 'left/link', 'right/base', 'right/link', 'leftover']);
  await m.state.pollOnce(); await m.state.deliveryQueue;
  m.state.value.selection = { reference: { kind: 'face', entity_id: 'face-2' }, geometry: { part_id: 'left/link' } };
  await m.state.setPartVisible('left', false);
  assert.deepEqual(m.saved(), ['left/base', 'left/link']); checks++;
  check(m.state.value.selection === null, 'hiding a subassembly clears its descendant pick');
  await m.state.isolatePart('right');
  assert.deepEqual(m.saved(), ['left/base', 'left/link', 'leftover']); checks++;
  await m.state.setPartVisible('left/link', true);
  assert.deepEqual(m.saved(), ['left/base', 'leftover']); checks++;
  await m.state.setPartVisible('left', true);
  assert.deepEqual(m.saved(), ['leftover']); checks++;
  assert.throws(() => m.state.isolatePart('lef'), /current assembly/); checks++;
  await m.state.setPartVisible('right', false); await m.state.sendPrompt('Inspect the left module');
  const sent = m.messages.find(item => item.method === 'ui/message');
  check(sent.params.content[0].text.includes('right/base') && sent.params.content[0].text.includes('right/link'), 'group visibility publishes exact leaf occurrence paths');
  m.state.dispose();
}
{
  const m = visibilityMock(['cover']); await m.state.pollOnce(); await m.state.deliveryQueue;
  assert.deepEqual(m.state.value.hidden_part_ids, ['cover']); checks++;
  check(m.state.snapshot().hidden_part_ids[0] === 'cover', 'reopened visibility travels in context and copied requests');
  m.state.value.selection = { reference: { kind: 'face', entity_id: 'face-1' }, geometry: { part_id: 'base' } };
  await m.state.setPartVisible('base', false);
  check(m.state.value.selection === null && m.state.value.hidden_part_ids.length === 2, 'hiding a selected part clears its reference and permits all-hidden');
  assert.deepEqual(m.saved(), ['base', 'cover']); checks++;
  await m.state.isolatePart('cover'); assert.deepEqual(m.saved(), ['base']); checks++;
  await m.state.showAll(); assert.deepEqual(m.saved(), []); checks++;
  assert.throws(() => m.state.setHiddenParts(['missing']), /current assembly/); checks++;
  assert.throws(() => m.state.setHiddenParts(['base', 'base']), /unique/); checks++;
  await m.state.setHiddenParts(['cover']); await m.state.sendPrompt('Inspect the visible base');
  check(m.messages.find(item => item.method === 'ui/message').params.content[0].text.includes('"hidden_part_ids": [\n    "cover"'), 'Quick Edit message states hidden parts');
  m.state.dispose();
}
for (const stale of [false, true]) {
  const m = visibilityMock(), tool = m.bridge.tool;
  m.bridge.tool = async (name, args) => {
    if (name === 'cad_context') m.external(['cover']); // Another view changes visibility after the confirming sync.
    const result = await tool(name, args);
    if (name === 'cad_context' && stale) Object.assign(result, { stale: true, evaluation_id: 'old_evaluation',
      selection: { document_id: 'part', revision: 1, evaluation_id: 'old_evaluation', feature_id: 'base', kind: 'face', entity_id: 'face-2' } });
    return result;
  };
  await m.state.pollOnce(); await m.state.deliveryQueue;
  assert.deepEqual(m.state.value.hidden_part_ids, ['cover']); checks++;
  assert.deepEqual(m.saved(), ['cover']); checks++;
  check(m.state.value.selection === null, `${stale ? 'stale pick does not discard current visibility' : 'newer context visibility wins over earlier sync'} on reopening`);
  m.state.dispose();
}
{
  const m = visibilityMock(); await m.state.pollOnce(); await m.state.deliveryQueue;
  const tool = m.bridge.tool; let release;
  m.bridge.tool = async (name, args) => {
    const result = await tool(name, args);
    return args.action === 'sync' ? new Promise(resolve => { release = () => resolve(result); }) : result;
  };
  const poll = m.state.pollOnce(); await new Promise(resolve => setTimeout(resolve, 0));
  await m.state.setHiddenParts(['cover']); release(); await poll;
  assert.deepEqual(m.state.value.hidden_part_ids, ['cover']); checks++;
  check(m.state.visibilitySaved === m.state.visibilityVersion, 'older sync cannot undo an already acknowledged visibility edit');
  m.state.dispose();
}
{
  const m = visibilityMock(); await m.state.pollOnce(); await m.state.deliveryQueue;
  const tool = m.bridge.tool; let release;
  m.bridge.tool = (name, args) => args.action === 'context' ? new Promise(resolve => { release = async () => resolve(await tool(name, args)); }) : tool(name, args);
  const saving = m.state.setHiddenParts(['base']); await new Promise(resolve => setTimeout(resolve, 0));
  await m.state.pollOnce(); assert.deepEqual(m.state.value.hidden_part_ids, ['base']); checks++;
  check(m.state.visibilitySaved !== m.state.visibilityVersion, 'pending visibility stays local while native persistence is in flight');
  await release(); await saving; assert.deepEqual(m.saved(), ['base']); checks++;
  m.state.dispose();
}
{
  const m = visibilityMock(); await m.state.pollOnce(); await m.state.deliveryQueue;
  await Promise.all([m.state.setHiddenParts(['base']), m.state.setHiddenParts(['cover'])]);
  assert.deepEqual(m.saved(), ['cover']); checks++;
  const oldSnapshot = m.state.snapshot(); await m.state.showAll();
  await assert.rejects(m.state.saveContext(oldSnapshot), /changed/); checks++;
  m.external(['base']); await m.state.pollOnce(); assert.deepEqual(m.state.value.hidden_part_ids, ['base']); checks++;
  await m.state.setHiddenParts(['cover']); m.removeCover(); await m.state.pollOnce(); await m.state.deliveryQueue;
  assert.deepEqual(m.state.value.hidden_part_ids, []); checks++;
  check(m.state.value.payload.revision === 2, 'revision refresh prunes removed part IDs');
  m.state.attach('other_view'); assert.deepEqual(m.state.value.hidden_part_ids, []); checks++;
  m.state.dispose();
}
{
  const m = visibilityMock(); let release, blocked = false;
  m.bridge.request = async () => { if (!blocked) { blocked = true; await new Promise(resolve => { release = resolve; }); } return {}; };
  await m.state.pollOnce(); await new Promise(resolve => setTimeout(resolve, 0));
  const hiding = m.state.setHiddenParts(['cover']); await new Promise(resolve => setTimeout(resolve, 0));
  assert.deepEqual(m.saved(), ['cover']); checks++;
  check(m.state.visibilitySaved === m.state.visibilityVersion, 'optional host acknowledgment does not block native visibility persistence');
  release(); await hiding; m.state.dispose();
}
{
  const m = visibilityMock(); await m.state.pollOnce(); await m.state.deliveryQueue;
  const tool = m.bridge.tool; let rejectOld, blocked = false;
  m.bridge.tool = (name, args) => {
    if (args.action === 'context' && !blocked) { blocked = true; return new Promise((resolve, reject) => { rejectOld = reject; }); }
    return tool(name, args);
  };
  const first = m.state.setHiddenParts(['base']); await new Promise(resolve => setTimeout(resolve, 0));
  const latest = m.state.setHiddenParts(['cover']); rejectOld(Error('Superseded save failed'));
  await Promise.all([first, latest]);
  check(!m.state.value.context_error, 'superseded visibility failure does not overwrite feedback or reject the older button action');
  assert.deepEqual(m.saved(), ['cover']); checks++;
  let fail = true, attempts = 0;
  m.bridge.tool = (name, args) => { if (args.action === 'context') { attempts++; if (fail) return Promise.reject(Error('Current visibility save failed')); } return tool(name, args); };
  await assert.rejects(m.state.setHiddenParts(['base']), /Current visibility save failed/); checks++;
  check(m.state.value.context_error === 'Current visibility save failed', 'active visibility failure remains visible');
  await assert.rejects(m.state.setHiddenParts(['base']), /Current visibility save failed/); checks++;
  check(attempts === 2 && m.state.value.context_error === 'Current visibility save failed', 'explicit repeat retries unchanged unsaved visibility and retains a repeated failure');
  fail = false; await m.state.setHiddenParts(['base']);
  check(attempts === 3 && !m.state.value.context_error && m.state.visibilitySaved === m.state.visibilityVersion, 'explicit retry can persist unchanged local visibility');
  assert.deepEqual(m.saved(), ['base']); checks++;
  m.state.dispose();
}
{
  const m = visibilityMock(['cover']); await m.state.pollOnce(); await m.state.deliveryQueue;
  const tool = m.bridge.tool, notifications = []; let fail = true;
  m.state.changed = value => notifications.push({ hidden: [...value.hidden_part_ids], unsaved: value.visibility_unsaved });
  m.bridge.tool = (name, args) => args.action === 'context' && fail ? Promise.reject(Error('View save failed')) : tool(name, args);
  await assert.rejects(m.state.showAll(), /View save failed/); checks++;
  check(m.state.value.visibility_unsaved && m.state.value.hidden_part_ids.length === 0, 'failed Show all retains a retryable unsaved empty mask');
  fail = false; await m.state.showAll();
  check(notifications.some(value => value.unsaved && !value.hidden.length), 'UI receives pending visibility status even when no parts are hidden');
  check(!notifications.at(-1).unsaved && !m.state.value.visibility_unsaved, 'successful persistence immediately notifies UI to disable the completed Show all retry');
  m.state.dispose();
}
function presentationMock() {
  const m=visibilityMock(),tool=m.bridge.tool;let saved=CadRenderer.math.defaultPresentation();
  m.bridge.tool=async(name,args)=>{
    const result=await tool(name,args);
    if(args.action==='context')saved=structuredClone(args.presentation);
    if(args.action==='sync'||name==='cad_context')result.presentation=structuredClone(saved);
    return result;
  };
  return {...m,savedPresentation:()=>saved,externalPresentation(value){saved=structuredClone(value);}};
}
const inspectionPresentation=()=>({clip:{normal:[1,0,0],offset_mm:3,keep:'negative'},explode:{distance_mm:5,directions:[{part_id:'cover',direction:[0,0,1]}]}});
{
  const m=presentationMock();await m.state.pollOnce();await m.state.deliveryQueue;
  const snapshot=m.state.snapshot('old view');m.state.value.selection={reference:{kind:'face',entity_id:'face-1'},geometry:{part_id:'base'}};
  await m.state.setPresentation(inspectionPresentation());
  assert.deepEqual(m.savedPresentation(),inspectionPresentation());checks++;
  check(!m.state.value.selection&&!m.state.value.presentation_unsaved,'Clipping/explosion clear old picks and acknowledge native view persistence');
  await assert.rejects(m.state.saveContext(snapshot),/changed/);checks++;
  await m.state.sendPrompt('Inspect this clipped assembly');
  const text=m.messages.find(message=>message.method==='ui/message').params.content[0].text;
  check(text.includes('"offset_mm": 3')&&text.includes('"distance_mm": 5'),'Agent requests retain exact presentation settings beside source identity');
  m.removeCover();await m.state.pollOnce();await m.state.deliveryQueue;
  check(m.state.value.presentation.explode.directions.length===0&&m.state.value.presentation.clip.offset_mm===3,'Revision change prunes only removed directions and preserves document plane');
  m.state.attach('other');check(m.state.value.presentation.clip===null&&m.state.value.presentation.explode.distance_mm===0,'View retarget resets presentation');m.state.dispose();
}
{
  const m=presentationMock();await m.state.pollOnce();await m.state.deliveryQueue;
  const tool=m.bridge.tool;let fail=true,attempts=0;
  m.bridge.tool=(name,args)=>{if(args.action==='context'){attempts++;if(fail)return Promise.reject(Error('Presentation save failed'));}return tool(name,args);};
  await assert.rejects(m.state.setPresentation(inspectionPresentation()),/Presentation save failed/);checks++;
  check(m.state.value.presentation_unsaved&&m.state.value.presentation.clip.offset_mm===3,'Failed persistence retains the local view and retry state');
  await m.state.pollOnce();check(m.state.value.presentation.clip.offset_mm===3,'Old sync cannot overwrite an unacknowledged presentation');
  fail=false;await m.state.setPresentation(inspectionPresentation());check(attempts===2&&!m.state.value.presentation_unsaved,'Explicit repeat persists the unchanged local presentation');
  const old=m.state.snapshot();const changed=inspectionPresentation();changed.clip.offset_mm=4;m.externalPresentation(changed);
  await m.state.pollOnce();await m.state.deliveryQueue;
  check(m.state.value.presentation.clip.offset_mm===4,'External presentation change reaches an unchanged evaluation');
  await assert.rejects(m.state.saveContext(old),/changed/);checks++;
  m.state.dispose();
}
{
  const m=presentationMock();m.externalPresentation(inspectionPresentation());await m.state.pollOnce();await m.state.deliveryQueue;
  check(m.state.value.presentation.clip.offset_mm===3,'Reopening restores native presentation before sending context');
  const tool=m.bridge.tool;let release;
  m.bridge.tool=(name,args)=>args.action==='sync'?new Promise(resolve=>{release=()=>tool(name,args).then(resolve);}):tool(name,args);
  const polling=m.state.pollOnce();const changed=inspectionPresentation();changed.clip.offset_mm=8;await m.state.setPresentation(changed);
  release();await polling;check(m.state.value.presentation.clip.offset_mm===8,'An older in-flight sync cannot undo an acknowledged local plane change');m.state.dispose();
}
function measurementMock(){
  let revision=1,meta=null,status=null,sequence=0;
  const p=()=>({...payload(revision),feature_id:'assembly',topology:{faces:[{id:'face-1'},{id:'face-2'}],edges:[{id:'edge-1'}]},summary:{assembly:{parts:[{id:'left'},{id:'right'}]}}});
  const report=query=>({method:'exact_BRep_minimum_distance',coordinate_space:'committed_source_pose',units:'mm',action:query.action,coverage:'explicit_pair',minimum_distance_mm:5,status:'measured',interference_count:0,
    pairs:[{targets:query.targets||[{kind:'part',part_id:'left'},{kind:'part',part_id:'right'}],distance_mm:5,witnesses:[{a_mm:[0,0,0],b_mm:[5,0,0]}],interference:false,intersection_volume_mm3:0}]});
  const bridge={capabilities:{updateModelContext:{}},async request(){return{};},async tool(name,args){
    if(name==='cad_context')return{stale:false,document_id:'part',revision,head_revision:revision,evaluation_id:`eval_${revision}`,selection:null,...(meta?{measurement:meta}:{})};
    if(args.action==='sync')return{...ready(revision),feature_id:'assembly',...(meta?{measurement:structuredClone(meta)}:{})};
    if(args.action==='mesh'){const data=JSON.stringify(p());return{data,offset:0,next_offset:null,total_bytes:data.length};}
    if(args.action==='context')return{};
    if(args.action==='measure'){
      if(Object.hasOwn(args,'query')){
        if(args.query===null){meta=null;status=null;}
        else{meta={evaluation_id:`eval_${revision}`,job_id:`measure_${++sequence}`,query:structuredClone(args.query)};status={state:'queued'};}
      }
      return{view_id:'main',evaluation_id:`eval_${revision}`,state:'empty',...(meta?{...structuredClone(meta),...structuredClone(status)}:{})};
    }
    throw Error('Unexpected measurement fixture tool');
  }};
  const state=new CadLiveState(bridge);state.attach('main');
  return{state,bridge,finish(){status={state:'succeeded',result:{document_id:'part',revision,evaluation_id:`eval_${revision}`,feature_id:'assembly',report:report(meta.query)}};},advance(){revision++;meta=null;status=null;},fail(){status={state:'failed',error:{message:'Ambiguous geometric recovery'}};},corrupt(){status.result.revision=99;}};
}
const measurePair=()=>({action:'pair',targets:[{kind:'part',part_id:'left'},{kind:'part',part_id:'right'}]});
{
  const m=measurementMock();await m.state.pollOnce();await m.state.deliveryQueue;
  m.state.setMeasurementTarget('a',{kind:'face',entity_id:'face-1'});m.state.setMeasurementTarget('b',{kind:'part',part_id:'right'});
  check(m.state.value.measurement_targets.a.entity_id==='face-1','A current face can be retained as one measurement endpoint');
  const old=m.state.snapshot();await m.state.measure(measurePair());check(m.state.value.measurement_status.state==='queued','Native measurement request remains asynchronous');
  await assert.rejects(m.state.saveContext(old),/changed/);checks++;
  m.finish();await m.state.pollOnce();check(m.state.value.measurement_status.result.report.minimum_distance_mm===5,'Controller displays native exact result after polling');
  const request=m.state.snapshot('Inspect this clearance');check(request.measurement.job_id==='measure_1'&&CadLiveState.promptText(request).includes('"measurement"'),'Agent request contains the native measurement job and qualified query');
  m.state.setMeasurementTarget('a',{kind:'face',entity_id:'face-1'});await m.state.pollOnce();
  check(m.state.value.measurement_targets.a.kind==='face','Polling an existing result preserves newly chosen measurement targets');
  await m.state.setPresentation({clip:{normal:[0,0,1],offset_mm:3,keep:'negative'},explode:{distance_mm:20,directions:[]}});
  check(m.state.value.measurement_status.result.report.minimum_distance_mm===5,'Clipping and explosion retain the source measurement');
  await m.state.measure(null);check(!m.state.value.measurement&&m.state.value.measurement_status.state==='empty','Clear result resets native measurement metadata');
  m.state.dispose();
}
{
  const m=measurementMock();await m.state.pollOnce();const tool=m.bridge.tool;let release;
  m.bridge.tool=async(name,args)=>{if(args.action==='sync'){const result=await tool(name,args);return new Promise(resolve=>{release=()=>resolve(result);});}return tool(name,args);};
  const polling=m.state.pollOnce();await Promise.resolve();await m.state.measure(measurePair());release();await polling;
  check(m.state.value.measurement.job_id==='measure_1','An older in-flight sync cannot clear an acknowledged measurement');m.state.dispose();
}
{
  const m=measurementMock();await m.state.pollOnce();await m.state.measure(measurePair());m.finish();m.corrupt();await m.state.pollOnce();
  check(m.state.value.measurement_error.includes('identity')&&!m.state.value.measurement_status.result,'Wrong source result is rejected before display');
  m.fail();await m.state.pollOnce();check(m.state.value.measurement_status.error.message.includes('Ambiguous'),'Failed native matching remains explicit');
  m.advance();await m.state.pollOnce();check(!m.state.value.measurement&&m.state.value.measurement_targets.a===null,'New revision retires measurement and endpoint references');m.state.dispose();
}
{
  const m=measurementMock();await m.state.pollOnce();await m.state.measure(measurePair());m.finish();const tool=m.bridge.tool;
  m.bridge.tool=async(name,args)=>{const response=await tool(name,args);if(args.action==='measure'&&response.result)response.result.report.pairs[0].witnesses[0].b_mm=[0,0,0];return response;};
  await m.state.pollOnce();check(m.state.value.measurement_error.includes('witness')&&!m.state.value.measurement_status.result,'Inconsistent closest-point witnesses never reach display');m.state.dispose();
}
{
  const m=measurementMock();await m.state.pollOnce();const tool=m.bridge.tool;let release;
  m.bridge.tool=async(name,args)=>{if(args.action==='measure'&&args.query){const response=await tool(name,args);return new Promise(resolve=>{release=()=>resolve(response);});}return tool(name,args);};
  const measuring=m.state.measure(measurePair());await Promise.resolve();m.advance();await m.state.pollOnce();release();await measuring;
  check(!m.state.value.measurement&&m.state.value.payload.revision===2,'Late measurement completion cannot repopulate an obsolete evaluation');m.state.dispose();
}
const tick=()=>new Promise(resolve=>setTimeout(resolve,0));
const sectionPresentation=()=>({clip:{normal:[0,0,1],offset_mm:1,keep:'negative'},explode:{distance_mm:0,directions:[]}});
function sectionPayload(revision=1,document_id='part',draft=false){
  return {schema_version:1,document_id,revision,evaluation_id:`eval_${document_id}_${revision}${draft?'_preview':''}`,feature_id:'assembly',draft,
    summary:{bounds_mm:{min:[-1,-1,0],max:[5,1,2]},assembly:{parts:[
      {id:'left',bounds_mm:{min:[-1,-1,0],max:[1,1,2]}},{id:'right',bounds_mm:{min:[3,-1,0],max:[5,1,2]}}]}},
    mesh:{schema_version:1,feature_id:'assembly',selection_lifetime:'evaluation',positions:[[-1,-1,0],[1,-1,0],[1,1,0],[-1,1,0],[3,-1,0],[5,-1,0],[5,1,0],[3,1,0]],
      triangles:[[0,1,2],[0,2,3],[4,5,6],[4,6,7]],triangle_faces:['face-1','face-1','face-2','face-2'],edges:[]},
    topology:{schema_version:1,feature_id:'assembly',selection_lifetime:'evaluation',faces:[{id:'face-1',part_id:'left'},{id:'face-2',part_id:'right'}],edges:[]}};
}
function sectionResult(query,p){
  const source=CadRenderer.math.prepare(p),explode=query.explode||{distance_mm:0,directions:[]};
  const parts=query.part_ids||['left','right'],regions=[],curves=[],positions=[],triangles=[],triangle_regions=[];
  const sections=parts.map(part_id=>{
    const entry=explode.directions.find(item=>item.part_id===part_id),direction=entry?.direction||(part_id==='left'?[-1,0,0]:[1,0,0]);
    const displacement_mm=direction.map(v=>v/Math.hypot(...direction)*explode.distance_mm);
    const source_plane_offset_mm=query.plane.offset_mm-query.plane.normal.reduce((sum,v,i)=>sum+v*displacement_mm[i],0);
    const area=explode.distance_mm===0&&query.plane.normal.join(',')==='0,0,1'&&query.plane.offset_mm===1;
    if(area){
      const x=part_id==='left'?0:4,cap=`cap-${regions.length+1}`,points=[[x-1,-1,1],[x+1,-1,1],[x+1,1,1],[x-1,1,1]],start=positions.length;
      regions.push({id:cap,part_id,solid_index:1,area_mm2:4,perimeter_mm:8,center_mm:[x,0,1],wire_count:1});
      positions.push(...points);triangles.push([start,start+1,start+2],[start,start+2,start+3]);triangle_regions.push(cap,cap);
      points.forEach((a,i)=>{const b=points[(i+1)%4];curves.push({id:`section-${curves.length+1}`,part_id,solid_index:1,curve_kind:'line',length_mm:2,
        center_mm:a.map((v,k)=>(v+b[k])/2),bounds_mm:{min:a.map((v,k)=>Math.min(v,b[k])),max:a.map((v,k)=>Math.max(v,b[k]))},degenerate:false,points:[a,b],direction:a.map((v,k)=>(b[k]-v)/2)});});
    }
    return{part_id,source_plane_offset_mm,displacement_mm,area_mm2:area?4:0,boundary_length_mm:area?8:0,region_count:area?1:0,curve_count:area?4:0,contact_points:[],status:area?'area':'empty'};
  });
  const report={schema_version:1,units:'mm',action:'section',method:'native_BRep_planar_section',coordinate_space:'committed_source_pose',plane_coordinate_space:'displayed_world_mm',
    coverage:query.part_ids?'explicit_leaf_subset':'all_assembly_leaves',area_semantics:'sum_of_solid_sections',plane:structuredClone(query.plane),explode:structuredClone(explode),sections,regions,curves,
    mesh:{positions,triangles,triangle_regions,linear_deflection_mm:.1},area_mm2:regions.length*4,boundary_length_mm:curves.length*2,status:regions.length?'area':'empty',selection_lifetime:'section_result',tolerance_mm:1e-7,point_tolerance_mm:1e-6};
  const result={...source.identity,kernel_version:'8.0.1',model_sha256:'a'.repeat(64),native_build:'section-controller-fixture',report};
  CadRenderer.math.validateSection(result,source,{clip:{...query.plane,keep:'negative'},explode});return result;
}
function sectionMock(){
  let revision=1,document_id='part',draft=false,presentation=sectionPresentation(),hidden=[],meta=null,status=null,sequence=0,measurement=null,measureStatus=null;
  const calls=[],messages=[],p=()=>sectionPayload(revision,document_id,draft),evaluation=()=>p().evaluation_id;
  const sectionResponse=()=>({view_id:'main',evaluation_id:evaluation(),state:'empty',...(meta?{...structuredClone(meta),...structuredClone(status)}:{})});
  const bridge={capabilities:{updateModelContext:{},message:{}},async request(method,params){messages.push({method,params});return{};},async tool(name,args){
    calls.push({name,args:structuredClone(args)});
    const identity={document_id,revision,evaluation_id:evaluation(),feature_id:'assembly'};
    if(name==='cad_context')return{...identity,view_id:'main',head_revision:revision,stale:false,selection:null,presentation:structuredClone(presentation),hidden_part_ids:[...hidden],...(meta?{section:structuredClone(meta)}:{}),...(measurement?{measurement:structuredClone(measurement)}:{})};
    if(args.action==='sync')return{...identity,state:'ready',draft,model:{features:[],parameters:{}},presentation:structuredClone(presentation),hidden_part_ids:[...hidden],...(meta?{section:structuredClone(meta)}:{}),...(measurement?{measurement:structuredClone(measurement)}:{})};
    if(args.action==='mesh'){const data=JSON.stringify(p());return{data,offset:0,next_offset:null,total_bytes:data.length};}
    if(args.action==='context'){
      if(args.evaluation_id!==evaluation())throw Object.assign(Error('Evaluation changed'),{code:'stale_selection'});
      if(JSON.stringify(CadRenderer.math.sectionGeometry(args.presentation))!==JSON.stringify(CadRenderer.math.sectionGeometry(presentation))){meta=null;status=null;}
      presentation=structuredClone(args.presentation);hidden=[...args.hidden_part_ids];return{};
    }
    if(args.action==='section'){
      if(args.evaluation_id!==evaluation())throw Object.assign(Error('Evaluation changed'),{code:'stale_selection'});
      if(Object.hasOwn(args,'query')){
        if(args.query===null){meta=null;status=null;}
        else{
          const wanted=CadRenderer.math.sectionGeometry(presentation),given=CadRenderer.math.sectionGeometry({clip:{...args.query.plane,keep:'negative'},explode:args.query.explode||{distance_mm:0,directions:[]}});
          if(JSON.stringify(wanted)!==JSON.stringify(given))throw Object.assign(Error('Presentation changed'),{code:'stale_selection'});
          meta={evaluation_id:evaluation(),job_id:`section_${++sequence}`,query:structuredClone(args.query)};status={state:'queued'};
        }
      }
      return sectionResponse();
    }
    if(args.action==='measure'){
      if(Object.hasOwn(args,'query')){measurement=args.query?{evaluation_id:evaluation(),job_id:'measure_1',query:structuredClone(args.query)}:null;measureStatus={state:'queued'};}
      return{view_id:'main',evaluation_id:evaluation(),state:'empty',...(measurement?{...structuredClone(measurement),...structuredClone(measureStatus)}:{})};
    }
    if(args.action.startsWith('motion_')){draft=args.action==='motion_preview';meta=null;status=null;return{};}
    throw Error('Unexpected section fixture call');
  }};
  const state=new CadLiveState(bridge);state.attach('main');
  return{state,bridge,calls,messages,query(part_ids){return{action:'section',plane:structuredClone(presentation.clip&&{normal:presentation.clip.normal,offset_mm:presentation.clip.offset_mm}),explode:structuredClone(presentation.explode),...(part_ids?{part_ids}:{})};},
    finish(){status={state:'succeeded',result:sectionResult(meta.query,p())};},response:sectionResponse,metadata:()=>structuredClone(meta),
    fail(){status={state:'failed',error:{code:'kernel_failure',message:'Native section failed at its source solid'}};},
    advance(){revision++;meta=null;status=null;measurement=null;measureStatus=null;},retarget(){document_id='other';revision=1;meta=null;status=null;},
    externalPresentation(value){if(JSON.stringify(CadRenderer.math.sectionGeometry(value))!==JSON.stringify(CadRenderer.math.sectionGeometry(presentation))){meta=null;status=null;}presentation=structuredClone(value);},
    seed(){meta={evaluation_id:evaluation(),job_id:`section_${++sequence}`,query:this.query()};this.finish();},
    finishMeasurement(){measureStatus={state:'succeeded',result:{...p(),report:{method:'exact_BRep_minimum_distance',coordinate_space:'committed_source_pose',units:'mm',action:'pair',minimum_distance_mm:2,
      pairs:[{targets:measurement.query.targets,distance_mm:2,witnesses:[{a_mm:[1,0,0],b_mm:[3,0,0]}]}]}}};}
  };
}
{
  const m=sectionMock();await m.state.pollOnce();await m.state.deliveryQueue;
  await m.state.measure(measurePair());m.finishMeasurement();await m.state.pollOnce();
  const old=m.state.snapshot(),first=m.calls.length;await m.state.section(m.query());
  check(m.state.value.section_status.state==='queued','Native section admission remains asynchronous');
  const admission=m.calls.slice(first);check(admission[0].args.action==='context'&&admission[1].args.action==='section','Section starts only after its presentation is persisted');
  await assert.rejects(m.state.saveContext(old),/changed/);checks++;
  const snapshot=m.state.snapshot('Review the section');check(snapshot.section.job_id==='section_1'&&snapshot.measurement.job_id==='measure_1','Independent native section and distance jobs travel in the same context');
  check(CadLiveState.promptText(snapshot).includes('derived review geometry'),'Agent context identifies section surfaces as derived review geometry');
  m.finish();await m.state.pollOnce();const result=m.state.value.section_status.result;
  check(result.report.area_mm2===8&&result.report.regions.length===2,'Validated native cap regions and exact summed area reach state');
  const reads=m.calls.filter(c=>c.args.action==='section'&&!Object.hasOwn(c.args,'query')).length;
  await m.state.pollOnce();await m.state.pollOnce();
  check(m.calls.filter(c=>c.args.action==='section'&&!Object.hasOwn(c.args,'query')).length===reads&&m.state.value.section_status.result===result,'Completed section reports remain cached while sync qualifies their metadata');
  const flipped=sectionPresentation();flipped.clip.keep='positive';await m.state.setPresentation(flipped);await m.state.setHiddenParts(['left']);await m.state.pollOnce();
  check(m.state.value.section_status.result===result,'Kept-side reversal and visibility reuse the same immutable native section');
  const zeroOverride=structuredClone(flipped);zeroOverride.explode.directions=[{part_id:'left',direction:[0,0,1]}];await m.state.setPresentation(zeroOverride);
  check(m.state.value.section_status.result===result,'Zero-distance direction overrides do not retire an unchanged section');
  await m.state.section(null);check(m.state.value.section===null&&m.state.value.section_status.state==='empty','Clear section removes its metadata and reports an empty native slot');
  check(m.state.value.measurement_status.result.report.minimum_distance_mm===2,'Clearing a section leaves the independent source measurement intact');
  await m.state.deliveryQueue;check(m.messages.some(item=>item.params.structuredContent?.section?.job_id==='section_1'),'New section metadata is delivered through optional host context');m.state.dispose();
}
{
  const m=sectionMock();m.seed();await m.state.pollOnce();await m.state.deliveryQueue;
  check(m.state.value.section_status.result.report.area_mm2===8,'Opening a view restores and validates its existing native section');
  const old=m.state.snapshot();m.externalPresentation({...sectionPresentation(),clip:{normal:[0,0,1],offset_mm:2,keep:'negative'}});await m.state.pollOnce();
  check(!m.state.value.section&&!m.state.value.section_status,'External plane change retires the previous caps');
  await assert.rejects(m.state.saveContext(old),/changed/);checks++;
  m.state.dispose();
}
{
  const m=sectionMock();await m.state.pollOnce();await m.state.section(m.query(['right']));m.finish();await m.state.pollOnce();
  check(m.state.value.section_status.result.report.area_mm2===4&&m.state.value.section_status.result.report.coverage==='explicit_leaf_subset','Deliberate subset coverage stays explicit beside native section area');
  const result=m.state.value.section_status.result,tool=m.bridge.tool;let fail=true;
  m.bridge.tool=(name,args)=>args.action==='context'&&fail?Promise.reject(Error('Plane persistence failed')):tool(name,args);
  const moved=sectionPresentation();moved.clip.offset_mm=3;
  await assert.rejects(m.state.setPresentation(moved),/Plane persistence failed/);checks++;
  check(!m.state.value.section&&!m.state.value.section_status&&m.state.value.presentation_unsaved,'A local plane change removes old caps immediately even when persistence fails');
  await m.state.pollOnce();check(!m.state.value.section_status?.result,'Old native metadata cannot restore caps while the new plane is unsaved');
  const before=m.calls.filter(c=>c.args.action==='section'&&c.args.query).length;
  await assert.rejects(m.state.section(m.query()),/match/);checks++;
  check(m.calls.filter(c=>c.args.action==='section'&&c.args.query).length===before&&result.report.area_mm2===4,'Wrong-plane starts are rejected before a native mutation');
  fail=false;await m.state.setPresentation(moved);m.state.dispose();
}
{
  const m=sectionMock();await m.state.pollOnce();const tool=m.bridge.tool;let release;
  m.bridge.tool=async(name,args)=>{if(args.action==='sync'){const response=await tool(name,args);return new Promise(resolve=>{release=()=>resolve(response);});}return tool(name,args);};
  const polling=m.state.pollOnce();await tick();await m.state.section(m.query());release();await polling;
  check(m.state.value.section.job_id==='section_1','An older in-flight sync cannot clear an acknowledged section admission');m.state.dispose();
}
{
  const m=sectionMock();await m.state.pollOnce();const tool=m.bridge.tool;let release;
  m.bridge.tool=async(name,args)=>{if(args.action==='section'&&args.query){const response=await tool(name,args);return new Promise(resolve=>{release=()=>resolve(response);});}return tool(name,args);};
  const starting=m.state.section(m.query());await tick();const moved=sectionPresentation();moved.clip.offset_mm=3;await m.state.setPresentation(moved);release();await starting;
  check(!m.state.value.section&&!m.state.value.section_status&&!m.state.value.sectioning,'Late section admission cannot repopulate a changed plane');m.state.dispose();
}
{
  const m=sectionMock();await m.state.pollOnce();await m.state.section(m.query());m.finish();const tool=m.bridge.tool;let release;
  m.bridge.tool=async(name,args)=>{if(args.action==='section'&&!Object.hasOwn(args,'query')){const response=await tool(name,args);return new Promise(resolve=>{release=()=>resolve(response);});}return tool(name,args);};
  const polling=m.state.pollOnce();await tick();const moved=sectionPresentation();moved.clip.offset_mm=4;await m.state.setPresentation(moved);release();await polling;
  check(!m.state.value.section_status?.result&&!m.state.value.section,'Late completed caps cannot restore a section after its plane changes');m.state.dispose();
}
for(const change of ['revision','retarget','invalidate','motion']){
  const m=sectionMock();await m.state.pollOnce();const tool=m.bridge.tool;let release;
  m.bridge.tool=async(name,args)=>{if(args.action==='section'&&args.query){const response=await tool(name,args);return new Promise(resolve=>{release=()=>resolve(response);});}return tool(name,args);};
  const starting=m.state.section(m.query());await tick();
  if(change==='revision'){m.advance();await m.state.pollOnce();}
  else if(change==='retarget'){m.retarget();await m.state.pollOnce();}
  else if(change==='invalidate'){m.state.invalidate();}
  else await m.state.previewValues({});
  release();await starting;check(!m.state.value.section&&!m.state.value.section_status,`Late section admission cannot restore caps after ${change}`);
  if(change==='motion')await assert.rejects(m.state.section(m.query()),/committed/),checks++;
  m.state.dispose();
}
{
  const m=sectionMock();await m.state.pollOnce();const tool=m.bridge.tool;let release;
  m.bridge.tool=(name,args)=>args.action==='context'?new Promise(resolve=>{release=()=>tool(name,args).then(resolve);}):tool(name,args);
  const starting=m.state.section(m.query());await tick();
  check(!m.calls.some(c=>c.args.action==='section')&&m.state.value.sectioning,'Pending presentation persistence delays native admission');
  m.state.invalidate();release();await assert.rejects(starting,/changed/);checks++;
  check(!m.calls.some(c=>c.args.action==='section')&&!m.state.value.section,'A revision invalidation during persistence prevents a section start');m.state.dispose();
}
{
  const m=sectionMock();let release;
  m.bridge.request=()=>new Promise(resolve=>{release=resolve;});await m.state.pollOnce();await tick();
  await m.state.section(m.query());check(m.state.value.section_status.state==='queued','Optional host acknowledgment does not block section admission');
  m.advance();await m.state.pollOnce();check(m.state.value.payload.revision===2&&!m.state.value.section,'HEAD polling continues while section context delivery awaits the host');
  m.state.dispose();release?.({});
}
{
  const m=sectionMock();await m.state.pollOnce();const tool=m.bridge.tool;let lost=true;
  m.bridge.tool=async(name,args)=>{const response=await tool(name,args);if(args.action==='section'&&args.query&&lost){lost=false;throw Error('Section acknowledgment lost');}return response;};
  await assert.rejects(m.state.section(m.query()),/acknowledgment/);checks++;
  check(m.calls.filter(c=>c.args.action==='section'&&c.args.query).length===1&&!m.state.value.section&&m.state.value.section_error.includes('lost'),'An uncertain section admission is never automatically retried');
  await m.state.pollOnce();check(m.state.value.section.job_id==='section_1'&&m.state.value.section_status.state==='queued','Read-only sync reconciles an admission whose acknowledgment was lost');m.state.dispose();
}

// An empty native slot is not evidence that a failed admission was dismissed.
// Keep its feedback across read-only sync until a new action or source retires it.

{
  const p=sectionPayload(),data=JSON.stringify(p);let attempts=0;
  const {bridge}=nativeBridge((name,args)=>{
    if(name==='cad_context')return toolResult({view_id:'main',document_id:p.document_id,revision:p.revision,head_revision:p.revision,evaluation_id:p.evaluation_id,stale:false,selection:null,presentation:sectionPresentation()});
    if(args.action==='sync')return toolResult({...p,state:'ready',model:{features:[],parameters:{}},presentation:sectionPresentation()});
    if(args.action==='mesh')return toolResult({data,offset:0,next_offset:null,total_bytes:data.length});
    if(args.action==='context')return toolResult({});
    if(args.action==='section'){attempts++;return toolResult({error:{code:'workspace_busy',message:'Native section admission lock held',details:{}}},true);}
    throw Error('Unexpected native admission fixture request');
  });
  const state=new CadLiveState(bridge);state.attach('main');await state.pollOnce();
  await assert.rejects(state.section({action:'section',plane:{normal:[0,0,1],offset_mm:1},explode:{distance_mm:0,directions:[]}}),error=>error.code==='workspace_busy');checks++;
  await state.pollOnce();await state.pollOnce();
  check(state.value.section_error==='Native section admission lock held'&&!state.value.section,'Actual bridge tool errors remain visible after empty native sync');
  check(attempts===1,'Actual bridge reconciliation never retries a rejected section start');state.dispose();bridge.dispose();
}

for(const code of ['workspace_busy','transport_error']){
  const m=sectionMock();await m.state.pollOnce();await m.state.deliveryQueue;
  const tool=m.bridge.tool;let fail=true,attempts=0;
  m.bridge.tool=(name,args)=>{
    if(args.action==='section'&&args.query){attempts++;if(fail)return Promise.reject(Object.assign(Error(`Section admission failed: ${code}`),{code}));}
    return tool(name,args);
  };
  await assert.rejects(m.state.section(m.query()),/admission failed/);checks++;
  const error=m.state.value.section_error;await m.state.pollOnce();await m.state.pollOnce();
  check(!m.state.value.section&&!m.state.value.section_status&&m.state.value.section_error===error,`${code} admission feedback survives an empty native slot`);
  check(attempts===1,'Empty-slot reconciliation does not repeat a failed section mutation');
  fail=false;await m.state.section(m.query());
  check(!m.state.value.section_error&&m.state.value.section_status.state==='queued','Explicit successful admission replaces the previous error');m.state.dispose();
}
{
  const m=sectionMock();await m.state.pollOnce();await m.state.section(null);
  const tool=m.bridge.tool;let fail=true;
  m.bridge.tool=(name,args)=>args.action==='context'&&fail?Promise.reject(Object.assign(Error('Section presentation lock held'),{code:'workspace_busy'})):tool(name,args);
  await assert.rejects(m.state.section(m.query()),/lock held/);checks++;
  const error=m.state.value.section_error;await m.state.pollOnce();
  check(m.state.value.section_status.state==='empty'&&m.state.value.section_error===error,'A failed admission keeps feedback even when a prior clear left empty status');
  fail=false;await m.state.section(null);
  check(!m.state.value.section_error,'An explicit clear dismisses admission feedback');m.state.dispose();
}
{
  const m=sectionMock();await m.state.pollOnce();const tool=m.bridge.tool;
  m.bridge.tool=(name,args)=>args.action==='section'&&args.query?Promise.reject(Error('Section start was not admitted')):tool(name,args);
  await assert.rejects(m.state.section(m.query()),/not admitted/);checks++;
  m.advance();await m.state.pollOnce();
  check(!m.state.value.section_error&&m.state.value.payload.revision===2,'A new source evaluation retires feedback from its old failed admission');m.state.dispose();
}

for(const corrupt of ['source','plane','coverage','ownership']){
  const m=sectionMock();await m.state.pollOnce();await m.state.section(m.query(['right']));m.finish();const tool=m.bridge.tool;
  m.bridge.tool=async(name,args)=>{const response=await tool(name,args);if(args.action==='section'&&response.result){
    if(corrupt==='source')response.result.revision=99;
    if(corrupt==='plane')response.result.report.plane.offset_mm=99;
    if(corrupt==='coverage')response.query.part_ids=['left'];
    if(corrupt==='ownership')response.result.report.mesh.triangle_regions[0]='face-1';
  }return response;};
  await m.state.pollOnce();check(m.state.value.section_error&&!m.state.value.section_status?.result,`A section with mismatched ${corrupt} never reaches rendered state`);m.state.dispose();
}
{
  const m=sectionMock();await m.state.pollOnce();await m.state.section(m.query());m.fail();await m.state.pollOnce();
  check(m.state.value.section_status.state==='failed'&&m.state.value.section_status.error.message.includes('source solid'),'Native section failures stay explicit in the independent result slot');
  const tool=m.bridge.tool;let cancelAttempts=0;m.bridge.tool=(name,args)=>{if(args.action==='section'&&args.query===null){cancelAttempts++;return Promise.reject(Error('Native cancellation failed'));}return tool(name,args);};
  await assert.rejects(m.state.section(null),/cancellation/);checks++;
  check(!m.state.value.section_status?.result&&m.state.value.section_error.includes('cancellation'),'A cancellation failure cannot leave a cleared result falsely displayed');
  await m.state.pollOnce();check(cancelAttempts===1,'A failed cancellation is not repeated during read-only reconciliation');m.state.dispose();
}
{
  const m=sectionMock();await m.state.pollOnce();const outside=sectionPresentation();outside.clip.offset_mm=1e12;await m.state.setPresentation(outside);
  await m.state.section(m.query());m.finish();await m.state.pollOnce();
  check(m.state.value.section_status.state==='succeeded'&&m.state.value.section_status.result.report.status==='empty'&&!m.state.value.section_status.result.report.mesh.triangles.length,'An outside plane yields a qualified complete empty section without invented caps');
  const disabled=structuredClone(outside);disabled.clip=null;await m.state.setPresentation(disabled);
  check(!m.state.value.section&&!m.state.value.section_status,'Disabling clipping retires the section result');
  await assert.rejects(m.state.section({action:'section',plane:{normal:[0,0,1],offset_mm:1}}),/match/);checks++;
  m.state.dispose();
}
{
  const m=sectionMock();await m.state.pollOnce();const exploded=sectionPresentation();exploded.explode={distance_mm:5,directions:[{part_id:'right',direction:[0,0,-1]},{part_id:'left',direction:[0,0,1]}]};
  await m.state.setPresentation(exploded);await m.state.section(m.query());m.finish();await m.state.pollOnce();const result=m.state.value.section_status.result;
  const reordered=structuredClone(exploded);reordered.explode.directions.reverse();await m.state.setPresentation(reordered);await m.state.pollOnce();
  check(m.state.value.section_status.result===result,'Equivalent direction order retains the qualified native section');
  const moved=structuredClone(reordered);moved.explode.distance_mm=6;await m.state.setPresentation(moved);
  check(!m.state.value.section&&!m.state.value.section_status,'Changing exploded placement retires its caps immediately');m.state.dispose();
}
const reviewColor=()=>({default_color:[.2,.4,.6],parts:[{part_id:'left',color:[1,.2,0]}]});
const reviewCamera=()=>({yaw:1,pitch:.25,zoom:2,pan:[.1,-.2]});
function reviewMock(){
  const m=sectionMock(),tool=m.bridge.tool;let appearance=CadRenderer.math.defaultAppearance(),camera=reviewCamera(),presets=[],rightRemoved=false;
  const response=async()=>({...await tool('cad_context',{view_id:'main'}),appearance:structuredClone(appearance),camera:structuredClone(camera),presets:structuredClone(presets)});
  m.bridge.tool=async(name,args)=>{
    if(args.action==='preset'){
      m.calls.push({name,args:structuredClone(args)});
      if(args.operation==='save'){
        const native=await response(),saved={name:args.name,camera:structuredClone(camera),appearance:structuredClone(appearance),presentation:native.presentation,hidden_part_ids:native.hidden_part_ids};
        if(!presets.some(p=>p.name===args.name)&&presets.length===16)throw Error('Preset capacity reached');
        presets=presets.filter(p=>p.name!==args.name);presets.push(saved);presets.sort((a,b)=>a.name.localeCompare(b.name));
      }else if(args.operation==='apply'){
        const saved=presets.find(p=>p.name===args.name);if(!saved)throw Error('Preset missing');
        await tool(name,{action:'context',view_id:args.view_id,evaluation_id:args.evaluation_id,selection:null,presentation:structuredClone(saved.presentation),hidden_part_ids:[...saved.hidden_part_ids]});
        appearance=structuredClone(saved.appearance);camera=structuredClone(saved.camera);
      }else if(args.operation==='delete')presets=presets.filter(p=>p.name!==args.name);
      return response();
    }
    const native=await tool(name,args);
    if(args.action==='mesh'&&rightRemoved){const current=JSON.parse(native.data);current.summary.bounds_mm.max=[1,1,2];current.summary.assembly.parts=current.summary.assembly.parts.filter(part=>part.id==='left');current.mesh.positions=current.mesh.positions.slice(0,4);current.mesh.triangles=current.mesh.triangles.slice(0,2);current.mesh.triangle_faces=current.mesh.triangle_faces.slice(0,2);current.topology.faces=current.topology.faces.filter(face=>face.part_id==='left');native.data=JSON.stringify(current);native.total_bytes=native.data.length;}
    if(args.action==='context'){if(args.appearance)appearance=structuredClone(args.appearance);if(args.camera)camera=structuredClone(args.camera);}
    if(args.action==='sync'||name==='cad_context')Object.assign(native,{appearance:structuredClone(appearance),camera:structuredClone(camera),presets:structuredClone(presets)});
    return native;
  };
  return {...m,nativeAppearance:()=>structuredClone(appearance),nativeCamera:()=>structuredClone(camera),nativePresets:()=>structuredClone(presets),
    externalReview(settings){if(settings.appearance)appearance=structuredClone(settings.appearance);if(settings.camera)camera=structuredClone(settings.camera);if(settings.presets)presets=structuredClone(settings.presets);},
    removeRight(){rightRemoved=true;m.advance();},
    retarget(){m.retarget();appearance=CadRenderer.math.defaultAppearance();camera=reviewCamera();presets=[];},
    seedReview(name='Overview'){m.seed();presets=[{name,camera:reviewCamera(),appearance:reviewColor(),presentation:sectionPresentation(),hidden_part_ids:['right']}];appearance=reviewColor();}
  };
}
{
  const m=reviewMock();await m.state.pollOnce();await m.state.deliveryQueue;
  const old=m.state.snapshot(),selection={reference:{document_id:'part',revision:1,evaluation_id:m.state.value.payload.evaluation_id,feature_id:'assembly',kind:'face',entity_id:'face-1'},geometry:{part_id:'left'}};
  m.state.value.selection=selection;await m.state.setAppearance(reviewColor());
  assert.deepEqual(m.nativeAppearance(),reviewColor());checks++;
  check(m.state.value.selection===selection&&!m.state.value.appearance_unsaved,'Color persistence retains the current qualified source pick');
  await assert.rejects(m.state.saveContext(old),/changed/);checks++;
  check(m.state.snapshot().appearance.parts[0].part_id==='left','Agent and copied context include exact leaf appearance');
  const before=m.state.snapshot();m.state.setCamera({...reviewCamera(),zoom:3});await assert.rejects(m.state.saveContext(before),/changed/);checks++;
  await m.state.publishContext();check(m.nativeCamera().zoom===3,'Camera version changes are persisted with current view settings');
  for(const bad of [{default_color:[1.1,0,0],parts:[]},{default_color:[0,0,0],parts:[{part_id:'missing',color:[1,0,0]}]},{default_color:[0,0,0],parts:[{part_id:'left',color:[1,0,0]},{part_id:'left',color:[0,1,0]}]},{default_color:[0,0,0],parts:[],alpha:.5}]){
    assert.throws(()=>m.state.setAppearance(bad));checks++;
  }
  await m.state.setAppearance(CadRenderer.math.defaultAppearance());check(!m.state.value.appearance.parts.length&&m.state.value.appearance.default_color[0]===.66,'Reset colors restores the exact default appearance');m.state.dispose();
}
{
  const m=reviewMock();m.seedReview();await m.state.pollOnce();await m.state.deliveryQueue;
  check(m.state.value.presets[0].name==='Overview'&&m.state.value.appearance.parts[0].part_id==='left','Opening restores native review presets and current appearance');
  const result=m.state.value.section_status.result;await m.state.setAppearance({...reviewColor(),default_color:[.1,.2,.3]});
  check(m.state.value.section_status.result===result,'Appearance changes retain the exact geometric section');
  await m.state.preset('apply','Overview');check(m.state.value.section_status.result===result,'Applying a preset with the same plane and placement reuses the qualified section');
  check(m.state.value.hidden_part_ids[0]==='right'&&m.state.value.camera.zoom===2,'Applying a preset restores camera and hidden leaf parts');
  const flipped=sectionPresentation();flipped.clip.keep='positive';await m.state.setPresentation(flipped);await m.state.preset('save','Opposite side');
  await m.state.preset('apply','Overview');check(m.state.value.section_status.result===result,'Applying a kept-side-only preset retains its native cap report');
  const moved=sectionPresentation();moved.clip.offset_mm=2;await m.state.setPresentation(moved);await m.state.preset('save','Outside');await m.state.preset('apply','Overview');
  check(!m.state.value.section_status?.result,'Applying different section geometry cannot revive retired cap reports');
  await m.state.preset('delete','Opposite side');check(!m.state.value.presets.some(p=>p.name==='Opposite side'),'Deleting a preset removes its saved review configuration');m.state.dispose();
}
{
  const m=reviewMock();await m.state.pollOnce();await m.state.setAppearance(reviewColor());m.state.setCamera(reviewCamera());await m.state.setHiddenParts(['left']);
  const first=m.calls.length;await m.state.preset('save','Assembly overview');const calls=m.calls.slice(first);
  check(calls[0].args.action==='context'&&calls[1].args.action==='preset','Saving a preset persists the current snapshot before its native mutation');
  const saved=m.nativePresets()[0];assert.deepEqual(saved.appearance,reviewColor());checks++;assert.deepEqual(saved.camera,reviewCamera());checks++;
  check(saved.hidden_part_ids[0]==='left'&&saved.presentation.clip.offset_mm===1,'Saved presets capture appearance, camera, clipping and visibility together');
  await m.state.setAppearance({default_color:[0,1,0],parts:[]});await m.state.preset('save','Assembly overview');
  check(m.nativePresets().length===1&&m.nativePresets()[0].appearance.default_color[1]===1,'Saving the same plain name replaces the existing preset');
  for(const name of ['', '   ', 'bad\nname', 'é'.repeat(33)]){await assert.rejects(m.state.preset('save',name),/UTF-8/);checks++;}
  await m.state.previewValues({});await assert.rejects(m.state.preset('apply','Assembly overview'),/pose/);checks++;
  await assert.rejects(m.state.preset('save','Draft'),/pose/);checks++;await m.state.preset('list');check(m.state.value.presets.length===1,'Draft review permits read-only preset listing');m.state.dispose();
}
{
  const m=reviewMock();await m.state.pollOnce();const tool=m.bridge.tool;let fail=true;
  m.bridge.tool=(name,args)=>args.action==='context'&&fail?Promise.reject(Error('Appearance write failed')):tool(name,args);
  await assert.rejects(m.state.setAppearance(reviewColor()),/write/);checks++;
  check(m.state.value.appearance_unsaved&&m.state.value.appearance.parts[0].part_id==='left','Failed color persistence keeps a visible retryable local choice');
  await m.state.pollOnce();check(m.state.value.appearance.parts[0].part_id==='left','Native sync cannot overwrite an unsaved local appearance');
  fail=false;await m.state.setAppearance(reviewColor());check(!m.state.value.appearance_unsaved,'Explicit repeat persists unchanged unsaved colors');m.state.dispose();
}
{
  const m=reviewMock();await m.state.pollOnce();await m.state.preset('save','Old camera');const tool=m.bridge.tool;let release;
  m.bridge.tool=async(name,args)=>{if(args.action==='preset'&&args.operation==='apply'){const response=await tool(name,args);return new Promise(resolve=>{release=()=>resolve(response);});}return tool(name,args);};
  const applying=m.state.preset('apply','Old camera');await tick();
  m.state.setCamera({...reviewCamera(),zoom:7});const newer=m.state.setAppearance({default_color:[.9,.1,.5],parts:[]});
  release();await applying;await newer;
  check(m.state.value.camera.zoom===7&&m.state.value.appearance.default_color[0]===.9,'A delayed apply acknowledgment cannot overwrite later camera or color edits');
  check(m.nativeCamera().zoom===7&&m.nativeAppearance().default_color[0]===.9,'Queued context persistence restores the later local choices after the native apply');m.state.dispose();
}
for(const change of ['revision','retarget','attach']){
  const m=reviewMock();m.seedReview();await m.state.pollOnce();const tool=m.bridge.tool;let release;
  m.bridge.tool=async(name,args)=>{if(args.action==='preset'&&args.operation==='apply'){const response=await tool(name,args);return new Promise(resolve=>{release=()=>resolve(response);});}return tool(name,args);};
  const applying=m.state.preset('apply','Overview');await tick();
  if(change==='revision'){m.advance();await m.state.pollOnce();}
  else if(change==='retarget'){m.retarget();await m.state.pollOnce();}
  else m.state.attach('different');
  release();await applying;
  check(!m.state.value.preset_busy&&m.state.value.payload?.evaluation_id!=='eval_part_1',`A delayed preset apply cannot restore an obsolete source after ${change}`);
  if(change!=='revision')check(!m.state.value.presets.length&&!m.state.value.appearance.parts.length,`${change} clears presets and part color overrides`);
  m.state.dispose();
}
{
  const m=reviewMock();m.seedReview();await m.state.pollOnce();await m.state.setAppearance(CadRenderer.math.defaultAppearance());const tool=m.bridge.tool;let lost=true;
  m.bridge.tool=async(name,args)=>{const response=await tool(name,args);if(args.action==='preset'&&args.operation==='apply'&&lost){lost=false;throw Error('Preset acknowledgment lost');}return response;};
  await assert.rejects(m.state.preset('apply','Overview'),/acknowledgment/);checks++;
  const attempts=m.calls.filter(call=>call.args.action==='preset'&&call.args.operation==='apply').length;
  await m.state.pollOnce();check(m.state.value.appearance.parts[0].part_id==='left'&&m.state.value.hidden_part_ids[0]==='right','Read-only sync reconciles a successfully applied preset after its acknowledgment is lost');
  check(m.calls.filter(call=>call.args.action==='preset'&&call.args.operation==='apply').length===attempts,'An uncertain preset apply is never automatically repeated');m.state.dispose();
}
{
  const m=reviewMock();let release;m.bridge.request=()=>new Promise(resolve=>{release=resolve;});await m.state.pollOnce();await tick();
  await m.state.preset('save','Without host delay');check(m.state.value.presets[0].name==='Without host delay','Optional host acknowledgment does not block native preset saving');
  await m.state.preset('apply','Without host delay');m.advance();await m.state.pollOnce();check(m.state.value.payload.revision===2,'Source polling remains available while optional preset context delivery is pending');
  m.state.dispose();release?.({});
}
{
  const m=reviewMock();await m.state.pollOnce();await m.state.deliveryQueue;const tool=m.bridge.tool;let release;
  m.bridge.tool=async(name,args)=>{if(args.action==='sync'){const response=await tool(name,args);return new Promise(resolve=>{release=()=>resolve(response);});}return tool(name,args);};
  const polling=m.state.pollOnce();await tick();await m.state.setAppearance(reviewColor());m.state.setCamera({...reviewCamera(),zoom:5});await m.state.publishContext();release();await polling;
  check(m.state.value.appearance.parts[0].part_id==='left'&&m.state.value.camera.zoom===5,'An older in-flight sync cannot overwrite acknowledged local colors or camera');m.state.dispose();
}
{
  const m=reviewMock();await m.state.pollOnce();await m.state.preset('save','Keep');const tool=m.bridge.tool;let release;
  m.bridge.tool=async(name,args)=>{if(args.action==='sync'){const response=await tool(name,args);return new Promise(resolve=>{release=()=>resolve(response);});}return tool(name,args);};
  const polling=m.state.pollOnce();await tick();await m.state.preset('delete','Keep');release();await polling;
  check(!m.state.value.presets.length,'An older sync cannot restore a deleted saved view');m.state.dispose();
}
for(const corrupt of ['evaluation','feature']){
  const m=reviewMock();await m.state.pollOnce();await m.state.preset('save','Good');const tool=m.bridge.tool;
  m.bridge.tool=async(name,args)=>{const response=await tool(name,args);if(args.action==='preset'&&args.operation==='apply')response[corrupt==='evaluation'?'evaluation_id':'feature_id']='old-source';return response;};
  await assert.rejects(m.state.preset('apply','Good'),/evaluation/);checks++;
  check(m.state.value.preset_error.includes('evaluation'),'Mismatched native preset identity remains explicit');m.state.dispose();
}
{
  const m=reviewMock();m.seedReview();await m.state.pollOnce();const tool=m.bridge.tool;let reject;
  m.bridge.tool=async(name,args)=>{if(args.action==='preset'&&args.operation==='apply'){await tool(name,args);return new Promise((resolve,no)=>{reject=no;});}return tool(name,args);};
  const applying=m.state.preset('apply','Overview');await tick();m.retarget();await m.state.pollOnce();reject(Error('Old preset request failed'));
  await assert.rejects(applying,/Old preset/);checks++;
  check(!m.state.value.preset_error&&m.state.value.payload.document_id==='other','Late failure from another source cannot overwrite the new document preset feedback');m.state.dispose();
}
{
  const m=reviewMock();await m.state.pollOnce();m.externalReview({appearance:reviewColor(),camera:{...reviewCamera(),zoom:4}});const snapshot=m.state.snapshot();await m.state.pollOnce();
  check(m.state.value.appearance.parts[0].part_id==='left'&&m.state.value.camera.zoom===4,'Native external review changes reach an unchanged source evaluation');
  await assert.rejects(m.state.saveContext(snapshot),/changed/);checks++;
  m.state.value.selection={reference:{entity_id:'face-1'},geometry:{part_id:'left'}};await m.state.setAppearance({default_color:[.1,.2,.3],parts:[]});
  check(m.state.value.selection.reference.entity_id==='face-1','Appearance changes preserve source selection identity after external view updates');m.state.dispose();
}
{
  const m=reviewMock();m.seedReview();const outside=sectionPresentation();outside.clip.offset_mm=3;
  m.externalReview({presets:[{name:'Outside',camera:reviewCamera(),appearance:reviewColor(),presentation:outside,hidden_part_ids:[]}]});
  await m.state.pollOnce();const result=m.state.value.section_status.result;check(result.report.area_mm2===8,'Current caps exist before applying a preset for a different plane');
  await m.state.preset('apply','Outside');check(!m.state.value.section&&!m.state.value.section_status?.result&&m.state.value.presentation.clip.offset_mm===3,'Applying different plane geometry retires its section atomically');m.state.dispose();
}
{
  const m=reviewMock();await m.state.pollOnce();const appearance={default_color:[.3,.4,.5],parts:[{part_id:'left',color:[1,0,0]},{part_id:'right',color:[0,1,0]}]},presentation=sectionPresentation();
  presentation.explode.directions=[{part_id:'right',direction:[1,0,0]}];await m.state.setAppearance(appearance);await m.state.setPresentation(presentation);await m.state.setHiddenParts(['right']);await m.state.preset('save','Both leaves');
  m.removeRight();await m.state.pollOnce();await m.state.deliveryQueue;
  check(m.state.value.appearance.parts.length===1&&m.state.value.appearance.parts[0].part_id==='left'&&m.state.value.appearance.default_color[0]===.3,'Revision refresh prunes removed leaf colors while retaining the default and surviving leaf');
  const preset=m.state.value.presets[0];check(!preset.hidden_part_ids.length&&!preset.presentation.explode.directions.length&&preset.appearance.parts.length===1,'Preset restoration prunes removed visibility, explosion and color owners against the new source');
  m.state.dispose();
}
function annotationMock(){
  const m=reviewMock(),tool=m.bridge.tool;let notes=[],next=0;
  const current=async()=>{const context=await tool('cad_context',{view_id:'main'});return{...context,annotations:notes.map(note=>({...structuredClone(note),status:note.evaluation_id===context.evaluation_id&&!context.draft?'current':'retired'}))};};
  m.bridge.tool=async(name,args)=>{
    if(args.action==='annotation'){
      m.calls.push({name,args:structuredClone(args)});const c=await current();
      if(args.evaluation_id!==c.evaluation_id)throw Error('Source changed');
      if(args.operation==='add'){
        const a=args.anchor,part_id=a.kind==='part'?a.part_id:null,point_mm=part_id==='right'?[4,0,1]:part_id==='left'?[0,0,1]:[2,0,1];
        notes.push({id:`ann_${++next}`,text:args.text,document_id:c.document_id,revision:c.revision,evaluation_id:c.evaluation_id,feature_id:c.feature_id,anchor_lifetime:'evaluation',coordinate_space:'committed_source_pose',anchor:{kind:a.kind,part_id,point_mm,position_semantics:'bounds_center'}});
      }else if(args.operation==='update'){const n=notes.find(note=>note.id===args.annotation_id);if(!n)throw Error('Missing note');n.text=args.text;}
      else if(args.operation==='delete')notes=notes.filter(note=>note.id!==args.annotation_id);
      else if(args.operation==='clear')notes=[];
      return current();
    }
    const result=await tool(name,args);if(name==='cad_context'||args.action==='sync')result.annotations=(await current()).annotations;return result;
  };
  return{...m,notes:()=>structuredClone(notes),retarget(){notes=[];m.retarget();}};
}
{
 const m=annotationMock();await m.state.pollOnce();await m.state.deliveryQueue;
 const old=m.state.snapshot();await m.state.annotation('add',{anchor:{kind:'part',part_id:'right'},text:'Inspect this leaf <b>plain text</b>'});
 check(m.state.value.annotations.length===1&&m.state.value.annotations[0].anchor.part_id==='right','Native review note returns its qualified leaf inspection center');
 check(!m.state.matches(old),'Review notes invalidate older request snapshots');
 const note=structuredClone(m.state.value.annotations[0]),snapshot=m.state.snapshot('Inspect note 1');
 check(snapshot.annotations[0].evaluation_id===snapshot.evaluation_id&&CadLiveState.promptText(snapshot).includes('must never be rebound'),'Request context carries qualified note text and explicit anchor lifetime');
 snapshot.annotations[0].text='outside mutation';check(m.state.value.annotations[0].text!==snapshot.annotations[0].text,'Request annotations are immutable snapshots');
 await m.state.annotation('update',{annotation_id:note.id,text:'Edited review'});assert.deepEqual(m.state.value.annotations[0].anchor,note.anchor);checks++;
 m.seed();await m.state.pollOnce();const section=m.state.value.section;
 await m.state.setAppearance(reviewColor());const keep=structuredClone(m.state.value.presentation);keep.clip.keep='positive';await m.state.setPresentation(keep);
 check(m.state.value.annotations[0].evaluation_id===note.evaluation_id&&m.state.value.section.job_id===section.job_id,'Colors and kept side retain original inspection identity and exact section');
 await m.state.preset('save','Notes');await m.state.preset('apply','Notes');check(m.state.value.annotations.length===1,'Preset apply preserves native review notes');
 const reopened=new CadLiveState(m.bridge);reopened.attach('main');await reopened.pollOnce();check(reopened.value.annotations[0].text==='Edited review','Opening restores saved current review notes');reopened.dispose();
 m.advance();await m.state.pollOnce();const retired=m.state.value.annotations[0];check(retired.status==='retired'&&retired.evaluation_id===note.evaluation_id&&retired.anchor.part_id==='right','New revision keeps historical text and retires its original anchor');
 await m.state.annotation('update',{annotation_id:note.id,text:'Historical review'});check(m.state.value.annotations[0].status==='retired','Editing historical text cannot reactivate its anchor');
 await m.state.annotation('add',{anchor:{kind:'model'},text:'New revision note'});check(m.state.value.annotations[1].status==='current'&&m.state.value.annotations[1].revision===2,'New notes use current native source evaluation');
 await m.state.annotation('delete',{annotation_id:note.id});check(m.state.value.annotations.length===1,'Native delete frees an annotation slot');await m.state.annotation('clear');check(m.state.value.annotations.length===0,'Clear removes bounded review metadata');
 for(const args of [{anchor:{kind:'model'},text:''},{anchor:{kind:'model'},text:'é'.repeat(257)},{anchor:{kind:'model'},text:'bad\u0001'},{anchor:{kind:'model'},text:'x',script:'unsafe'}]){await assert.rejects(m.state.annotation('add',args));checks++;}
 m.state.dispose();
}
{
 const m=annotationMock();await m.state.pollOnce();const tool=m.bridge.tool;let release;
 m.bridge.tool=async(name,args)=>{const response=await tool(name,args);if(args.action==='annotation')return new Promise(resolve=>{release=()=>resolve(response);});return response;};
 const adding=m.state.annotation('add',{anchor:{kind:'model'},text:'Late note'});await tick();m.advance();await m.state.pollOnce();release();await adding;
 check(m.state.value.payload.revision===2&&m.state.value.annotations[0].status==='retired','Delayed accepted note cannot reintroduce a current pin after revision refresh');m.state.dispose();
}
{
 const m=annotationMock();await m.state.pollOnce();const tool=m.bridge.tool;let release;
 m.bridge.tool=async(name,args)=>{const response=await tool(name,args);if(args.action==='sync')return new Promise(resolve=>{release=()=>resolve(response);});return response;};
 const polling=m.state.pollOnce();await tick();await m.state.annotation('add',{anchor:{kind:'model'},text:'Concurrent note'});release();await polling;
 check(m.state.value.annotations.length===1,'Earlier empty sync cannot erase a newly accepted note');m.state.dispose();
}
{
 const m=annotationMock();await m.state.pollOnce();const tool=m.bridge.tool;let calls=0;
 m.bridge.tool=async(name,args)=>{const response=await tool(name,args);if(args.action==='annotation'){calls++;throw Error('Acknowledgment lost');}return response;};
 await assert.rejects(m.state.annotation('add',{anchor:{kind:'model'},text:'Accepted but ACK lost'}),/lost/);checks++;
 check(m.state.value.annotation_error.includes('lost')&&!m.state.value.annotations.length,'Uncertain admission stays explicit before read reconciliation');
 await m.state.pollOnce();check(m.state.value.annotations.length===1&&!m.state.value.annotation_error&&calls===1,'Read sync reconciles accepted lost acknowledgment without retrying mutation');m.state.dispose();
}
{
 const m=annotationMock();await m.state.pollOnce();const tool=m.bridge.tool;
 m.bridge.tool=(name,args)=>args.action==='annotation'?Promise.reject(Error('No admission')):tool(name,args);
 await assert.rejects(m.state.annotation('add',{anchor:{kind:'model'},text:'Not saved'}));checks++;await m.state.pollOnce();await m.state.pollOnce();check(m.state.value.annotation_error==='No admission','Empty sync retains a failed note admission until explicit retry');m.state.dispose();
}
{
 const m=annotationMock();await m.state.pollOnce();const tool=m.bridge.tool;let release;
 m.bridge.tool=async(name,args)=>{const response=await tool(name,args);if(args.action==='annotation')return new Promise(resolve=>{release=()=>resolve(response);});return response;};
 const adding=m.state.annotation('add',{anchor:{kind:'model'},text:'Old document'});await tick();m.retarget();await m.state.pollOnce();release();await adding;
 check(m.state.value.payload.document_id==='other'&&!m.state.value.annotations.length,'Retarget rejects delayed note response and removes unrelated history');m.state.dispose();
}
{
 const m=annotationMock();await m.state.pollOnce();await m.state.deliveryQueue;let release;m.bridge.request=()=>new Promise(resolve=>{release=resolve;});
 await m.state.annotation('add',{anchor:{kind:'model'},text:'Host ACK is optional'});await tick();check(!m.state.value.annotating&&m.state.value.annotations.length===1,'Optional host context acknowledgment cannot block native note admission');release({});await m.state.deliveryQueue;m.state.dispose();
}
{
 const m=annotationMock();await m.state.pollOnce();const tool=m.bridge.tool;
 m.bridge.tool=async(name,args)=>{const response=await tool(name,args);if(args.action==='annotation')response.feature_id='wrong';return response;};
 await assert.rejects(m.state.annotation('add',{anchor:{kind:'model'},text:'Wrong response'}),/another source/);checks++;check(!m.state.value.annotations.length,'Mismatched action source headers cannot expose review pins');m.state.dispose();
}
{
 const m=annotationMock();await m.state.pollOnce();const tool=m.bridge.tool;let release;
 m.bridge.tool=async(name,args)=>{const response=await tool(name,args);if(args.action==='annotation')return new Promise(resolve=>{release=()=>resolve(response);});return response;};
 const adding=m.state.annotation('add',{anchor:{kind:'model'},text:'View changed while admitted'});await tick();m.state.setCamera({...reviewCamera(),yaw:.8});release();await adding;
 check(!m.state.value.annotations.length,'Late response cannot qualify a request snapshot after camera settings changed');await m.state.pollOnce();check(m.state.value.annotations.length===1,'Read reconciliation retains the independently saved note after a view-only change');m.state.dispose();
}
{
 const m=annotationMock();await m.state.pollOnce();await m.state.annotation('add',{anchor:{kind:'model'},text:'Preview source lifetime'});const original=structuredClone(m.state.value.annotations[0]);await m.state.previewPose('extended');
 check(m.state.value.payload.draft&&m.state.value.annotations[0].status==='retired'&&m.state.value.annotations[0].evaluation_id===original.evaluation_id,'Motion preview retires committed inspection pins without deleting review text');
 await assert.rejects(m.state.annotation('add',{anchor:{kind:'model'},text:'Draft pin'}),/Save or reset/);checks++;m.state.dispose();
}
{
 const m=annotationMock();await m.state.pollOnce();const count=()=>m.calls.filter(call=>call.args.action==='annotation').length,before=count();
 m.state.value.payload.read_only=true;
 for(const operation of ['list','add','update','delete','clear']){await assert.rejects(m.state.annotation(operation),/External artifact sessions/);checks++;}
 check(count()===before,'Read-only external payload cannot reach native review-note mutation or read actions');delete m.state.value.payload.read_only;m.state.value.read_only=true;
 await assert.rejects(m.state.annotation('list'),/External artifact sessions/);checks++;check(count()===before,'Read-only sync discriminator blocks notes without inventing a native source identity');m.state.dispose();
}
console.log(`${checks} live UI bridge/state checks passed`);
