// Tests the actual bundled bridge/controller without a browser or network shim.
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import vm from 'node:vm';
import { fileURLToPath } from 'node:url';
const root = fileURLToPath(new URL('../', import.meta.url));
for (const file of ['bridge.js', 'state.js']) vm.runInThisContext(readFileSync(`${root}/web/${file}`, 'utf8'), { filename: file });
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
function visibilityMock(initial = []) {
  const m = mock(), tool = m.bridge.tool; let hidden = initial, ids = ['base', 'cover'];
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
console.log(`${checks} live UI bridge/state checks passed`);
