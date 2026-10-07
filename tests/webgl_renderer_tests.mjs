// Pure geometry and mocked WebGL lifecycle tests; no browser claim.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import vm from 'node:vm';
import os from 'node:os';
import path from 'node:path';
import {spawnSync} from 'node:child_process';
const source=fs.readFileSync(new URL('../web/renderer.js',import.meta.url),'utf8');
let checks=0;
function test(name,action){action();checks++;console.log('PASS '+name);}
const scheduled=new Map();let nextFrame=1;
const environment={console,requestAnimationFrame:callback=>{const id=nextFrame++;scheduled.set(id,callback);return id;},cancelAnimationFrame:id=>scheduled.delete(id),devicePixelRatio:2};
vm.runInNewContext(source,environment);
const Renderer=environment.CadRenderer,{validate,prepare,visibleModel,project,screenRay,pick,trace,camera,gpuData}=Renderer.math;
const defaultCamera={yaw:0,pitch:Math.PI/2,zoom:1,pan:[0,0]};
function evaluation(positions,triangles,triangleFaces,edges=[]) {
  return {schema_version:1,document_id:'test',revision:1,evaluation_id:'evaluation-a',feature_id:'part',
    summary:{bounds_mm:{min:[-2,-2,-2],max:[2,2,2]}},
    mesh:{schema_version:1,feature_id:'part',selection_lifetime:'evaluation',positions,triangles,triangle_faces:triangleFaces,edges},
    topology:{schema_version:1,feature_id:'part',selection_lifetime:'evaluation',faces:[...new Set(triangleFaces)].map(id=>({id})),edges:edges.map(({id})=>({id}))}};
}
const flat=[[-1,-1,0],[1,-1,0],[-1,1,0]];
const base=evaluation(flat,[[0,1,2]],['face-1']);
function assembly() {
  const data=evaluation([...flat,...flat.map(([x,y])=>[x,y,.5])],[[0,1,2],[3,4,5]],['face-1','face-2'],[
    {id:'edge-1',part_id:'base',points:[[-.8,-.4,0],[.2,-.4,0]]},
    {id:'edge-2',part_id:'cover',points:[[-.8,-.4,.5],[.2,-.4,.5]]}
  ]);
  data.summary.assembly={parts:[{id:'base',input:'plate',bounds_mm:{min:[-1,-1,0],max:[1,1,0]}},{id:'cover',input:'plate',bounds_mm:{min:[-1,-1,.5],max:[1,1,.5]}}],mates:[]};
  for(let i=0;i<2;i++){data.topology.faces[i].part_id=i?'cover':'base';data.topology.edges[i].part_id=i?'cover':'base';}
  return data;
}
function at(model,point,mode='face',view=defaultCamera){const p=project(point,view,400,400);return pick(model,view,400,400,p[0],p[1],mode);}
test('payload validates exact mapping',()=>assert.equal(validate(base),base));
for(const [name,modify] of [
  ['missing revision',d=>delete d.revision],['stale mesh feature',d=>d.mesh.feature_id='other'],
  ['nonfinite coordinate',d=>d.mesh.positions[0][0]=Infinity],['coordinate outside bounds',d=>d.mesh.positions[0][0]=4],
  ['invalid triangle index',d=>d.mesh.triangles[0][1]=100],['invalid mapped face',d=>d.mesh.triangle_faces[0]='face-999'],
  ['duplicate face ID',d=>d.topology.faces.push({id:'face-1'})],['unrepresented topology',d=>d.topology.faces.push({id:'face-2'})],
  ['inverted bounds',d=>d.summary.bounds_mm.min[0]=3],['unbounded face count',d=>d.topology.faces=Array(10001).fill({id:'face-1'})],
  ['unbounded vertices',d=>d.mesh.positions=Array(200001).fill([0,0,0])],['unbounded triangles',d=>d.mesh.triangles=Array(200001).fill([0,1,2])],
  ['unbounded edge payload',d=>{d.mesh.edges=[{id:'edge-1',points:Array(200001).fill([0,0,0])}];d.topology.edges=[{id:'edge-1'}];}],
  ['duplicate edge mapping',d=>{d.mesh.edges=[{id:'edge-1',points:[]},{id:'edge-1',points:[]}];d.topology.edges=[{id:'edge-1'},{id:'edge-2'}];}],
])test('rejects '+name,()=>{const invalid=structuredClone(base);modify(invalid);assert.throws(()=>validate(invalid));});
test('orthographic projection and ray agree with Z-up',()=>{
  const c={yaw:.73,pitch:.51,zoom:2.5,pan:[.1,-.2]},point=[.17,-.13,.22],screen=project(point,c,900,500),ray=screenRay(screen[0],screen[1],c,900,500);
  for(let i=0;i<3;i++)assert.ok(Math.abs(ray.origin[i]+(screen[2]+2)*ray.direction[i]-point[i])<1e-12);
  assert.ok(project([0,0,1],{yaw:0,pitch:0,zoom:1,pan:[0,0]},400,400)[1]<200);
});
test('camera clamps controls and rejects nonfinite values',()=>{
  assert.equal(camera({yaw:1e20,pitch:8,zoom:1e10,pan:[30,-30]}).zoom,50);
  assert.throws(()=>camera({...defaultCamera,yaw:NaN}));
});
test('face ray selects frontmost triangle at actual hit point',()=>{
  const d=evaluation([[-1,-1,1],[1,-1,-2],[-1,1,-2],[-.9,-.9,0],[-.5,-.9,0],[-.9,-.5,0]],[[0,1,2],[3,4,5]],['face-1','face-2']);
  assert.equal(at(prepare(d),[-.2,-.2,0]).id,'face-1');
});
test('zero projected area is not a selectable face',()=>{
  const d=evaluation([[0,-1,0],[0,1,0],[0,0,1]],[[0,1,2]],['face-1']);
  assert.equal(at(prepare(d),[0,0,0]).id,null);
});
test('coincident faces are explicitly ambiguous',()=>{
  const d=evaluation([...flat,...flat],[[0,1,2],[3,4,5]],['face-1','face-2']);
  const result=at(prepare(d),[-.1,-.1,0]);assert.equal(result.id,null);assert.equal(result.ambiguous,true);
});
test('same-face triangles are not ambiguous',()=>{
  const d=evaluation([...flat,...flat],[[0,1,2],[3,4,5]],['face-1','face-1']);assert.equal(at(prepare(d),[-.1,-.1,0]).id,'face-1');
});
test('near-coincident face ambiguity does not depend on triangle order',()=>{
  const positions=[...flat,...flat.map(([x,y])=>[x,y,3.2e-8]),...flat.map(([x,y])=>[x,y,6e-8])];
  for(const order of [[0,1,2],[2,1,0],[1,0,2]]) {
    const d=evaluation(positions,order.map(i=>[3*i,3*i+1,3*i+2]),order.map(i=>'face-'+(i+1))),model=prepare(d),p=project([-.1,-.1,0],defaultCamera,400,400);
    const hit=trace(model,screenRay(p[0],p[1],defaultCamera,400,400));assert.deepEqual([...hit.ids].sort(),['face-2','face-3']);
  }
});
test('nearest visible edge wins over an occluded edge',()=>{
  const d=evaluation(flat,[[0,1,2]],['face-1'],[{id:'edge-1',points:[[-.8,-.4,-.5],[.2,-.4,-.5]]},{id:'edge-2',points:[[-.8,-.4,.5],[.2,-.4,.5]]}]);
  assert.equal(at(prepare(d),[-.1,-.1,0],'edge').id,'edge-2');
});
test('occluded edges are not picked through faces',()=>{
  const d=evaluation(flat,[[0,1,2]],['face-1'],[{id:'edge-1',points:[[-.8,-.4,-.5],[.2,-.4,-.5]]}]);assert.equal(at(prepare(d),[-.1,-.1,0],'edge').id,null);
});
test('identical visible edges report ambiguity',()=>{
  const edges=['edge-1','edge-2'].map(id=>({id,points:[[-.8,-.4,.5],[.2,-.4,.5]]})),result=at(prepare(evaluation(flat,[[0,1,2]],['face-1'],edges)),[-.1,-.1,0],'edge');
  assert.equal(result.id,null);assert.equal(result.ambiguous,true);
});
test('duplicate segments of one edge do not create ambiguity',()=>{
  const edges=[{id:'edge-1',points:[[-.8,-.4,.5],[.2,-.4,.5],[-.8,-.4,.5]]}],result=at(prepare(evaluation(flat,[[0,1,2]],['face-1'],edges)),[-.1,-.1,0],'edge');
  assert.equal(result.id,'edge-1');assert.equal(result.ambiguous,false);
});
// Fine side tessellation with a coarse edge polyline: every edge chord lies up
// to its sagitta inside the solid, behind the side triangles that bound it.
function cylinder(deflection) {
  const r=10,h=10,sides=256,edgeSegments=12,positions=[],triangles=[],faces=[],points=[];
  for(let i=0;i<sides;i++){const a=2*Math.PI*i/sides;positions.push([r*Math.cos(a),r*Math.sin(a),0],[r*Math.cos(a),r*Math.sin(a),h]);}
  for(let i=0;i<sides;i++){const j=(i+1)%sides;triangles.push([2*i,2*j,2*j+1],[2*i,2*j+1,2*i+1]);faces.push('face-1','face-1');}
  const top=positions.length,bottom=top+1;positions.push([0,0,h],[0,0,0]);
  for(let i=0;i<sides;i++){const j=(i+1)%sides;triangles.push([top,2*i+1,2*j+1],[bottom,2*j,2*i]);faces.push('face-2','face-3');}
  for(let i=0;i<=edgeSegments;i++){const a=2*Math.PI*i/edgeSegments;points.push([r*Math.cos(a),r*Math.sin(a),h]);}
  const mesh={schema_version:1,feature_id:'part',selection_lifetime:'evaluation',positions,triangles,triangle_faces:faces,edges:[{id:'edge-1',points}]};
  if(deflection!==undefined)mesh.linear_deflection_mm=deflection;
  return {schema_version:1,document_id:'test',revision:1,evaluation_id:'evaluation-c',feature_id:'part',summary:{bounds_mm:{min:[-r,-r,0],max:[r,r,h]}},mesh,
    topology:{schema_version:1,feature_id:'part',selection_lifetime:'evaluation',faces:[{id:'face-1'},{id:'face-2'},{id:'face-3'}],edges:[{id:'edge-1'}]}};
}
test('curved edges sampled within the mesh deflection stay pickable without exposing hidden edges',()=>{
  const below={yaw:0,pitch:-.3,zoom:1,pan:[0,0]},rim=angle=>{const p=[10*Math.cos(angle)*Math.cos(Math.PI/12),10*Math.sin(angle)*Math.cos(Math.PI/12),10];return p.map((v,i)=>(v-[0,0,5][i])/20);};
  const at=(model,angle)=>{const p=project(rim(angle),below,400,400);return pick(model,below,400,400,p[0],p[1],'edge');};
  const front=-5*Math.PI/12,back=5*Math.PI/12,sagitta=10*(1-Math.cos(Math.PI/12));
  const model=prepare(cylinder(sagitta*1.01));
  assert.equal(at(model,front).id,'edge-1');
  assert.equal(at(model,back).id,null);
  assert.equal(at(prepare(cylinder()),front).id,null,'without a declared deflection only the exact epsilon applies');
  for(const invalid of [-1,NaN,Infinity,'0.1'])assert.throws(()=>validate(cylinder(invalid)),/deflection/);
});
test('picking outside the viewport returns no entity',()=>assert.equal(pick(prepare(base),defaultCamera,400,400,-1,200).id,null));
test('ray traversal fails explicitly when its work budget is exhausted',()=>assert.throws(()=>trace(prepare(base),screenRay(180,220,defaultCamera,400,400),{remaining:0})));
test('GPU faces and lines retain the exact topology numbers',()=>{
  const d=structuredClone(base);d.mesh.edges=[{id:'edge-1',points:[flat[0],flat[1]]}];d.topology.edges=[{id:'edge-1'}];const buffers=gpuData(prepare(d));
  assert.equal(buffers.faces.length,21);assert.equal(buffers.edges.length,8);assert.equal(buffers.faces[6],1);assert.equal(buffers.edges[3],1);assert.equal(buffers.ranges.get('edge-1').count,2);
});
test('hidden assembly parts neither draw nor occlude visible face and edge picks',()=>{
  const data=assembly(),original=JSON.stringify(data),full=prepare(data),shown=visibleModel(full,['cover']);
  assert.equal(at(full,[-.1,-.1,0]).id,'face-2');assert.equal(at(shown,[-.1,-.1,0]).id,'face-1');
  assert.equal(at(shown,[-.1,-.1,0],'edge').id,'edge-1');
  assert.equal(shown.indices.length,3);assert.equal(shown.edges.length,1);assert.equal(gpuData(shown).faces.length,21);assert.equal(gpuData(shown).edges.length,8);
  assert.equal(shown.identity,full.identity);assert.equal(shown.positions,full.positions);assert.equal(shown.faceNumbers.get('face-1'),full.faceNumbers.get('face-1'));assert.equal(JSON.stringify(data),original);
});
test('all-hidden geometry remains a valid empty draw/pick model and can be restored',()=>{
  const full=prepare(assembly()),empty=visibleModel(full,['cover','base']);
  assert.equal(empty.indices.length,0);assert.equal(empty.bounds,null);assert.equal(at(empty,[0,0,0]).id,null);assert.equal(at(empty,[0,0,0],'edge').id,null);
  assert.equal(gpuData(empty).faces.length,0);assert.equal(gpuData(empty).edges.length,0);assert.equal(visibleModel(full,[]),full);
  assert.throws(()=>visibleModel(full,['missing']));assert.throws(()=>visibleModel(full,['base','base']));
});
test('assembly ownership is validated before presentation filtering',()=>{
  for(const alter of [data=>delete data.topology.faces[0].part_id,data=>data.mesh.edges[0].part_id='cover',data=>data.summary.assembly.parts[0].bounds_mm.max[0]=10]) {
    const data=assembly();alter(data);assert.throws(()=>prepare(data));
  }
});
function mockCanvas({webgl2=true,unavailable=false}={}) {
  const stats={createdBuffers:0,deletedBuffers:0,createdPrograms:0,deletedPrograms:0,draws:0,listeners:new Map(),contexts:[],dimensions:[]};let id=0;
  const gl=new Proxy({NO_ERROR:0,COMPILE_STATUS:1,LINK_STATUS:2,MAX_RENDERBUFFER_SIZE:3,MAX_VIEWPORT_DIMS:4,ALIASED_LINE_WIDTH_RANGE:5,
    createShader:()=>({id:++id}),createProgram:()=>{stats.createdPrograms++;return {id:++id};},createBuffer:()=>{stats.createdBuffers++;return {id:++id};},deleteBuffer:()=>stats.deletedBuffers++,deleteProgram:()=>stats.deletedPrograms++,
    getShaderParameter:()=>true,getProgramParameter:()=>true,getUniformLocation:()=>1,getAttribLocation:(_,name)=>({aPosition:0,aNormal:1,aEntity:2}[name]),getError:()=>0,
    getParameter:name=>name===3?4096:name===4?[4096,4096]:name===5?[1,1]:0,drawArrays:()=>stats.draws++,viewport:(_,__,w,h)=>stats.dimensions.push([w,h]),
  },{get:(target,key)=>key in target?target[key]:(()=>{})});
  const canvas={width:0,height:0,getContext:name=>{stats.contexts.push(name);return unavailable||(!webgl2&&name==='webgl2')?null:gl;},getBoundingClientRect:()=>({left:0,top:0,width:640,height:480}),
    addEventListener:(type,callback)=>stats.listeners.set(type,callback),removeEventListener:type=>stats.listeners.delete(type),focus:()=>{},setPointerCapture:()=>{},hasPointerCapture:()=>false,
    toDataURL:type=>'data:'+type+';base64,example'};
  return {canvas,gl,stats};
}
function flush(){for(const [id,callback] of [...scheduled]){scheduled.delete(id);callback();}}
test('WebGL2 loads, highlights, captures, and releases resources',()=>{
  const {canvas,stats}=mockCanvas(),errors=[],picks=[],renderer=new Renderer(canvas,{onError:e=>errors.push(e),onPick:(...args)=>picks.push(args)});
  renderer.load(base);renderer.setSelection({...base,kind:'face',entity_id:'face-1'});flush();assert.ok(stats.draws>0);assert.equal(errors.length,0);assert.equal(picks.length,0);
  assert.match(renderer.capture(),/^data:image\/png/);renderer.destroy();assert.equal(stats.createdBuffers,stats.deletedBuffers);assert.equal(stats.createdPrograms,stats.deletedPrograms);assert.equal(stats.listeners.size,0);
});
test('WebGL1 fallback uses the same renderer API',()=>{const {canvas,stats}=mockCanvas({webgl2:false}),renderer=new Renderer(canvas);renderer.load(base);flush();assert.deepEqual(stats.contexts,['webgl2','webgl']);assert.ok(stats.draws>0);renderer.destroy();});
test('missing graphics support produces a clear error and never reports ready',()=>{const {canvas}=mockCanvas({unavailable:true}),errors=[],ready=[],renderer=new Renderer(canvas,{onError:e=>errors.push(e),onReady:value=>ready.push(value)});renderer.load(base);flush();assert.match(errors[0].message,/WebGL is unavailable/);assert.equal(ready.length,0);assert.throws(()=>renderer.capture());renderer.destroy();});
test('ready identifies the successfully drawn evaluation without firing on every frame',()=>{
  const {canvas}=mockCanvas(),ready=[],renderer=new Renderer(canvas,{onReady:value=>ready.push(value)});flush();assert.equal(ready.length,0);
  renderer.load(base);flush();assert.equal(ready.length,1);assert.equal(ready[0].evaluation_id,'evaluation-a');renderer.setView('top');flush();assert.equal(ready.length,1);
  renderer.load({...base,evaluation_id:'evaluation-b'});flush();assert.equal(ready.length,2);assert.equal(ready[1].evaluation_id,'evaluation-b');renderer.destroy();
});
test('camera survives reload; reset and explicit replacement are available',()=>{
  const {canvas,stats}=mockCanvas(),renderer=new Renderer(canvas);renderer.load(base);renderer.setCamera({yaw:.3,pitch:.4,zoom:2,pan:[.1,.2]});const saved=JSON.stringify(renderer.getCamera());
  renderer.load({...base,evaluation_id:'evaluation-b'});assert.equal(JSON.stringify(renderer.getCamera()),saved);assert.equal(stats.createdBuffers-stats.deletedBuffers,2);
  const copy=renderer.getCamera();copy.pan[0]=9;assert.equal(JSON.stringify(renderer.getCamera()),saved);
  renderer.load(base,{preserveCamera:false});assert.equal(renderer.getCamera().zoom,1);renderer.setView('top');assert.equal(renderer.getCamera().pitch,Math.PI/2);renderer.reset();assert.equal(renderer.getCamera().pitch,.6);renderer.destroy();
});
test('visibility filters GPU capture/highlights, fits remaining parts, and avoids redundant rebuilds',()=>{
  const {canvas,stats}=mockCanvas(),renderer=new Renderer(canvas),data=assembly();renderer.load(data);renderer.reset();const fullZoom=renderer.getCamera().zoom;
  renderer.setSelection({...data,kind:'face',entity_id:'face-2'});renderer.setHiddenParts(['cover']);
  assert.equal(renderer.selection,null);assert.equal(renderer.resources.faceCount,3);assert.equal(renderer.resources.edgeCount,2);
  assert.throws(()=>renderer.setSelection({...data,kind:'face',entity_id:'face-2'}));
  const created=stats.createdBuffers,model=renderer.model;renderer.setHiddenParts(['cover']);renderer.setView('front');
  assert.equal(stats.createdBuffers,created);assert.equal(renderer.model,model);
  renderer.reset();assert.ok(renderer.getCamera().zoom>fullZoom);renderer.setHiddenParts(['base','cover']);flush();
  assert.equal(renderer.resources.faceCount,0);assert.equal(renderer.resources.edgeCount,0);assert.match(renderer.capture(),/^data:image\/png/);
  renderer.reset();assert.ok(Number.isFinite(renderer.getCamera().zoom));renderer.setHiddenParts([]);assert.equal(renderer.resources.faceCount,6);
  renderer.load({...data,evaluation_id:'evaluation-b'},{hiddenPartIds:['base']});assert.equal(renderer.model.indices.length,3);assert.ok(renderer.model.geometries.face.has('face-2'));
  renderer.destroy();assert.equal(stats.createdBuffers,stats.deletedBuffers);
});
test('stale references and malformed loads cannot replace a valid model',()=>{
  const {canvas}=mockCanvas(),renderer=new Renderer(canvas);renderer.load(base);assert.throws(()=>renderer.setSelection({...base,evaluation_id:'old',kind:'face',entity_id:'face-1'}));
  assert.throws(()=>renderer.load({}));assert.equal(renderer.model.identity.evaluation_id,'evaluation-a');renderer.destroy();
});
test('context loss reports recovery and restores current model buffers',()=>{
  const {canvas,stats}=mockCanvas(),errors=[],ready=[],renderer=new Renderer(canvas,{onError:e=>errors.push(e),onReady:value=>ready.push(value)});renderer.load(base);flush();assert.equal(ready.length,1);
  let prevented=false;stats.listeners.get('webglcontextlost')({preventDefault:()=>{prevented=true;}});assert.equal(prevented,true);assert.match(errors[0].message,/context was lost/);assert.throws(()=>renderer.capture());
  stats.listeners.get('webglcontextrestored')();flush();assert.equal(renderer.lost,false);assert.ok(stats.draws>0);assert.equal(ready.length,2);assert.equal(ready[1].evaluation_id,'evaluation-a');renderer.destroy();
});
test('huge CSS canvases stay inside the framebuffer pixel and dimension budgets',()=>{
  const {canvas,stats}=mockCanvas();canvas.getBoundingClientRect=()=>({left:0,top:0,width:100000,height:50000});const renderer=new Renderer(canvas);renderer.load(base);flush();
  const [w,h]=stats.dimensions.at(-1);assert.ok(w*h<=4000000&&w<=4096&&h<=4096);renderer.destroy();
});
test('pointer picks emit complete evaluation identity and selected geometry',()=>{
  const {canvas,stats}=mockCanvas(),picks=[],renderer=new Renderer(canvas,{onPick:(...args)=>picks.push(args)});renderer.load(base);renderer.setCamera(defaultCamera);
  const point=project([-.1,-.1,0],defaultCamera,640,480),event={pointerId:1,clientX:point[0],clientY:point[1],button:0,preventDefault:()=>{}};
  stats.listeners.get('pointerdown')(event);stats.listeners.get('pointerup')(event);
  assert.equal(picks[0][0].reference.entity_id,'face-1');assert.equal(picks[0][0].reference.evaluation_id,'evaluation-a');assert.equal(picks[0][0].geometry.id,'face-1');renderer.destroy();
});
test('destroy cancels animation and makes further mutations fail',()=>{
  const {canvas}=mockCanvas(),renderer=new Renderer(canvas);renderer.destroy();const before=scheduled.size;assert.throws(()=>renderer.load(base));flush();assert.equal(before,0);
});
if(process.argv[2]) {
  const executable=path.resolve(process.argv[2]),workspace=fs.mkdtempSync(path.join(os.tmpdir(),'cad-webgl-'));
  try {
    const call=(tool,args)=>{
      const result=spawnSync(executable,['call',tool,'--workspace',workspace,'--input','-'],{input:JSON.stringify(args),encoding:'utf8',timeout:45000,maxBuffer:64*1024*1024});
      assert.equal(result.status,0,result.stderr);return JSON.parse(result.stdout);
    };
    for(const name of ['plate','bracket','nozzle','assembly'])test('native '+name+' mesh prepares and maps without conversion',()=>{
      const creation=JSON.parse(fs.readFileSync(new URL('../examples/'+name+'.create.json',import.meta.url),'utf8'));
      call('cad_create',creation);const evaluation=call('cad_query',{document_id:creation.document_id,revision:1,kind:'mesh'}),model=prepare(evaluation),buffers=gpuData(model);
      assert.ok(buffers.faces.length>0);assert.equal(model.faces.length,evaluation.topology.faces.length);assert.equal(model.edges.length,evaluation.topology.edges.length);
      if(name==='assembly') {
        const parts=new Set(evaluation.summary.assembly.parts.map(p=>p.id));
        assert.ok(parts.size>1);
        for(const face of model.geometries.face.values())assert.ok(parts.has(face.part_id));
        for(const edge of model.geometries.edge.values())assert.ok(parts.has(edge.part_id));
        const keep=[...parts][0],filtered=visibleModel(model,[...parts].filter(id=>id!==keep));
        assert.ok(filtered.indices.length>0&&filtered.indices.length<model.indices.length);
        for(const face of filtered.geometries.face.values())assert.equal(face.part_id,keep);
        for(const edge of filtered.geometries.edge.values())assert.equal(edge.part_id,keep);
      }
      let picked=false;
      for(let i=0;i<model.indices.length;i+=3){const centroid=[0,0,0];for(let j=0;j<3;j++)for(let a=0;a<3;a++)centroid[a]+=model.positions[3*model.indices[i+j]+a]/3;const p=project(centroid,defaultCamera,400,400);if(pick(model,defaultCamera,400,400,p[0],p[1],'face').id){picked=true;break;}}
      assert.ok(picked,'native mesh has a selectable visible face');
    });
    // Edge polylines and face triangulations are sampled independently within
    // the kernel's linear deflection, so curved edges can sit slightly behind
    // their own faces. The nozzle's walls are at least 2 mm thick.
    test('native nozzle edges offset only by tessellation stay pickable; hidden edges do not',()=>{
      const evaluation=call('cad_query',{document_id:'nozzle',revision:1,kind:'mesh'}),model=prepare(evaluation);
      const b=evaluation.summary.bounds_mm,span=Math.max(...b.max.map((v,i)=>v-b.min[i])),deflection=evaluation.mesh.linear_deflection_mm;
      assert.ok(deflection>0);let offset=0,hidden=0;
      for(const yaw of [-2.5,-1.3,-.65,0,.9,2.1])for(const pitch of [-1.2,-.6,.3,.6,1.1]) {
        const view={yaw,pitch,zoom:1,pan:[0,0]};
        for(const edge of model.edges)for(let i=3;i<edge.points.length;i+=3) {
          const mid=[0,1,2].map(k=>(edge.points[i-3+k]+edge.points[i+k])/2),p=project(mid,view,800,800);
          const gap=(p[2]+2-trace(model,screenRay(p[0],p[1],view,800,800)).depth)*span,picked=pick(model,view,800,800,p[0],p[1],'edge').id;
          if(gap>1e-6&&gap<=deflection){offset++;assert.equal(picked,edge.id,`${edge.id} (yaw ${yaw}, pitch ${pitch}) lies ${gap.toFixed(3)} mm behind its tessellated face`);}
          if(gap>1){hidden++;assert.notEqual(picked,edge.id,`${edge.id} (yaw ${yaw}, pitch ${pitch}) is hidden ${gap.toFixed(3)} mm behind other geometry`);}
        }
      }
      assert.ok(offset>=20&&hidden>=100,`exercised ${offset} tessellation offsets and ${hidden} hidden samples`);
    });
  }finally{fs.rmSync(workspace,{recursive:true,force:true});}
}
console.log(`webgl renderer: ${checks} checks passed (pure math and mocked GPU lifecycle)`);
