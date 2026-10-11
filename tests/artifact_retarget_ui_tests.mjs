// Combined artifact/native controller and renderer regression probes; no browser claim.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import vm from 'node:vm';
import {fileURLToPath} from 'node:url';
const root=process.argv[2]||fileURLToPath(new URL('../',import.meta.url));
let next=1,now=0,checks=0;const timers=new Map();
const environment={console,TextEncoder,performance:{now:()=>now},setTimeout:fn=>{const id=next++;timers.set(id,fn);return id;},clearTimeout:id=>timers.delete(id)};
for(const name of ['renderer.js','state.js'])vm.runInNewContext(fs.readFileSync(root+'/web/'+name,'utf8'),environment,{filename:name});
const {CadLiveState:State,CadRenderer:Renderer,CadCapture:Capture}=environment,copy=x=>JSON.parse(JSON.stringify(x)),check=(x,label)=>{assert.ok(x,label);checks++;};
const p=()=>Renderer.math.defaultPresentation(),a=()=>Renderer.math.defaultAppearance();
const native={schema_version:1,document_id:'Native',revision:1,evaluation_id:'eval_native',feature_id:'Part',draft:false,
 summary:{bounds_mm:{min:[-1,-1,-1],max:[1,1,1]}},
 mesh:{schema_version:1,feature_id:'Part',selection_lifetime:'evaluation',positions:[[-1,-1,0],[1,-1,0],[-1,1,0]],triangles:[[0,1,2]],triangle_faces:['face-1'],edges:[]},
 topology:{schema_version:1,feature_id:'Part',selection_lifetime:'evaluation',faces:[{id:'face-1'}],edges:[]}};
