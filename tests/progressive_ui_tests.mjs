import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import vm from 'node:vm';
for(const file of ['renderer.js','progressive.js'])vm.runInThisContext(readFileSync(new URL('../web/'+file,import.meta.url),'utf8'),{filename:file});
let checks=0;const check=(v,label)=>{assert.ok(v,label);checks++;};
const matrix=x=>[1,0,0,x,0,1,0,0,0,0,1,0,0,0,0,1],levels=[.5,.1,.01].map((d,level)=>({level,linear_deflection_mm:d,angular_deflection_rad:[.8,.4,.2][level]}));
function manifest(){const parts=[{id:'a',input:'shape',transform:matrix(0),bounds_mm:{min:[0,0,0],max:[10,10,0]}},{id:'b',input:'shape',transform:matrix(20),bounds_mm:{min:[20,0,0],max:[30,10,0]}},{id:'c',input:'second',transform:matrix(60),bounds_mm:{min:[60,0,0],max:[70,10,0]}}];return {schema_version:1,document_id:'test',revision:1,evaluation_id:'eval_one',feature_id:'assembly',draft:false,summary:{bounds_mm:{min:[0,0,0],max:[70,10,0]},assembly:{parts}},topology:{schema_version:1,units:'mm',feature_id:'assembly',selection_lifetime:'evaluation',faces:parts.map((p,i)=>({id:'face-'+(i+1),part_id:p.id})),edges:parts.map((p,i)=>({id:'edge-'+(i+1),part_id:p.id}))},components:{schema_version:1,coordinate_space:'source_local_mm',geometries:['shape','second'].map(id=>({id,bounds_mm:{min:[0,0,0],max:[10,10,0]},topology_sha256:'a'.repeat(64),levels})),occurrences:parts.map((p,i)=>({id:p.id,geometry_id:p.input,transform:p.transform,faces:['face-'+(i+1)],edges:['edge-'+(i+1)]}))}};}
function response(p,id,level){return {...Object.fromEntries(['document_id','revision','evaluation_id','feature_id'].map(k=>[k,p[k]])),component_mesh:{geometry_id:id,level,topology_sha256:'a'.repeat(64),mesh:{schema_version:1,units:'mm',feature_id:id,selection_lifetime:'evaluation',...levels[level],positions:[[0,0,0],[10,0,0],[0,10,0]],triangles:Array.from({length:[1,4,16][level]},()=>[0,1,2]),triangle_faces:Array([1,4,16][level]).fill('face-1'),edges:[{id:'edge-1',points:[[0,0,0],[10,0,0]]}]}}};}
function host(p,{gate=null,corrupt=false,makeResponse=response}={}){const requests=[],cancel=[];return {requests,cancel,request:async args=>{if(args.cancel){cancel.push(args);return {component_state:'cancelled'};}if(args.offset===0)requests.push([args.geometry_id,args.level]);if(gate)await gate(args);const data=JSON.stringify(makeResponse(p,args.geometry_id,args.level));return {component_state:'ready',evaluation_id:corrupt?'eval_wrong':p.evaluation_id,geometry_id:args.geometry_id,level:args.level,data:data.slice(args.offset,args.offset+128*1024),offset:args.offset,next_offset:args.offset+128*1024<data.length?args.offset+128*1024:null,total_bytes:data.length};}};}
const camera={yaw:0,pitch:Math.PI/2,zoom:.1,pan:[0,0]};
{
 const p=manifest(),h=host(p),published=[],s=new CadProgressiveSession(p,{...h,camera,onPayload:x=>published.push(x)}),first=await s.loadFirst();
 check(h.requests.length===1,'First rendered payload needs one source mesh only');check(first.mesh.triangles.length===2,'Both repeated occurrences use the first source mesh');assert.deepEqual(first.mesh.positions[3],[20,0,0]);checks++;
 check(first.topology===p.topology&&first.summary===p.summary,'Framing and exact selection descriptors do not change with partial display');CadRenderer.math.validate(first);checks++;
 await s.whenReady();check(h.requests.length===2,'Repeated instances never cause duplicate source transfer');check(published.at(-1).mesh.triangles.length===3,'All sources complete independently');
 s.update({camera:{...camera,zoom:10}});await s.whenReady();check(h.requests.some(([id,l])=>l===2),'Camera zoom requests fine geometry');check(published.at(-1).evaluation_id===first.evaluation_id,'Refinement preserves evaluation qualified picks');
 const count=h.requests.length;s.update({camera});await s.whenReady();check(s.resident.get('shape')===0&&s.resident.get('second')===0,'Zoom out coarsens visible geometry');check(h.requests.length===count,'Coarsening reuses cached coarse source meshes');
 const source=CadRenderer.math.prepare(first),fine=CadRenderer.math.prepare(published.at(-1));assert.deepEqual(source.center,fine.center);assert.equal(source.span,fine.span);checks+=2;
 s.update({hiddenPartIds:['a','b']});await s.whenReady();check(s.priorities().length===1,'Hidden components do not request refinements');s.dispose();check(s.pool.size===0,'Retired sources release resident/cache meshes');
}
{
 const p=manifest(),bad=structuredClone(p);bad.components.occurrences[1].faces=['face-1'];assert.throws(()=>CadProgressiveSession.validateManifest(bad),/correspondence/);checks++;
 bad.components.occurrences[1].faces=['face-2'];bad.components.occurrences[1].transform[0]=2;assert.throws(()=>CadProgressiveSession.validateManifest(bad),/rigid/);checks++;
 const r=response(p,'shape',0);r.component_mesh.topology_sha256='b'.repeat(64);assert.throws(()=>CadProgressiveSession.validateComponent(r,p,p.components.geometries[0],0),/qualified/);checks++;
}
{
 const p=manifest();let unblock;const gate=new Promise(r=>unblock=r),h=host(p,{gate:()=>gate}),s=new CadProgressiveSession(p,{...h,camera}),first=s.loadFirst();await Promise.resolve();s.dispose();unblock();check(await first===null,'Superseded requests never publish delayed geometry');check(h.cancel.length===1,'Retiring a live request cancels its native worker');
}
{
 const p=manifest(),h=host(p),frames=[],errors=[],s=new CadProgressiveSession(p,{...h,camera,onPayload:x=>frames.push(x),onError:e=>errors.push(e)});await s.loadFirst();await s.whenReady();const before=frames.at(-1);s.request=host(p,{corrupt:true}).request;s.update({camera:{...camera,zoom:10}});await assert.rejects(s.whenReady(),/identity/);checks++;
 check(s.resident.get('shape')===0&&frames.at(-1)===before,'Failed refinement leaves last successful resident geometry intact');check(errors.length>0,'Refinement failures surface instead of silently substituting geometry');s.dispose();
}
{
 const p=manifest(),h=host(p),s=new CadProgressiveSession(p,{...h,camera,memoryBytes:1024});await assert.rejects(s.loadFirst(),/memory budget/);checks++;check(s.pool.size===0,'Oversized source never enters cache');s.dispose();
}
{
 const p=manifest(),h=host(p),s=new CadProgressiveSession(p,{...h,camera});await s.loadFirst();await s.whenReady();
 // Small camera changes inside hysteresis do not churn workers.
 const before=h.requests.length;for(const zoom of [.101,.099,.1]){s.update({camera:{...camera,zoom}});await s.whenReady();}check(h.requests.length===before,'Detail hysteresis avoids camera jitter churn');s.dispose();
}
{
 const p=manifest(),h=host(p),frames=[],s=new CadProgressiveSession(p,{...h,camera,onPayload:x=>frames.push(x)});await s.loadFirst();await s.whenReady();
 s.update({hiddenPartIds:['a','b','c']});await s.whenReady();check(frames.at(-1).mesh.triangles.length===0,'Capture readiness awaits pending hide composition even without a mesh request');s.dispose();
}
{
 const p=manifest(),h=host(p),s=new CadProgressiveSession(p,{...h,camera});await s.loadFirst();
 s.request=async a=>a.geometry_id==='second'?{component_state:'error',evaluation_id:p.evaluation_id,geometry_id:a.geometry_id,level:a.level,error:{message:'Specific source failed'}}:h.request(a);
 s.update({camera:{...camera,zoom:1}});await assert.rejects(s.whenReady(),/Specific source failed/);checks++;
 check(s.status().failed_requests===1&&s.status().missing_visible_geometries===1&&s.status().error.includes('Specific source failed'),'Later successful refinements cannot erase an unresolved source failure');
 s.update({camera:{...camera,zoom:.99}});await assert.rejects(s.whenReady(),/Specific source failed/);checks++;s.dispose();
}
{
 const p=manifest(),h=host(p);let refusals=0,first=null;const s=new CadProgressiveSession(p,{camera,request:async a=>{if(!first)first={...a};if(refusals++===0){const e=Error('Native publication lock busy');e.code='workspace_busy';throw e;}check(a.request_id===first.request_id&&a.offset===first.offset,'Explicit lock refusal retries identical job identity and chunk offset');return h.request(a);}});
 const payload=await s.loadFirst();check(payload.mesh.triangles.length>0&&s.failed.size===0,'Transient native lock refusal does not become a permanent missing source');s.dispose();
}

