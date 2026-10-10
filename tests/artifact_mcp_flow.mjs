// Real native MCP and actual shipped controller/renderer; no browser claim.
import assert from 'node:assert/strict';
import {spawn} from 'node:child_process';
import {createInterface} from 'node:readline';
import {mkdtempSync,realpathSync,readFileSync,writeFileSync,cpSync,rmSync,mkdirSync,readdirSync} from 'node:fs';
import {tmpdir} from 'node:os';
import {join,resolve,dirname} from 'node:path';
import {createHash} from 'node:crypto';
import vm from 'node:vm';
import {fileURLToPath} from 'node:url';
const root=fileURLToPath(new URL('../',import.meta.url)),exe=resolve(process.argv[2]),fixtures=resolve(process.argv[3]);
for(const file of ['bridge.js','renderer.js','state.js'])vm.runInThisContext(readFileSync(join(root,'web',file),'utf8'),{filename:file});
const temporary=realpathSync(mkdtempSync(join(tmpdir(),'cad-artifact-flow-'))),workspace=join(temporary,'workspace'),source=join(temporary,'source');mkdirSync(workspace);cpSync(fixtures,source,{recursive:true});
const hash=bytes=>createHash('sha256').update(bytes).digest('hex');
const child=spawn(exe,['serve','--workspace',workspace],{stdio:['pipe','pipe','pipe']}),waiting=new Map();let next=1,stderr='',checks=0;
const evidence={calls:[],discovery:null};const check=(condition,label)=>{assert.ok(condition,label);checks++;};
child.stderr.on('data',v=>stderr+=v);const lines=createInterface({input:child.stdout});lines.on('line',line=>{const m=JSON.parse(line),w=waiting.get(m.id);if(w){waiting.delete(m.id);clearTimeout(w.timer);m.error?w.reject(Error(m.error.message)):w.resolve(m.result);}});
child.on('exit',code=>{for(const w of waiting.values()){clearTimeout(w.timer);w.reject(Error(`Service exited ${code}: ${stderr}`));}waiting.clear();});
const rpc=(method,params={})=>new Promise((resolve,reject)=>{const id=next++,timer=setTimeout(()=>{waiting.delete(id);reject(Error(`Timeout ${method}: ${stderr}`));},45000);waiting.set(id,{resolve,reject,timer});child.stdin.write(JSON.stringify({jsonrpc:'2.0',id,method,params})+'\n');});
const tool=async(name,args={})=>{const r=CadBridge.value(await rpc('tools/call',{name,arguments:args}));evidence.calls.push({name,arguments:args,result:r});return r;};
const fails=async(code,fn)=>{await assert.rejects(fn,e=>e.code===code);checks++;};
let listener;const messages=[],parent={async postMessage(m){if(!Object.hasOwn(m,'id'))return;try{let result;if(m.method==='ui/initialize')result={protocolVersion:'2026-01-26',hostCapabilities:{message:{},updateModelContext:{}},hostContext:{}};else if(m.method==='tools/call')result=await rpc(m.method,m.params);else{messages.push(m);result={};}listener?.({source:parent,origin:'https://artifact.test.invalid',data:{jsonrpc:'2.0',id:m.id,result}});}catch(e){listener?.({source:parent,origin:'https://artifact.test.invalid',data:{jsonrpc:'2.0',id:m.id,error:{code:-32603,message:e.message}}});}}};
const bridge=new CadBridge({parent,addEventListener(_,fn){listener=fn;},removeEventListener(){listener=null;}}),controller=new CadLiveState(bridge);
const input=(name,format,units)=>{const path=join(source,name);return {action:'review',path,format,units,expected_sha256:hash(readFileSync(path))};};
async function job(args,id){let value=await tool('cad_job',{action:'submit',request_id:id,tool:'cad_artifact',arguments:args});for(let n=0;n<500&&['queued','running','cancelling'].includes(value.state);n++){await new Promise(r=>setTimeout(r,10));try{value=await tool('cad_job',{action:'get',job_id:value.job_id});}catch(e){if(e.code!=='workspace_busy')throw e;}}return value;}
async function ready(){for(let n=0;n<100;n++){await controller.pollOnce();if(controller.value.status==='ready')return;if(controller.value.status==='error')throw Error(controller.value.error);await new Promise(r=>setTimeout(r,20));}throw Error('Artifact viewer did not load');}
function forged(review,modify,label){const directory=join(temporary,label);cpSync(review.directory,directory,{recursive:true});const path=join(directory,'review.json'),value=JSON.parse(readFileSync(path));modify(value);const raw=JSON.stringify(value,null,2);writeFileSync(path,raw);const digest=hash(raw),manifest=JSON.parse(readFileSync(join(directory,'manifest.json')));manifest.review_sha256=digest;for(const item of manifest.artifacts)if(item.path==='review.json'){item.sha256=digest;item.bytes=Buffer.byteLength(raw);}writeFileSync(join(directory,'manifest.json'),JSON.stringify(manifest));return {action:'verify',review_path:path,expected_sha256:digest};}
try{
 await rpc('initialize',{protocolVersion:'2025-11-25',capabilities:{extensions:{'io.modelcontextprotocol/ui':{mimeTypes:['text/html;profile=mcp-app']}}},clientInfo:{name:'artifact-live-flow',version:'1'}});child.stdin.write(JSON.stringify({jsonrpc:'2.0',method:'notifications/initialized'})+'\n');
 evidence.discovery=(await rpc('tools/list')).tools;check(Buffer.byteLength(JSON.stringify(evidence.discovery))<=29*15*1024,'15 KiB per tool discovery byte gate for 29 tools');
 const resource=await rpc('resources/read',{uri:'ui://agent-3d-cad/viewer.html'});check(resource.contents[0].text.includes('artifact-review-panel')&&resource.contents[0].text.includes('validateArtifact'),'shipped native app contains artifact UI');
 const model={schema_version:1,units:'mm',parameters:{},features:[{id:'Part',type:'box',size:[10,20,30]}],output:'Part'};
 await tool('cad_create',{document_id:'Source',model});const native=await tool('cad_read',{document_id:'Source',revision:1});
 await tool('cad_apply',{document_id:'Source',expected_revision:1,operations:[{op:'replace_feature',id:'Part',feature:{id:'Part',type:'box',size:[11,20,30]}}]});
 let args=input('triangle.stl','stl','in');args.native_source={document_id:'Source',revision:1,feature_id:'Part'};
 await fails('invalid_argument',()=>tool('cad_artifact',{...input('triangle.stl','stl','mm'),units:'millimeter'}));
 const reviewed=await tool('cad_artifact',args);check(reviewed.source.native_source_association.qualification==='caller_declared'&&reviewed.source.native_source_association.source_record_sha256===hash(JSON.stringify(native)),'historical declared source binds actual record, not HEAD');
 await bridge.initialize();await tool('cad_artifact_show',{review_path:reviewed.path,expected_sha256:reviewed.sha256,view_id:'review'});controller.attach('review');await ready();
 check(controller.value.read_only&&controller.value.model===null&&controller.value.document_id===null,'artifact replaces native association with explicit null/read-only state');
 const prepared=CadRenderer.math.prepareArtifact(controller.value.payload),camera={yaw:0,pitch:Math.PI/2,zoom:1,pan:[0,0]};
 const point=[0,0,0];for(let k=0;k<3;k++)point[k]=(prepared.positions[k]+prepared.positions[3+k]+prepared.positions[6+k])/3;
 const screen=CadRenderer.math.project(point,camera,800,600),pick=CadRenderer.math.pick(prepared,camera,800,600,screen[0],screen[1],'face');check(pick.id==='artifact-1'&&!pick.ambiguous,'real imported mesh picks a review-local label');
 const selection={reference:{review_sha256:reviewed.sha256,kind:'mesh_group',entity_id:pick.id},geometry:prepared.geometries.face.get(pick.id)};controller.update({selection});await controller.saveContext(controller.snapshot('Inspect this triangle'));
 let context=await tool('cad_context',{view_id:'review'});check(context.selection.kind==='mesh_group'&&context.resolved_selection.geometry.editable===false&&!context.selection.document_id,'artifact context never emits native selector');
 check(CadLiveState.promptText(controller.snapshot()).includes('read-only external artifact')&&!CadLiveState.promptText(controller.snapshot()).includes('cad_resolve_selection'),'copy/chat prompt describes artifact provenance and limitations');
 check(context.annotations.length===0&&context.sequences.length===0&&context.playback===null,'external context carries no native notes or playback');
 for(const [action,extra]of [['section',{}],['measure',{}],['motion_reset',{}],['preset',{operation:'list'}],['annotation',{operation:'list'}],['sequence',{operation:'list'}]])await fails('read_only_artifact',()=>tool('cad_viewer',{action,view_id:'review',evaluation_id:controller.value.payload.evaluation_id,...extra}));
 await assert.rejects(()=>controller.section(null),/read-only/);checks++;await assert.rejects(()=>controller.measure(null),/read-only/);checks++;await assert.rejects(()=>controller.preset('list'),/read-only/);checks++;await assert.rejects(()=>controller.motion('motion_reset'),/read-only/);checks++;
 await assert.rejects(()=>controller.annotation('list'),/saved native|read only/);checks++;await assert.rejects(()=>controller.sequence('list'),/editable/);checks++;
 await fails('stale_selection',()=>tool('cad_viewer',{action:'context',view_id:'review',evaluation_id:controller.value.payload.evaluation_id,selection:{review_sha256:'0'.repeat(64),kind:'mesh_group',entity_id:'artifact-1'}}));
 for(const [name,format,units]of [['triangle-binary.stl','stl','cm'],['triangle.glb','glb','m'],['nested-deflate.3mf','3mf','file'],['drawing.dxf','dxf','mm'],['primitives.urdf','urdf','m'],['robot.sdf','sdf','m'],['robot.srdf','srdf','m']]){
  const result=await tool('cad_artifact',input(name,format,units));await tool('cad_artifact',{action:'verify',review_path:result.path,expected_sha256:result.sha256});await tool('cad_artifact_show',{review_path:result.path,expected_sha256:result.sha256,view_id:'review'});await ready();
  const p=controller.value.payload;check(p.artifact.source.format===format&&p.document_id===null&&!p.topology&&!p.mesh,'original '+format+' reviewed without native topology');CadRenderer.math.prepareArtifact(p);checks++;
  if(format==='srdf')check(p.summary.bounds===null&&p.artifact_geometry.triangles.length===0,'SRDF semantic review invents no geometry');
 }
 args=input('robot.urdf','urdf','m');args.references=[{uri:'triangle.stl',path:join(source,'triangle.stl'),expected_sha256:hash(readFileSync(join(source,'triangle.stl'))),units:'cm'}];const robot=await tool('cad_artifact',args);check(robot.source.references.length===1,'explicit referenced bytes captured and qualified');
 const step=await tool('cad_export',{document_id:'Source',revision:1,format:'step'});
 const stepReviewed=await tool('cad_artifact',{action:'review',path:step.path,format:'step',units:'file',expected_sha256:hash(readFileSync(step.path))});
 await tool('cad_artifact',{action:'verify',review_path:stepReviewed.path,expected_sha256:stepReviewed.sha256});await tool('cad_artifact_show',{review_path:stepReviewed.path,expected_sha256:stepReviewed.sha256,view_id:'review'});await ready();
 check(controller.value.payload.summary.representation==='exact_brep_import'&&controller.value.payload.metadata.exact_summary.volume_mm3===6000,'STEP exact work stays in bounded kernel worker and reaches read-only viewer');
 const done=await job(input('nested-stored.3mf','3mf','file'),'artifact_job');check(done.state==='succeeded'&&done.result.read_only&&!done.document_id,'durable artifact job succeeds without fake document_id');
 const bad=await job({...input('triangle.stl','stl','mm'),expected_sha256:'0'.repeat(64)},'artifact_bad');check(bad.state==='failed'&&bad.error.code==='artifact_mismatch','durable source mismatch remains explicit failure');
 // A large valid ASCII STL exercises parser checkpoints in the real worker.
 // Observe running before cancellation; a queued-only cancel is insufficient.
 const largePath=join(source,'large-valid.stl'),facet='facet normal 0 0 1\nouter loop\nvertex 0 0 0\nvertex 1 0 0\nvertex 0 1 0\nendloop\nendfacet\n';
 writeFileSync(largePath,'solid bounded\n'+facet.repeat(60000)+'endsolid bounded\n');
 const large=input('large-valid.stl','stl','mm'),headBefore=await tool('cad_read',{document_id:'Source'}),packageBefore=readFileSync(reviewed.path),publishedBefore=readdirSync(join(workspace,'artifact_reviews')).sort();
 const retryTool=async(args)=>{for(let n=0;n<200;n++){try{return await tool('cad_job',args);}catch(e){if(e.code!=='workspace_busy')throw e;await new Promise(r=>setTimeout(r,5));}}throw Error('Artifact job admission remained busy');};
 const terminal=async(id)=>{let value;for(let n=0;n<1000;n++){value=await retryTool({action:'get',job_id:id});if(!['queued','running','cancelling'].includes(value.state))return value;await new Promise(r=>setTimeout(r,5));}throw Error('Artifact job did not become terminal');};
 await retryTool({action:'submit',request_id:'artifact_cancel',tool:'cad_artifact',arguments:large});let active;
 for(let n=0;n<1000;n++){active=await retryTool({action:'get',job_id:'artifact_cancel'});if(active.state==='running')break;if(!['queued','running'].includes(active.state))throw Error('Valid artifact completed before observed-running cancellation');await new Promise(r=>setTimeout(r,2));}
 check(active.state==='running','large valid artifact job observed running');
 await retryTool({action:'cancel',job_id:'artifact_cancel'});const cancelled=await terminal('artifact_cancel');check(cancelled.state==='cancelled'&&cancelled.error.code==='job_cancelled','running artifact parser job cancels explicitly');
 const expiredRequest={action:'submit',request_id:'artifact_timeout',tool:'cad_artifact',arguments:large,budget:{timeout_ms:1,memory_mb:2048}};
 await retryTool(expiredRequest);const expired=await terminal('artifact_timeout');check(expired.state==='failed'&&expired.error.code==='job_timeout','1ms artifact job budget fails as timeout');
 const replay=await retryTool(expiredRequest);check(replay.job_id===expired.job_id&&replay.state===expired.state&&replay.error.code===expired.error.code,'same timed-out request replays durable failure');
 const successReplay=await retryTool({action:'submit',request_id:'artifact_job',tool:'cad_artifact',arguments:input('nested-stored.3mf','3mf','file')});check(successReplay.job_id===done.job_id&&successReplay.state==='succeeded'&&successReplay.result.sha256===done.result.sha256,'same successful artifact request replays exact package identity');
 check(JSON.stringify(await tool('cad_read',{document_id:'Source'}))===JSON.stringify(headBefore)&&readFileSync(reviewed.path).equals(packageBefore),'artifact cancel/timeout/replay preserve source HEAD and prior package bytes');
 check(JSON.stringify(readdirSync(join(workspace,'artifact_reviews')).sort())===JSON.stringify(publishedBefore),'cancelled and timed-out artifact jobs publish no review package');

 const forgedGeometry=forged(reviewed,v=>v.geometry.triangles[0]=[0,2,1],'forged-geometry');await fails('artifact_mismatch',()=>tool('cad_artifact',forgedGeometry));await fails('artifact_mismatch',()=>tool('cad_artifact_show',{review_path:forgedGeometry.review_path,expected_sha256:forgedGeometry.expected_sha256}));
 for(const [label,modify]of [['source-null',v=>v.source=null],['summary-null',v=>v.summary=null],['missing-geometry',v=>delete v.geometry.positions],['native-label',v=>v.geometry.groups[0].id='face-1']])await fails('artifact_invalid',()=>tool('cad_artifact',forged(reviewed,modify,label)));
 const manifestDir=join(temporary,'bad-manifest');cpSync(reviewed.directory,manifestDir,{recursive:true});writeFileSync(join(manifestDir,'manifest.json'),'[]');await fails('artifact_invalid',()=>tool('cad_artifact',{action:'verify',review_path:join(manifestDir,'review.json'),expected_sha256:reviewed.sha256}));
 const revisionPath=join(workspace,'documents','Source','revisions','1.json'),original=readFileSync(revisionPath);const altered=JSON.parse(original);altered.model.features[0].size=[99,20,30];writeFileSync(revisionPath,JSON.stringify(altered));await fails('artifact_source_mismatch',()=>tool('cad_artifact_show',{review_path:reviewed.path,expected_sha256:reviewed.sha256,view_id:'review'}));writeFileSync(revisionPath,original);
 await fails('artifact_source_missing',()=>tool('cad_artifact',{...input('triangle.stl','stl','mm'),native_source:{document_id:'Source',revision:1,feature_id:'Missing'}}));
 await tool('cad_show',{document_id:'Source',view_id:'review'});await ready();check(!controller.value.read_only&&controller.value.payload.document_id==='Source'&&controller.value.payload.revision===2,'real native source restores native viewer identity and controls');
 const output=process.argv[4];if(output)writeFileSync(output,JSON.stringify(evidence,null,2));console.log(`${checks} real MCP artifact/controller checks passed`);
}finally{controller.dispose();bridge.dispose();child.stdin.end();await new Promise(resolve=>child.once('exit',resolve));lines.close();rmSync(temporary,{recursive:true,force:true});}