const source={document_id:'Native',revision:1,evaluation_id:'eval_native',feature_id:'Part',model_sha256:'a'.repeat(64)};
const sequence={name:'Review',source,frames:[{time_s:0,presentation:p(),joints:[]},{time_s:2,presentation:p(),joints:[]}]};
const playback={name:'Review',source,time_s:.5,speed:2,loop:true,state:'displayed'};
const note={id:'ann_1',text:'Native inspection',document_id:'Native',revision:1,evaluation_id:'eval_native',feature_id:'Part',anchor_lifetime:'evaluation',coordinate_space:'committed_source_pose',status:'current',anchor:{kind:'model',part_id:null,point_mm:[0,0,0],position_semantics:'bounds_center'}};
function artifact(hash='b'.repeat(64)){
 const summary={editable:false,units:'mm',representation:'triangle_mesh',bounds:{min:[-1,-1,0],max:[1,1,0]},vertices:3,triangles:1,groups:1,curves:0};
 return{schema_version:1,kind:'artifact',document_id:null,revision:null,feature_id:null,evaluation_id:'artifact_'+hash.slice(0,55),draft:false,read_only:true,
 artifact:{review_sha256:hash,source:{format:'stl',sha256:'c'.repeat(64),declared_units:'mm',references:[],native_source_association:null},summary},summary,
 artifact_geometry:{positions:[[-1,-1,0],[1,-1,0],[-1,1,0]],triangles:[[0,1,2]],triangle_groups:['artifact-1'],groups:[{id:'artifact-1',editable:false,source_ref:'triangle'}],polylines:[]},metadata:{},limitations:['No editable native history.'],selection_lifetime:'review_sha256'};
}
const turn=()=>new Promise(resolve=>setImmediate(resolve));
function mock(){
 let displayed=native,gate=null,target=null;const calls=[],messages=[];
 const context=()=>({view_id:'review',document_id:displayed.document_id,revision:displayed.revision,feature_id:displayed.feature_id,evaluation_id:displayed.evaluation_id,head_revision:displayed.revision,stale:false,selection:null,presentation:p(),appearance:a(),hidden_part_ids:[],presets:[],annotations:displayed.read_only?[]:[note],sequences:displayed.read_only?[]:[sequence],playback:displayed.read_only?null:playback,...(displayed.read_only?{read_only:true,artifact:displayed.artifact,artifact_pose:displayed.artifact_pose||{}}:{} )});
 const sync=()=>({...context(),state:'ready',draft:false,model:displayed.read_only?null:{features:[{id:'Part',type:'box'}],parameters:{}}});
 const bridge={capabilities:{updateModelContext:{}},async request(method,args){messages.push({method,args:copy(args)});return{};},async tool(name,args){
  calls.push({name,args:copy(args)});
  if(name==='cad_context')return copy(context());
  if(args.action==='sync')return copy(sync());
  if(args.action==='mesh'){const data=JSON.stringify(displayed);return{data,offset:0,next_offset:null,total_bytes:data.length};}
  if(args.action==='artifact_pose'){assert.equal(args.evaluation_id,displayed.evaluation_id);displayed={...displayed,evaluation_id:'apose_'+String(next++).padStart(58,'0'),artifact_pose:copy(args.configuration)};const response=copy(sync());if(gate&&target===args.action)await gate;return response;}
  if(['context','annotation','sequence'].includes(args.action)){const response=copy(context());if(gate&&args.action===target)await gate;return response;}
  throw Error('Unexpected tool request '+JSON.stringify(args));
 }};
 const state=new State(bridge);state.attach('review');
 return{state,calls,messages,external(hash,animated=false){displayed=artifact(hash);if(animated){displayed.artifact.source.format='glb';displayed.metadata={animations:[{name:'Move',duration_seconds:2}]};}},native(){displayed=native;},block(action){target=action;let release;gate=new Promise(resolve=>release=()=>{gate=null;resolve();});return release;},dispose(){state.dispose();timers.clear();}};
}
{
 const m=mock();await m.state.pollOnce();m.state.setMeasurementPoint('a',[0,0,0]);m.state.setMeasurementPoint('b',[3,4,12]);m.external();await m.state.pollOnce();
 check(!m.state.value.point_measurement.a&&!m.state.value.point_measurement.b,'Retargeting native points to an artifact clears their coordinates');
 const count=m.calls.length;m.state.beginPointMeasurement();m.state.acceptMeasurementPoint([0,0,0]);m.state.setMeasurementPoint('b',[3,4,12]);
 check(Renderer.math.pointDistance(m.state.value.point_measurement.a.point_mm,m.state.value.point_measurement.b.point_mm).distance_mm===13&&m.calls.length===count,'External review supports local point distance without native tool calls');
 m.external('d'.repeat(64));await m.state.pollOnce();check(!m.state.value.point_measurement.a,'A changed artifact review hash retires defined points');m.dispose();
}
{
 const m=mock();await m.state.pollOnce();await m.state.deliveryQueue;await m.state.playSequence('Review');
 check(m.state.value.playing&&timers.size===1&&m.state.value.annotations.length===1,'Native clock and qualified inspection note exist before retarget');
 m.external();await m.state.pollOnce();
 check(m.state.value.read_only&&m.state.value.model===null&&m.state.value.document_id===null,'Artifact retarget clears editable native model/identity');
 check(!m.state.value.playing&&timers.size===0&&m.state.value.playback===null&&!m.state.value.sequences.length,'Artifact adoption immediately clears clock and native sequence state');
 check(!m.state.value.annotations.length&&!m.state.snapshot().annotations.length,'Artifact snapshots contain no notes from prior native source');
 await m.state.deliveryQueue;check(m.messages.at(-1).args.structuredContent.read_only===true&&!m.messages.at(-1).args.structuredContent.playback,'Host context reconciles to external review with no native time claim');
 const count=m.calls.length;
 for(const action of [()=>m.state.section(null),()=>m.state.measure(null),()=>m.state.motion('motion_reset'),()=>m.state.preset('list'),()=>m.state.annotation('list'),()=>m.state.sequence('list'),()=>m.state.playSequence('Review')]){await assert.rejects(action);checks++;}
 assert.throws(()=>m.state.captureSequenceFrame(0));checks++;
 check(m.calls.length===count,'External exact operations, notes, and sequences are rejected before bridge admission');m.dispose();
}
for(const operation of ['annotation','sequence','context']){
 const m=mock();await m.state.pollOnce();await m.state.deliveryQueue;const release=m.block(operation);
 const pending=operation==='annotation'?m.state.annotation('list'):operation==='sequence'?m.state.sequence('options',{name:'Review',speed:2,loop:true}):m.state.saveContext(m.state.snapshot());
 const settled=pending.then(()=>null,error=>error);await turn();m.external();await m.state.pollOnce();release();await settled;await m.state.deliveryQueue;
 check(m.state.value.payload.read_only&&!m.state.value.annotations.length&&!m.state.value.playback&&!m.state.value.sequences.length,'Delayed native '+operation+' response cannot restore old native state');
 check(m.calls.filter(x=>x.args.action===operation).length<=(operation==='context'?4:1),'Retargeted '+operation+' is never replayed');m.dispose();
}
{
 const m=mock();await m.state.pollOnce();await m.state.deliveryQueue;const release=m.block('sequence'),pending=m.state.playSequence('Review');await turn();
 check(m.state.value.playing&&m.calls.filter(x=>x.args.operation==='seek').length===1,'A native clock seek is admitted before external retarget');m.external();await m.state.pollOnce();release();await pending;await m.state.deliveryQueue;
 check(!m.state.value.playing&&timers.size===0&&!m.state.value.playback&&!m.state.value.annotations.length&&m.state.value.payload.read_only,'Late admitted clock seek cannot restart playback or recover old source metadata');
 check(m.calls.filter(x=>x.args.operation==='seek').length===1,'Admitted seek is not replayed after retarget');m.dispose();
}
{
 const m=mock();await m.state.pollOnce();m.external();await m.state.pollOnce();await m.state.deliveryQueue;
 const selected={reference:{review_sha256:'b'.repeat(64),kind:'mesh_group',entity_id:'artifact-1'},geometry:{id:'artifact-1',editable:false,source_ref:'triangle'}};
 m.state.update({selection:selected});const snapshot=m.state.snapshot();m.external('d'.repeat(64));await m.state.pollOnce();
 check(m.state.value.selection===null&&m.state.value.payload.artifact.review_sha256==='d'.repeat(64)&&!m.state.matches(snapshot),'Full review SHA retarget clears stale external pick and invalidates old context');
 m.native();await m.state.pollOnce();check(!m.state.value.read_only&&m.state.value.payload.document_id==='Native'&&!m.state.value.selection,'Retarget back to native restores native identity without artifact label');m.dispose();
}
{
 const m=mock();m.external(undefined,true);await m.state.pollOnce();m.state.setMeasurementPoint('a',[0,0,0]);const evaluation=m.state.value.payload.evaluation_id;await m.state.artifactPose({animation:0,time_seconds:.5});
 check(m.state.value.payload.evaluation_id!==evaluation&&m.state.value.artifact_pose.time_seconds===.5&&m.state.snapshot().artifact_pose.time_seconds===.5,'Imported seek adopts a fresh evaluation and explicitly qualified source time: '+JSON.stringify({evaluation:m.state.value.payload.evaluation_id,status:m.state.value.status,error:m.state.value.error,pose:m.state.value.artifact_pose,calls:m.calls.map(c=>c.args.action)}));
 check(!m.state.value.point_measurement.a&&!m.state.value.artifact_pose_busy,'Imported seek retires measured coordinates and finishes busy admission');
 m.state.playArtifact(0);check(m.state.value.artifact_playing&&timers.size===1,'Imported playback schedules a bounded asynchronous clock');const tick=[...timers.values()][0];timers.clear();now+=500;await tick();
 check(m.state.value.artifact_pose.time_seconds>=.5&&m.state.value.artifact_playing&&timers.size===1,'Clock displays newly evaluated geometry before scheduling its next seek');m.state.pauseArtifact();check(!m.state.value.artifact_playing&&timers.size===0,'Pause retires the imported clock');
 m.state.playArtifact(0);m.native();await m.state.pollOnce();check(!m.state.value.artifact_playing&&!m.state.value.read_only&&timers.size===0,'Native retarget retires imported playback');m.dispose();
}
{
 const m=mock();m.external(undefined,true);await m.state.pollOnce();const release=m.block('artifact_pose'),pending=m.state.artifactPose({animation:0,time_seconds:1});await turn();
 await assert.rejects(()=>m.state.artifactPose({animation:0,time_seconds:1.1}),/current review pose/);checks++;const admitted=m.calls.length;await m.state.pollOnce();check(m.calls.length===admitted,'Normal poll cannot race an admitted imported pose');
 m.native();m.state.attach('review');release();await pending;await m.state.pollOnce();check(!m.state.value.read_only&&!m.state.value.artifact_pose_busy&&!m.state.value.artifact_playing&&m.state.value.payload.document_id==='Native','Late imported pose cannot reattach a retired artifact or leave controls busy');m.dispose();
}
{
 const data=artifact(),model=Renderer.math.prepareArtifact(data),before=JSON.stringify(data),camera={yaw:0,pitch:Math.PI/2,zoom:1,pan:[0,0]},picks=[];
 const viewport={model,fullModel:model,camera,mode:'face',lost:false,gl:{},selection:null,sectionResult:null,_size:()=>({width:400,height:400}),_schedule(){},_checked:action=>action(),callbacks:{onPick:(...args)=>picks.push(args)},_error:error=>{throw error;}};
 const screen=Renderer.math.project([-.2,-.2,0],camera,400,400);Renderer.prototype._pick.call(viewport,screen[0],screen[1]);
 check(picks[0][0].reference.review_sha256===data.artifact.review_sha256&&picks[0][0].reference.kind==='mesh_group'&&picks[0][0].reference.entity_id==='artifact-1'&&Object.keys(picks[0][0].reference).length===3&&picks[0][1].artifact,'Production pick callback exposes only review-local labels, never native references');
 Renderer.prototype.setSelection.call(viewport,picks[0][0].reference);check(viewport.selection.entity_id==='artifact-1','Current artifact pick can be restored by its full review hash');
 for(const reference of [{...picks[0][0].reference,review_sha256:'d'.repeat(64)},{...picks[0][0].reference,entity_id:'artifact-2'},{...picks[0][0].reference,kind:'face'}, {...picks[0][0].reference,document_id:'Native'}]){assert.throws(()=>Renderer.prototype.setSelection.call(viewport,reference));checks++;}
 assert.throws(()=>Renderer.prototype.setSection.call(viewport,{}),/editable native/);checks++;
 assert.throws(()=>Renderer.math.annotations([note],model),/another evaluation/);checks++;
 check(JSON.stringify(data)===before,'Read-only picking/validation leaves the captured artifact unchanged');
 const shifted=copy(data);shifted.evaluation_id='apose_'+'a'.repeat(58);shifted.artifact_pose={joints:{}};shifted.artifact.source.format='urdf';shifted.artifact.summary.representation='robot_description';shifted.summary.representation='robot_description';shifted.artifact_geometry.positions=shifted.artifact_geometry.positions.map(p=>[p[0]+5,...p.slice(1)]);shifted.summary.bounds={min:[4,-1,0],max:[6,1,0]};
 const posedModel=Renderer.math.prepareArtifact(shifted);check(posedModel.center[0]===model.center[0]&&Math.abs(posedModel.positions[0]-model.positions[0]-2.5)<1e-9,'Pose normalization keeps the captured frame so whole-mesh translation remains visible');
 for(const pose of [{joints:{bad:NaN}},{animation:0},{ignored:true}]){const forged=copy(shifted);forged.artifact_pose=pose;assert.throws(()=>Renderer.math.prepareArtifact(forged));checks++;}
}
for(const change of ['selection','playing','artifact_playing']){
 const m=mock();m.external();await m.state.pollOnce();await m.state.contextQueue;let release,pixels=0;const captured=[];m.state.bridge.tool=async(_,args)=>{captured.push(copy(args));return{};};
 const r={whenTexturesReady:()=>new Promise(resolve=>release=resolve),capture(){pixels++;return'data:image/png;base64,AAAA';}},capture=new Capture(m.state.bridge,m.state,()=>r),pending=capture.capture({capture_id:'capture_wait',source:{view_id:'review',evaluation_id:m.state.value.payload.evaluation_id},max_bytes:100});await turn();
 m.state.update(change==='selection'?{selection:{reference:{review_sha256:m.state.value.artifact.review_sha256,kind:'mesh_group',entity_id:'artifact-1'}}}:{[change]:true});release();await pending;
 check(pixels===0&&captured.length===1&&captured[0].error?.includes('changed before capture'),'Capture rejects '+change+' changes while settling texture decode');m.dispose();
}
{
 const m=mock();m.external();await m.state.pollOnce();await m.state.contextQueue;const captured=[];m.state.bridge.tool=async(_,args)=>{captured.push(copy(args));if(args.data)m.state.update({selection:{reference:{review_sha256:m.state.value.artifact.review_sha256,kind:'mesh_group',entity_id:'artifact-1'}}});return{};};
 const capture=new Capture(m.state.bridge,m.state,()=>({capture:()=> 'data:image/png;base64,'+'A'.repeat(128*1024+4)}));await capture.capture({capture_id:'capture_chunks',source:{view_id:'review',evaluation_id:m.state.value.payload.evaluation_id},max_bytes:200000});
 check(captured.length===2&&captured[0].final===false&&captured[1].error?.includes('changed during capture'),'Capture rejects local selection changes between chunks before final publication');m.dispose();
}
for(const change of [false,true]){
 const m=mock();m.external();await m.state.pollOnce();await m.state.contextQueue;const captured=[];m.state.bridge.tool=async(_,args)=>{captured.push(copy(args));if(captured.length===1)throw Object.assign(Error('Writer owns document'),{code:'workspace_busy'});return{};};
 const capture=new Capture(m.state.bridge,m.state,()=>({capture:()=> 'data:image/png;base64,AAAA'})),pending=capture.capture({capture_id:'capture_retry',source:{view_id:'review',evaluation_id:m.state.value.payload.evaluation_id},max_bytes:100});await turn();
 check(captured.length===1&&timers.size===1,'Explicit capture lock rejection schedules one paced retry');if(change)m.state.update({selection:{reference:{review_sha256:m.state.value.artifact.review_sha256,kind:'mesh_group',entity_id:'artifact-1'}}});const resume=[...timers.values()][0];timers.clear();resume();await pending;
 check(captured.length===2&&(change?captured[1].error?.includes('changed during capture'):captured[1].data==='AAAA'&&captured[1].offset===0&&captured[1].final===true),'Capture '+(change?'rejects selection drift before retry':'retries rejected bytes at unchanged offset without duplicate publication'));m.dispose();
}
{
 const m=mock();m.external();await m.state.pollOnce();await m.state.contextQueue;const captured=[];m.state.bridge.tool=async(_,args)=>{captured.push(copy(args));if(captured.length===1)throw Object.assign(Error('Writer owns document'),{code:'workspace_busy'});return{};};
 const capture=new Capture(m.state.bridge,m.state,()=>({capture(){throw Error('WebGL frame unavailable');}})),pending=capture.capture({capture_id:'capture_error_retry',source:{view_id:'review',evaluation_id:m.state.value.payload.evaluation_id},max_bytes:100});await turn();const resume=[...timers.values()][0];timers.clear();resume();await pending;
 check(captured.length===2&&captured.every(args=>args.error==='WebGL frame unavailable'),'Explicit lock rejection of capture failure notification is safely retried');m.dispose();
}
for(const change of [false,true]){
 const m=mock();m.external();await m.state.pollOnce();await m.state.contextQueue;let visible=false,pixels=0;const captured=[];m.state.bridge.tool=async(_,args)=>{captured.push(copy(args));if(captured.length===1)throw Object.assign(Error('Writer owns document'),{code:'workspace_busy'});return{};};
 const capture=new Capture(m.state.bridge,m.state,()=>({capture(){pixels++;return'data:image/png;base64,AAAA';}}),()=>visible),pending=capture.capture({capture_id:'capture_hidden_retry',source:{view_id:'review',evaluation_id:m.state.value.payload.evaluation_id},max_bytes:100});await turn();if(change)visible=true;const resume=[...timers.values()][0];timers.clear();resume();await pending;
 check(pixels===0&&(change?captured.length===1:captured.length===2&&captured.every(args=>args.error?.includes('hidden, unavailable'))),'Hidden error notification '+(change?'stops retry when visibility changes':'retries a pre-write lock rejection with the same unavailable source'));m.dispose();
}
{
 const m=mock();m.external();await m.state.pollOnce();await m.state.contextQueue;const captured=[];m.state.bridge.tool=async(_,args)=>{captured.push(copy(args));if(args.data)throw Error('Acknowledgment lost');return{};};
 const capture=new Capture(m.state.bridge,m.state,()=>({capture:()=> 'data:image/png;base64,AAAA'}));await capture.capture({capture_id:'capture_uncertain',source:{view_id:'review',evaluation_id:m.state.value.payload.evaluation_id},max_bytes:100});
 check(captured.length===2&&captured[1].error==='Acknowledgment lost'&&timers.size===0,'Uncertain chunk acknowledgment never replays captured bytes');m.dispose();
}
console.log('PASS '+checks+' independent combined artifact/native retarget and capture checks');
