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
  check(context.selection.evaluation_id === evaluation.evaluation_id && context.selection.revision === 1 && !messages.some(m => m.method === 'ui/message'), 'main chat retrieves a qualified pick without a viewer message');
  const capabilities = bridge.capabilities;
  bridge.capabilities = {};
  state.update({ selection: null }); await state.publishContext();
  check((await tool('cad_context', { view_id: 'test_view' })).selection === null, 'clearing a pick persists without messaging or optional host context support');
  state.update({ selection: { reference, geometry: evaluation.topology.edges.find(e => e.id === picked) } }); await state.publishContext();
  check((await tool('cad_context', { view_id: 'test_view' })).selection.entity_id === picked && !messages.some(m => m.method === 'ui/message'), 'native selection handoff works with no host messaging capabilities');
  bridge.capabilities = capabilities;
  await state.sendPrompt('Round this edge to 1 mm.');
  const message = messages.find(m => m.method === 'ui/message');
  check(message.params.content[0].text.includes(evaluation.evaluation_id), 'Quick Edit carries exact evaluation');
  const resolved = await tool('cad_resolve_selection', reference);
  check(resolved.selector?.expected_count === 1, 'pick resolves to unique persistent design selector');
  await state.annotation('add',{anchor:{kind:'entity',reference},text:'Inspect selected native edge'});
  const annotation=structuredClone(state.value.annotations[0]);
  check(annotation.anchor.point_mm.every((v,i)=>v===resolved.geometry.center_mm[i]),'Actual bridge/controller receives native resolved inspection center');
  check(annotation.anchor.reference.entity_id===picked&&annotation.anchor_lifetime==='evaluation','Actual annotation retains source-qualified inspection evidence');
  await state.sendPrompt('Review saved inspection note.');
  check(messages.filter(m=>m.method==='ui/message').at(-1).params.content[0].text.includes(annotation.text),'Agent request includes saved review text and native source identity');
  await tool('cad_apply', { document_id: 'plate', expected_revision: 1, operations: [
    { op: 'add_feature', feature: { id: 'rounded', type: 'fillet', input: 'base', radius: 1, edges: resolved.selector } },
    { op: 'set_output', feature_id: 'rounded' }
  ] });
  check((await tool('cad_context', { view_id: 'test_view' })).stale, 'old context marked stale immediately after commit');
  await untilReady(2);
  check(state.value.annotations[0].status==='retired'&&state.value.annotations[0].evaluation_id===annotation.evaluation_id,'Actual commit retires notes without rebinding inspection centers');
  await state.annotation('update',{annotation_id:annotation.id,text:'Keep historical inspection'});
  check(state.value.annotations[0].status==='retired','Actual historical text edit cannot activate old source anchor');
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
  const mechanism = JSON.parse(readFileSync(`${root}/examples/articulated-arm.create.json`, 'utf8'));
  await tool('cad_create', mechanism);
  state.invalidate(); await tool('cad_show', { view_id: 'test_view', document_id: mechanism.document_id }); await untilReady(1);
  const savedGeometry = structuredClone(state.value.payload.mesh), source = await tool('cad_read', { document_id: mechanism.document_id });
  state.value.camera = camera; await state.setHiddenParts(['spindle']); await state.publishContext();
  const originalSnapshot = state.snapshot();
  await state.previewPose('extended'); await untilReady(1); await state.publishContext();
  check(state.value.payload.draft && state.value.payload.mesh.positions.some((p, i) => p.some((v, axis) => v !== savedGeometry.positions[i]?.[axis])), 'Native controller loads a different posed mesh without committing');
  assert.deepEqual(await tool('cad_read', { document_id: mechanism.document_id }), source); checks++;
  await assert.rejects(state.saveContext(originalSnapshot), /changed/); checks++;
  const draftContext = await tool('cad_context', { view_id: 'test_view' });
  check(draftContext.draft && !draftContext.stale && draftContext.selection === null && draftContext.preview_operations[0].pose_id === 'extended', 'Agent context identifies the exact draft and pose operation');
  check(CadLiveState.promptText(state.snapshot()).includes('unsaved motion preview'), 'Prompt explains draft semantics');
  assert.deepEqual(state.value.camera, camera); checks++;
  assert.deepEqual(state.value.hidden_part_ids, ['spindle']); checks++;
  const reset = state.resetMotion(); await assert.rejects(state.previewPose('folded'), /finish loading/); checks++;
  await reset; await untilReady(1);
  assert.deepEqual(state.value.payload.mesh, savedGeometry); checks++;
  check(!state.value.payload.draft, 'Reset returns to committed geometry');
  const values = [{ mate_id: 'hinge', coordinate: 'angle_deg', value: 70 }, { mate_id: 'spindle_joint', coordinate: 'travel_mm', value: 12 }];
  await state.previewValues(values); await untilReady(1);
  await state.saveMotion('review'); await untilReady(2); await state.publishContext();
  check(!state.value.payload.draft && state.value.payload.summary.assembly.motion.poses.includes('review'), 'Explicit save commits current joints and a named pose');
  const dofs = state.value.payload.summary.assembly.motion.dofs;
  check(dofs.find(d => d.mate_id === 'rail').value === 7 && dofs.find(d => d.mate_id === 'spindle_joint' && d.coordinate === 'angle_deg').value === -35, 'Saved geometry respects both couplings');
  const committedBeforePlayback=await tool('cad_read',{document_id:mechanism.document_id}),committedMesh=structuredClone(state.value.payload.mesh);
  await state.annotation('add',{anchor:{kind:'model'},text:'Review coordinated motion from the saved pose'});
  const motionNote=structuredClone(state.value.annotations[0]);
  const first=state.captureSequenceFrame(0),last=structuredClone(first);last.time_s=2;last.presentation.explode.distance_mm=10;
  last.joints[0].values.find(v=>v.mate_id==='hinge').value=90;last.joints[0].values.find(v=>v.mate_id==='spindle_joint').value=18;
  await state.sequence('save',{sequence:{name:'Coordinated playback',frames:[first,last]}});
  check(state.value.sequences[0].source.revision===2&&state.value.sequences[0].source.evaluation_id===state.value.payload.evaluation_id,'Real controller saves native source-qualified keyframes');
  await state.sequence('options',{name:'Coordinated playback',speed:1.5,loop:true});
  check(CadLiveState.promptText(state.snapshot()).includes(motionNote.text)&&CadLiveState.promptText(state.snapshot()).includes('Coordinated playback'),'Combined agent snapshot retains note evidence and declarative timeline identity');
  check(state.value.playback.state==='unapplied'&&!state.value.playing,'Selecting native playback options does not start or claim a displayed sample');
  await state.sequence('seek',{name:'Coordinated playback',time_s:1});await untilReady(2);await state.contextQueue;
  const midway=state.value.payload.summary.assembly.motion.dofs;
  check(midway.find(v=>v.mate_id==='hinge').value===80&&midway.find(v=>v.mate_id==='spindle_joint'&&v.coordinate==='travel_mm').value===15,'Real native controller coordinates both independent interpolated joints');
  check(midway.find(v=>v.mate_id==='rail').value===8&&midway.find(v=>v.mate_id==='spindle_joint'&&v.coordinate==='angle_deg').value===-40,'Native interpolated geometry honors both declarative couplings');
  check(state.value.payload.mesh.positions.some((p,i)=>p.some((v,axis)=>v!==committedMesh.positions[i]?.[axis])),'Playback loads actual changed native solid tessellation');
  check(state.value.presentation.explode.distance_mm===5&&state.value.playback.time_s===1&&state.value.playback.state==='displayed','Native sync atomically identifies displayed pose, explosion and paused timeline time');
  check(state.value.annotations[0].status==='retired'&&state.value.annotations[0].evaluation_id===motionNote.evaluation_id,'Real joint playback retires the original note anchor without rebinding it to moving geometry');
  check(state.snapshot().playback.source.model_sha256===state.value.sequences[0].source.model_sha256&&state.snapshot().selection===null,'Agent snapshot contains playback source and no committed draft pick');
  assert.deepEqual(await tool('cad_read',{document_id:mechanism.document_id}),committedBeforePlayback);checks++;
  const playbackReopened=new CadLiveState(bridge);playbackReopened.attach('test_view');await untilReady(2,playbackReopened);check(playbackReopened.value.playback.time_s===1&&playbackReopened.value.playback.speed===1.5&&playbackReopened.value.playback.loop&&!playbackReopened.value.playing,'Reopened real controller restores paused position and options');playbackReopened.dispose();
  const nativeBridgeTool=bridge.tool.bind(bridge);let lostSeekCalls=0;
  bridge.tool=async(name,args)=>{const result=await nativeBridgeTool(name,args);if(args.action==='sequence'&&args.operation==='seek'){lostSeekCalls++;throw Error('Seek acknowledgement was lost');}return result;};
  await assert.rejects(state.sequence('seek',{name:'Coordinated playback',time_s:1.5}),/acknowledgement/);checks++;bridge.tool=nativeBridgeTool;await untilReady(2);
  check(lostSeekCalls===1&&state.value.playback.time_s===1.5&&!state.value.playing,'Actual admitted seek reconciles lost ACK without replay');
  await tool('cad_apply', { document_id: mechanism.document_id, expected_revision: 2, operations: [{ op: 'set_joint_value', assembly_id: 'mechanism', mate_id: 'hinge', coordinate: 'angle_deg', value: 50 }] });
  await untilReady(3);
  check(!state.value.payload.draft && state.value.payload.summary.assembly.motion.dofs[0].value === 50, 'External revision supersedes draft through the real controller');
  check(state.value.playback===null&&state.value.sequences[0].source.revision===2,'External revision retires playback and retains its historical definition');
  await assert.rejects(state.sequence('seek',{name:'Coordinated playback',time_s:1}),error=>error.code==='stale_selection');checks++;
  // Extended appearance/preset controller smoke against the actual native process.
  const reviewHead=readFileSync(join(workspace,'documents',mechanism.document_id,'HEAD.json'),'utf8');
  const colors={default_color:[.2,.5,.7],parts:[{part_id:'spindle',color:[1,.1,.05]}]};
  await state.setAppearance(colors);state.setCamera({yaw:1,pitch:.4,zoom:1.7,pan:[.02,-.01]});
  await state.preset('save','Assembled overview');
  check((await tool('cad_context',{view_id:'test_view'})).presets.length===1,'Native/controller preset save captures current persisted settings');
  const bounds=state.value.payload.summary.bounds_mm,plane={normal:[0,0,1],offset_mm:(bounds.min[2]+bounds.max[2])/2};
  await state.setPresentation({clip:{...plane,keep:'negative'},explode:{distance_mm:0,directions:[]}});
  await state.section({action:'section',plane,explode:{distance_mm:0,directions:[]}});
  const sectionDeadline=Date.now()+45000;
  while(Date.now()<sectionDeadline&&state.value.section_status?.state!=='succeeded'){
    await state.pollOnce();
    if(['failed','cancelled','interrupted'].includes(state.value.section_status?.state))throw Error(state.value.section_status.error?.message||'Section stopped');
    await new Promise(resolve=>setTimeout(resolve,25));
  }
  check(state.value.section_status?.state==='succeeded','Actual native cap report is qualified through the current controller');
  const report=state.value.section_status.result;
  await state.preset('save','Section overview');
  await state.setAppearance({default_color:[.8,.4,.2],parts:[]});state.setCamera({yaw:2,pitch:.5,zoom:2.5,pan:[0,0]});await state.showAll();
  await state.preset('apply','Section overview');
  check(state.value.section_status.result===report,'Actual native same-plane preset apply retains the cached qualified cap report');
  assert.deepEqual(state.value.appearance,colors);checks++;
  check(Math.abs(state.value.camera.zoom-1.7)<1e-12,'Actual preset apply restores the native saved camera');
  const fresh=new CadLiveState(bridge);fresh.attach('test_view');
  try{await untilReady(3,fresh);check(fresh.value.presets.length===2&&fresh.value.appearance.parts[0].part_id==='spindle','Fresh controller restores persisted native presets and colors');}
  finally{fresh.dispose();}
  await state.preset('apply','Assembled overview');
  check(!state.value.section&&!state.value.section_status?.result&&!state.value.presentation.clip,'Actual native different-plane preset retires current cap metadata');
  check(readFileSync(join(workspace,'documents',mechanism.document_id,'HEAD.json'),'utf8')===reviewHead,'Colors, native presets, sections and review changes preserve raw source HEAD');
  await state.previewPose('folded');await untilReady(3);
  await state.preset('list');check(state.value.presets.length===2,'Actual native preset listing accepts the qualified current draft');
  await state.preset('delete','Section overview');check(state.value.presets.length===1,'Actual native preset deletion works during a qualified draft');
  await assert.rejects(state.preset('save','Draft'),/pose/);checks++;
  await state.resetMotion();await untilReady(3);
  state.invalidate();await tool('cad_show',{view_id:'test_view',document_id:'plate'});await untilReady(2);
  check(!state.value.presets.length&&!state.value.appearance.parts.length&&state.value.appearance.default_color[0]===.66,'Actual native retarget clears presets and restores default appearance');

  console.log(`${checks} real MCP live-loop checks passed`);
} finally {
  state.dispose(); bridge.dispose(); child.stdin.end();
  if (child.exitCode === null) await new Promise(resolve => { child.once('exit', resolve); setTimeout(() => { if (child.exitCode === null) child.kill(); resolve(); }, 3000).unref(); });
  lines.close(); rmSync(workspace, { recursive: true, force: true });
}