{
 const p=manifest(),makeResponse=(p,id,level)=>{const r=response(p,id,level),n=level===2?128:1;r.component_mesh.mesh.triangles=Array.from({length:n},()=>[0,1,2]);r.component_mesh.mesh.triangle_faces=Array(n).fill('face-1');return r;};
 const budget=CadProgressiveSession.validateComponent(makeResponse(p,'shape',2),p,p.components.geometries[0],2).bytes+100;
 const h=host(p,{makeResponse}),frames=[],s=new CadProgressiveSession(p,{...h,camera:{...camera,zoom:3,pan:[-.4,0]},hiddenPartIds:['c'],memoryBytes:budget,onPayload:x=>frames.push(x)});
 await s.loadFirst();await s.whenReady();check(s.resident.get('shape')===2,'Visible geometry initially refines when the budget permits');
 s.update({hiddenPartIds:[]});await s.whenReady();
 check(s.resident.size===2&&frames.at(-1).progressive.loaded_occurrences===3,'Memory pressure coarsens visible detail to admit missing geometry');
 check(h.requests.filter(([id,l])=>id==='shape'&&l===0).length===2,'Pressure downgrade refetches evicted coarse source only when needed');
 check(s.status().pressure_limited_geometries===2&&!s.status().error&&s.status().cache_bytes<=budget,'Pressure caps keep coverage within memory without false failure or refinement loops');s.dispose();
 const small=host(p,{makeResponse}),capped=new CadProgressiveSession(p,{...small,camera:{...camera,zoom:3,pan:[-.4,0]},hiddenPartIds:['c'],memoryBytes:3000});
 await capped.loadFirst();await capped.whenReady();check(capped.resident.get('shape')===0&&capped.status().pressure_limited_geometries===1,'A fine mesh larger than the entire budget retains usable coarse geometry');capped.resident.set('shape',2);check(await capped.relievePressure({id:'shape',level:0})===false,'An unexpectedly oversized coarsening cannot repeat forever');capped.dispose();
}
{
 const p=manifest(),h=host(p,{makeResponse:(p,id,level)=>{const r=response(p,id,level),n=67000;r.component_mesh.mesh.triangles=Array.from({length:n},()=>[0,1,2]);r.component_mesh.mesh.triangle_faces=Array(n).fill('face-1');return r;}}),s=new CadProgressiveSession(p,{...h,camera});
 await s.loadFirst();await assert.rejects(s.whenReady(),/visible occurrences/);checks++;
 check(s.status().omitted_visible_occurrences===1&&s.status().missing_visible_geometries===0,'Occurrence staging limits are distinct from resident source completion');
 s.update({hiddenPartIds:['c']});await s.whenReady();check(!s.status().omitted_visible_occurrences&&!s.status().error,'Hiding the omitted occurrence allows a complete visible capture');s.dispose();
}
{
 const p=manifest(),h=host(p),s=new CadProgressiveSession(p,{...h,camera,memoryBytes:3000});await s.loadFirst();await assert.rejects(s.whenReady(),/memory budget/);checks++;
 s.update({hiddenPartIds:['a','b']});await s.whenReady();check(s.resident.has('second')&&!s.resident.has('shape')&&!s.status().error,'A changed view retries only pressure refusals and can evict newly offscreen geometry');
 check(h.requests.filter(([id])=>id==='second').length===2,'Pressure recovery retries once for a meaningful visibility change');s.dispose();
}
{
 const p=manifest();p.summary.bounds_mm={min:[-50,-50,-50],max:[50,50,50]};p.summary.assembly.parts[2].bounds_mm={min:[44,45,0],max:[46,50,0]};
 const camera={yaw:0,pitch:0,zoom:1,pan:[.43,0]},s=new CadProgressiveSession(p,{camera,width:1000,height:700});
 s.priorities();check(!s.onScreen.has('c'),'Regression occurrence is outside orthographic viewport');
 const render={projection:'perspective',lens_mm:50};s.update({render});s.priorities();
 const actual=CadRenderer.math.project([.44,.5,0],{...camera,projection:'perspective',distance:3},1000,700);
 check(actual[0]<1000&&s.onScreen.has('c'),'Perspective priority retains geometry visibly projected by the renderer');
 const before=s.viewKey;s.update({render:{...render,lens_mm:120}});check(s.viewKey!==before,'Lens changes invalidate scheduler priority and pressure caps');s.dispose();
}
console.log(`${checks} progressive scheduling/identity/budget checks passed`);
