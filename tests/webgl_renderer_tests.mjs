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
const environment={console,TextEncoder,requestAnimationFrame:callback=>{const id=nextFrame++;scheduled.set(id,callback);return id;},cancelAnimationFrame:id=>scheduled.delete(id),devicePixelRatio:2};
vm.runInNewContext(source,environment);
vm.runInNewContext(fs.readFileSync(new URL('../web/state.js',import.meta.url),'utf8'),environment);
const Renderer=environment.CadRenderer,{validate,prepare,visibleModel,project,screenRay,pick,trace,camera,gpuData,presentedModel,defaultPresentation,clipSegment,sectionGeometry,validateSection,sectionModel,appearance,defaultAppearance,appearanceData,annotations,annotationLayout,annotationLabelBounds}=Renderer.math;
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
test('nested occurrence ownership remains selectable and hierarchy rejects missing or false parents',()=>{
  const data=assembly();
  data.summary.assembly.parts.forEach(part=>{part.id='module/'+part.id;});
  for(const item of [...data.topology.faces,...data.topology.edges,...data.mesh.edges])item.part_id='module/'+item.part_id;
  data.summary.assembly.tree=[{id:'module',input:'subassembly',assembly_id:'machine',parent_id:'',kind:'assembly'},
    ...data.summary.assembly.parts.map(part=>({id:part.id,input:part.input,assembly_id:'subassembly',parent_id:'module',kind:'part'}))];
  const model=prepare(data),filtered=visibleModel(model,['module/cover']);
  const selected=at(filtered,[-.1,-.1,0]);assert.equal(selected.id,'face-1');
  assert.equal(filtered.geometries.face.get(selected.id).part_id,'module/base');
  for(const alter of [d=>d.summary.assembly.tree.shift(),d=>d.summary.assembly.tree.pop(),
    d=>d.summary.assembly.tree[0].kind='part',d=>d.summary.assembly.tree[1].parent_id='',
    d=>d.summary.assembly.tree[1].id='module/../base']) {
    const invalid=structuredClone(data);alter(invalid);assert.throws(()=>prepare(invalid));
  }
});
test('clipped faces do not occlude source face picks and reversing the plane selects the other side',()=>{
  const source=prepare(assembly()),value=defaultPresentation();value.clip={normal:[0,0,1],offset_mm:.25,keep:'negative'};
  const shown=presentedModel(source,value);assert.equal(at(shown,[-.1,-.1,0]).id,'face-1');
  value.clip.keep='positive';assert.equal(at(presentedModel(source,value),[-.1,-.1,0]).id,'face-2');
  value.clip.offset_mm=5;assert.equal(at(presentedModel(source,value),[-.1,-.1,0]).id,null);
  assert.equal(source.clip,undefined);assert.equal(source.geometries.face.get('face-1').part_id,'base');
});
test('edge picking clips segments at their plane crossing and ignores the discarded half',()=>{
  const source=prepare(assembly()),value=defaultPresentation();value.clip={normal:[1,0,0],offset_mm:0,keep:'negative'};
  const shown=presentedModel(source,value),edge=shown.edges[1];
  const cut=clipSegment(shown,[.1,0,0],[-.1,0,0]);assert.ok(Math.abs(cut[0][0])<2e-7);
  assert.equal(clipSegment(shown,[.1,0,0],[.2,0,0]),null);
  assert.equal(at(shown,[-.1,-.1,0],'edge').id,'edge-2');
  assert.equal(at(shown,[.04,-.1,0],'edge').id,null);
  assert.equal(edge.points.length,source.edges[1].points.length);
});
test('exploded parts retain source measurements, references and independent vertex ownership',()=>{
  const data=assembly();data.mesh.triangles[1]=[0,1,2];data.mesh.positions=data.mesh.positions.slice(0,3);
  const raw=JSON.stringify(data),full=prepare(data),value=defaultPresentation();value.explode={distance_mm:4,directions:[{part_id:'base',direction:[-1,0,0]},{part_id:'cover',direction:[1,0,0]}]};
  const shown=presentedModel(full,value);assert.equal(shown.positions.length,18);assert.equal(shown.identity,full.identity);
  assert.equal(shown.positions[3*shown.indices[0]],full.positions[0]-1);assert.equal(shown.positions[3*shown.indices[3]],full.positions[0]+1);
  const c={...defaultCamera,zoom:.2},point=project([-.1+1,-.1,0],c,400,400);
  assert.equal(pick(shown,c,400,400,point[0],point[1]).id,'face-2');
  assert.equal(shown.geometries.face.get('face-2'),full.geometries.face.get('face-2'));assert.equal(JSON.stringify(data),raw);
});
test('large exploded depth uses the same extended range for CPU picking and GPU coordinates',()=>{
  const full=prepare(assembly()),value=defaultPresentation();value.explode={distance_mm:20,directions:[{part_id:'base',direction:[0,0,-1]}]};
  const shown=presentedModel(visibleModel(full,['cover']),value);assert.ok(shown.depthExtent>5);
  const point=project([-.1,-.1,-5],defaultCamera,400,400);assert.equal(pick(shown,defaultCamera,400,400,point[0],point[1]).id,'face-1');
  assert.equal(shown.bounds.min[2],-5);assert.equal(full.positions[2],0);
});
test('presentation rejects non-unit directions, unknown/duplicate owners, arbitrary fields and invalid distances',()=>{
  const full=prepare(assembly());
  for(const mutate of [v=>v.clip={normal:[0,0,0],offset_mm:0,keep:'positive'},v=>v.explode.distance_mm=-1,v=>v.explode.distance_mm=Infinity,
    v=>v.explode.directions=[{part_id:'unknown',direction:[1,0,0]}],v=>v.explode.directions=[{part_id:'base',direction:[1,0,0]},{part_id:'base',direction:[0,1,0]}],v=>v.script='bad']) {
    const value=defaultPresentation();mutate(value);assert.throws(()=>presentedModel(full,value));
  }
  const value=defaultPresentation();value.explode.distance_mm=1;assert.throws(()=>presentedModel(prepare(base),value));
});
function sectionSettings(offset=.25) {const value=defaultPresentation();value.clip={normal:[0,0,1],offset_mm:offset,keep:'negative'};return value;}
function capResult(data=base,offset=.25,owner=null) {
  const positions=flat.map(([x,y])=>[x,y,offset]),lengths=[2,Math.sqrt(8),2],boundary=lengths.reduce((a,b)=>a+b,0);
  const curves=positions.map((a,i)=>{const b=positions[(i+1)%3];return {id:'section-'+(i+1),part_id:owner,solid_index:1,curve_kind:'line',length_mm:lengths[i],center_mm:a.map((v,k)=>(v+b[k])/2),bounds_mm:{min:a.map((v,k)=>Math.min(v,b[k])),max:a.map((v,k)=>Math.max(v,b[k]))},degenerate:false,points:[a,b]};});
  return {document_id:data.document_id,revision:data.revision,evaluation_id:data.evaluation_id,feature_id:data.feature_id,kernel_version:'8.0.1',model_sha256:'a'.repeat(64),native_build:'native-test-build',
    report:{schema_version:1,units:'mm',action:'section',method:'native_BRep_planar_section',coordinate_space:'committed_source_pose',plane_coordinate_space:'displayed_world_mm',coverage:owner?'explicit_leaf_subset':'feature_solids',area_semantics:'sum_of_solid_sections',plane:{normal:[0,0,1],offset_mm:offset},explode:{distance_mm:0,directions:[]},
      sections:[{part_id:owner,source_plane_offset_mm:offset,displacement_mm:[0,0,0],area_mm2:2,boundary_length_mm:boundary,region_count:1,curve_count:3,contact_points:[],status:'area'}],regions:[{id:'cap-1',part_id:owner,solid_index:1,area_mm2:2,perimeter_mm:boundary,center_mm:[-1/3,-1/3,offset],wire_count:1}],curves,
      mesh:{positions,triangles:[[0,1,2]],triangle_regions:['cap-1'],linear_deflection_mm:.1},area_mm2:2,boundary_length_mm:boundary,status:'area',selection_lifetime:'section_result',tolerance_mm:1e-7,point_tolerance_mm:1e-6}};
}
function withSection(data,result,settings=sectionSettings(),hidden=[]) {const full=prepare(data),model=presentedModel(visibleModel(full,hidden),settings);validateSection(result,full,settings);model.section=sectionModel(result,full,hidden);return model;}
test('section geometry retains plane/explosion but ignores kept side, direction order and zero-distance overrides',()=>{
  const value=sectionSettings(),other=structuredClone(value);other.clip.keep='positive';other.explode.directions=[{part_id:'base',direction:[1,0,0]}];
  assert.equal(JSON.stringify(sectionGeometry(value)),JSON.stringify(sectionGeometry(other)));assert.equal(sectionGeometry(defaultPresentation()),null);
  value.explode={distance_mm:1,directions:[{part_id:'z',direction:[1,0,0]},{part_id:'a',direction:[0,1,0]}]};other.explode={distance_mm:1,directions:[...value.explode.directions].reverse()};
  assert.equal(JSON.stringify(sectionGeometry(value)),JSON.stringify(sectionGeometry(other)));
});
test('native section caps occlude original faces without inventing topology references',()=>{
  const result=capResult(),raw=JSON.stringify(base),model=withSection(base,result),hit=at(model,[-.1,-.1,0]);
  assert.equal(hit.id,null);assert.equal(hit.ambiguous,false);assert.equal(hit.section,true);assert.equal(model.geometries.face.has('cap-1'),false);
  assert.equal(JSON.stringify(base),raw);assert.equal(result.report.area_mm2,2);
  assert.throws(()=>trace(model,screenRay(180,220,defaultCamera,400,400),{remaining:1}),/work limit/);
});
test('source edge deflection allowance cannot expose geometry behind a native cap',()=>{
  const data=structuredClone(base);data.mesh.linear_deflection_mm=.1;data.mesh.edges=[{id:'edge-1',points:[[-.8,-.4,.24],[.2,-.4,.24]]}];data.topology.edges=[{id:'edge-1'}];
  const full=presentedModel(prepare(data),sectionSettings());assert.equal(at(full,[-.1,-.1,0],'edge').id,'edge-1');
  const capped=withSection(data,capResult(data));const hit=at(capped,[-.1,-.1,0],'edge');assert.equal(hit.id,null);assert.equal(hit.section,true);
});
test('hidden cap owners stop drawing and occluding without shrinking section coverage',()=>{
  const data=assembly();data.summary.assembly.parts[1].bounds_mm.min[2]=0;data.summary.assembly.parts[1].bounds_mm.max[2]=1;const result=capResult(data,.25,'cover');const shown=withSection(data,result),hidden=withSection(data,result,sectionSettings(),['cover']);
  assert.equal(at(shown,[-.1,-.1,0]).section,true);assert.equal(at(hidden,[-.1,-.1,0]).id,'face-1');assert.equal(hidden.section.indices.length,0);assert.equal(hidden.section.edges.length,0);assert.equal(result.report.sections.length,1);
});
test('native section rejects stale identity, unsafe fields, malformed geometry and false coverage',()=>{
  const full=prepare(base),value=sectionSettings();
  for(const mutate of [r=>r.evaluation_id='old',r=>r.model_sha256='bad',r=>r.report.method='mesh_slice',r=>r.report.plane.offset_mm=.5,r=>r.report.mesh.positions[0][2]+=.01,r=>r.report.mesh.positions[0][0]=10,
    r=>r.report.regions[0].part_id='missing',r=>r.report.mesh.triangle_regions[0]='face-1',r=>r.report.regions[0].id='face-1',r=>r.report.area_mm2=3,
    r=>r.report.sections[0].displacement_mm[0]=1,r=>r.report.curves[0].center_mm[2]=.5,r=>r.report.curves[0].points[0][0]=Infinity,r=>r.report.mesh.triangles[0][0]=100,
    r=>r.report.mesh.positions=Array(200001).fill([0,0,.25]),r=>r.report.curves[0].points=Array(200001).fill([0,0,.25]),r=>r.report.sections[0].contact_points=Array(10001).fill([0,0,.25]),
    r=>r.report.script='bad',r=>r.report.mesh.script='bad',r=>r.report.curves[0].script='bad',r=>r.report.coverage='all_assembly_leaves',r=>r.report.regions[0].wire_count=0]) {
    const invalid=capResult();mutate(invalid);assert.throws(()=>validateSection(invalid,full,value));
  }
  assert.throws(()=>validateSection(capResult(),full,defaultPresentation()));
});
test('finite unused OCCT triangulation vertices remain qualified without drawing or picking them',()=>{
  const result=capResult();result.report.mesh.positions.push([2,2,.25]);const model=withSection(base,result);assert.equal(model.section.positions.length,9);assert.equal(model.section.indices.length,3);
  result.report.mesh.positions[3][2]=1;assert.throws(()=>validateSection(result,prepare(base),sectionSettings()),/Unused native cap vertex/);
});
test('combined native section arrays retain the 8 MiB report limit',()=>{
  const result=capResult();result.report.curves[0].points=Array(200000).fill([.3333333333333333,.3333333333333333,.25]);
  assert.ok(JSON.stringify(result.report).length>8*1024*1024);assert.throws(()=>validateSection(result,prepare(base),sectionSettings()),/byte limit/);
});
test('empty and tangent native sections retain honest status without invented cap geometry',()=>{
  for(const tangent of [false,true]) {
    const result=capResult(),r=result.report;r.regions=[];r.mesh.positions=[];r.mesh.triangles=[];r.mesh.triangle_regions=[];r.area_mm2=0;r.sections[0].area_mm2=0;r.sections[0].region_count=0;
    if(!tangent){r.curves=[];r.boundary_length_mm=0;r.sections[0].boundary_length_mm=0;r.sections[0].curve_count=0;}r.status=r.sections[0].status=tangent?'tangent':'empty';
    const model=withSection(base,result);assert.equal(model.section.indices.length,0);assert.equal(model.section.edges.length,tangent?3:0);assert.equal(at(model,[-.1,-.1,0]).id,'face-1');
  }
});
function mockCanvas({webgl2=true,unavailable=false}={}) {
  const stats={createdBuffers:0,deletedBuffers:0,createdPrograms:0,deletedPrograms:0,draws:0,listeners:new Map(),contexts:[],dimensions:[]};let id=0;
  const gl=new Proxy({NO_ERROR:0,COMPILE_STATUS:1,LINK_STATUS:2,MAX_RENDERBUFFER_SIZE:3,MAX_VIEWPORT_DIMS:4,ALIASED_LINE_WIDTH_RANGE:5,
    createShader:()=>({id:++id}),createProgram:()=>{stats.createdPrograms++;return {id:++id};},createBuffer:()=>{stats.createdBuffers++;return {id:++id};},deleteBuffer:()=>stats.deletedBuffers++,deleteProgram:()=>stats.deletedPrograms++,
    getShaderParameter:()=>true,getProgramParameter:()=>true,getUniformLocation:()=>1,getAttribLocation:(_,name)=>({aPosition:0,aNormal:1,aEntity:2,aColor:3}[name]),getError:()=>0,
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
test('clipping updates uniforms/picking without rebuilding geometry buffers and survives graphics recovery',()=>{
  const {canvas,stats}=mockCanvas(),renderer=new Renderer(canvas);renderer.load(assembly());const original=renderer.fullModel,created=stats.createdBuffers;
  const settings=defaultPresentation();settings.clip={normal:[0,0,1],offset_mm:.25,keep:'negative'};renderer.setPresentation(settings);flush();
  assert.equal(stats.createdBuffers,created);assert.equal(renderer.fullModel,original);assert.ok(renderer.model.clip);
  settings.explode.distance_mm=2;renderer.setPresentation(settings);assert.ok(stats.createdBuffers>created);const position=Array.from(renderer.model.positions);
  const invalidated=[renderer.resources.faces,renderer.resources.edges].filter(Boolean).length;
  stats.listeners.get('webglcontextlost')({preventDefault:()=>{}});stats.listeners.get('webglcontextrestored')();flush();assert.deepEqual(Array.from(renderer.model.positions),position);assert.ok(renderer.model.clip);
  renderer.setHiddenParts(['base','cover']);flush();assert.equal(renderer.resources.faceCount,0);renderer.setHiddenParts([]);assert.ok(renderer.model.clip);renderer.destroy();assert.equal(stats.createdBuffers-stats.deletedBuffers,invalidated,'The lost context invalidates its old buffers; all subsequent buffers are deleted');
});
test('appearance validates finite opaque RGB and current leaf ownership before rendering',()=>{
  const full=prepare(assembly()),style={default_color:[.1,.2,.3],parts:[{part_id:'base',color:[1,0,0]},{part_id:'cover',color:[0,1,0]}]},copy=JSON.stringify(style),colors=appearanceData(full,appearance(style,full));
  assert.deepEqual(Array.from(colors.slice(0,9)),[1,0,0,1,0,0,1,0,0]);assert.deepEqual(Array.from(colors.slice(9)),[0,1,0,0,1,0,0,1,0]);assert.equal(JSON.stringify(style),copy);assert.equal(appearanceData(full,defaultAppearance()).length,0);
  for(const alter of [v=>v.default_color=[NaN,0,0],v=>v.default_color=[0,2,0],v=>v.parts[0].color=[1,0],v=>v.parts[0].part_id='missing',v=>v.parts.push(v.parts[0]),v=>v.opacity=.5,v=>v.parts[0].script='bad',v=>v.parts=Array(1025).fill(v.parts[0])]){const bad=structuredClone(style);alter(bad);assert.throws(()=>appearance(bad,full));}
  assert.equal(appearance(style,prepare(base),{prune:true}).parts.length,0);assert.throws(()=>appearance(style,prepare(base)));assert.equal(appearance(style,full).parts.length,2);
});
test('part colors preserve native section buffers, original picking, opaque occlusion and capture',()=>{
  const {canvas,stats}=mockCanvas(),renderer=new Renderer(canvas),data=assembly();data.summary.assembly.parts[1].bounds_mm.min[2]=0;data.summary.assembly.parts[1].bounds_mm.max[2]=1;
  renderer.load(data);renderer.setPresentation(sectionSettings());renderer.setSection(capResult(data,.25,'cover'));const original=renderer.fullModel,sourceBuffers=[renderer.resources.faces,renderer.resources.edges],capBuffers=[renderer.resources.capFaces,renderer.resources.capEdges],section=renderer.sectionResult;
  const style={default_color:[.1,.2,.3],parts:[{part_id:'base',color:[1,0,0]},{part_id:'cover',color:[0,1,0]}]};renderer.setAppearance(style);assert.ok(renderer.resources.colors);assert.deepEqual([renderer.resources.faces,renderer.resources.edges],sourceBuffers);assert.deepEqual([renderer.resources.capFaces,renderer.resources.capEdges],capBuffers);assert.equal(renderer.sectionResult,section);assert.equal(renderer.fullModel,original);assert.equal(at(renderer.model,[-.1,-.1,0]).section,true);
  const created=stats.createdBuffers;renderer.setAppearance(style);assert.equal(stats.createdBuffers,created);renderer.setHiddenParts(['cover']);assert.equal(at(renderer.model,[-.1,-.1,0]).id,'face-1');assert.equal(renderer.model.geometries.face.get('face-1').part_id,'base');assert.equal(renderer.resources.capFaces,null);assert.ok(renderer.resources.colors);assert.match(renderer.capture(),/^data:image\/png/);
  renderer.setHiddenParts([]);const invalidated=5;stats.listeners.get('webglcontextlost')({preventDefault:()=>{}});stats.listeners.get('webglcontextrestored')();flush();assert.ok(renderer.resources.colors&&renderer.resources.capFaces);assert.equal(renderer.appearance.parts.length,2);assert.equal(renderer.sectionResult,section);
  const saved=JSON.stringify(renderer.appearance),bad=structuredClone(style);bad.parts[0].part_id='missing';assert.throws(()=>renderer.setAppearance(bad));assert.equal(JSON.stringify(renderer.appearance),saved);
  renderer.setAppearance(defaultAppearance());assert.equal(renderer.resources.colors,null);assert.equal(renderer.sectionResult,section);renderer.destroy();assert.equal(stats.createdBuffers-stats.deletedBuffers,invalidated);
});
test('default surface color needs no extra buffer and is reset by a new source load',()=>{
  const {canvas,stats}=mockCanvas(),renderer=new Renderer(canvas);renderer.load(base);const created=stats.createdBuffers;renderer.setAppearance({default_color:[.8,.2,.1],parts:[]});assert.equal(stats.createdBuffers,created);assert.equal(renderer.resources.colors,null);assert.deepEqual(Array.from(renderer.appearance.default_color),[.8,.2,.1]);
  renderer.load({...base,evaluation_id:'next'});assert.deepEqual(Array.from(renderer.appearance.default_color),[.66,.75,.8]);renderer.destroy();assert.equal(stats.createdBuffers,stats.deletedBuffers);
});
test('section buffers render, capture, remain readonly, survive context recovery and retire atomically',()=>{
  const {canvas,stats}=mockCanvas(),picks=[],renderer=new Renderer(canvas,{onPick:(...v)=>picks.push(v)});renderer.load(base);renderer.setPresentation(sectionSettings());const sourceBuffers=[renderer.resources.faces,renderer.resources.edges],result=capResult(),original=renderer.fullModel;
  renderer.setSection(result);flush();assert.equal(stats.createdBuffers,4);assert.deepEqual([renderer.resources.faces,renderer.resources.edges],sourceBuffers);assert.equal(renderer.resources.capFaceCount,3);assert.equal(renderer.resources.capEdgeCount,6);assert.equal(renderer.fullModel,original);
  const created=stats.createdBuffers,section=renderer.model.section;renderer.setSection(result);renderer.setCamera(defaultCamera);assert.equal(stats.createdBuffers,created);
  renderer._pick(...project([-.1,-.1,0],defaultCamera,640,480));assert.equal(picks[0][0],null);assert.equal(picks[0][1].section,true);assert.match(picks[0][1].message,/section surface/);assert.throws(()=>renderer.setSelection({...base,kind:'face',entity_id:'cap-1'}));assert.match(renderer.capture(),/^data:image\/png/);
  const keep=sectionSettings();keep.clip.keep='positive';renderer.setPresentation(keep);assert.equal(renderer.sectionResult,result);assert.equal(renderer.model.section,section);assert.equal(stats.createdBuffers,created);
  const invalid=capResult();invalid.evaluation_id='old';assert.throws(()=>renderer.setSection(invalid));assert.equal(renderer.sectionResult,result);
  const invalidated=4;stats.listeners.get('webglcontextlost')({preventDefault:()=>{}});stats.listeners.get('webglcontextrestored')();flush();assert.equal(renderer.resources.capFaceCount,3);assert.equal(renderer.sectionResult,result);
  renderer.setPresentation(sectionSettings(.5));assert.equal(renderer.sectionResult,null);assert.equal(renderer.resources.capFaces,null);assert.equal(renderer.model.section,undefined);assert.equal(renderer.fullModel,original);
  renderer.setPresentation(sectionSettings());renderer.setSection(result);renderer.setSection(null);assert.equal(renderer.resources.capFaces,null);assert.equal(renderer.resources.capEdges,null);renderer.destroy();assert.equal(stats.createdBuffers-stats.deletedBuffers,invalidated);
});
test('section GPU buffers follow hidden owners and source reloads',()=>{
  const {canvas,stats}=mockCanvas(),renderer=new Renderer(canvas),data=assembly();data.summary.assembly.parts[1].bounds_mm.min[2]=0;data.summary.assembly.parts[1].bounds_mm.max[2]=1;const result=capResult(data,.25,'cover');renderer.load(data);renderer.setPresentation(sectionSettings());renderer.setSection(result);
  renderer.setHiddenParts(['cover']);assert.equal(renderer.resources.capFaceCount,0);assert.equal(renderer.resources.capFaces,null);renderer.setHiddenParts([]);assert.equal(renderer.resources.capFaceCount,3);
  renderer.load({...data,evaluation_id:'new'});assert.equal(renderer.sectionResult,null);assert.equal(renderer.resources.capFaces,null);assert.equal(stats.createdBuffers-stats.deletedBuffers,2);renderer.destroy();assert.equal(stats.createdBuffers,stats.deletedBuffers);
});
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
function reviewNote(data,anchor={kind:'model',part_id:null,point_mm:[0,0,0],position_semantics:'bounds_center'}){return{id:'ann_1',text:'Inspect clearance <plain text>',document_id:data.document_id,revision:data.revision,evaluation_id:data.evaluation_id,feature_id:data.feature_id,anchor_lifetime:'evaluation',coordinate_space:'committed_source_pose',status:'current',anchor};}
test('review anchors validate their own source identity, closed fields and UTF-8 bounds',()=>{
 const full=prepare(base),note=reviewNote(base);assert.equal(annotations([note],full)[0].text,note.text);
 for(const mutate of [n=>n.evaluation_id='old',n=>n.anchor.point_mm=[9,0,0],n=>n.anchor.part_id='absent',n=>n.anchor.reference={},n=>n.status='active',n=>n.text='é'.repeat(257),n=>n.text='bad\u0001',n=>n.script='x',n=>n.anchor_lifetime='stable',n=>n.revision=0]){const bad=structuredClone(note);mutate(bad);assert.throws(()=>annotations([bad],full));}
 assert.throws(()=>annotations([note,note],full));assert.throws(()=>annotations(Array(33).fill(note),full));assert.throws(()=>annotations([note],full,{draft:true}));
 const retired={...note,status:'retired',evaluation_id:'old_evaluation'};assert.equal(annotations([retired],full)[0].status,'retired');assert.equal(annotationLayout([retired],full,defaultPresentation(),defaultCamera,400,400).length,0);
});
test('native entity inspection centers must retain the actual resolved owner and coordinates',()=>{
 const data=assembly();data.topology.faces[0].center_mm=[0,0,0];const full=prepare(data),reference={...full.identity,kind:'face',entity_id:'face-1'},note=reviewNote(data,{kind:'entity',part_id:'base',point_mm:[0,0,0],position_semantics:'entity_center',reference});
 assert.equal(annotations([note],full).length,1);const bad=structuredClone(note);bad.anchor.point_mm[0]=.1;assert.throws(()=>annotations([bad],full));bad.anchor.point_mm=[0,0,0];bad.anchor.reference.entity_id='face-999';assert.throws(()=>annotations([bad],full));
 const original=JSON.stringify(data);annotations([note],full);assert.equal(JSON.stringify(data),original);
});
test('numbered review pins follow native leaf explosion and filter hidden, clipped and offscreen centers',()=>{
 const data=assembly(),full=prepare(data),note=reviewNote(data,{kind:'part',part_id:'cover',point_mm:[0,0,.5],position_semantics:'bounds_center'}),settings=defaultPresentation();annotations([note],full);
 const initial=annotationLayout([note],full,settings,defaultCamera,400,400)[0];settings.explode={distance_mm:1,directions:[{part_id:'cover',direction:[1,0,0]}]};const moved=annotationLayout([note],full,settings,defaultCamera,400,400)[0];assert.ok(moved.x>initial.x);assert.equal(moved.number,1);
 assert.equal(annotationLayout([note],full,settings,defaultCamera,400,400,['cover']).length,0);settings.clip={normal:[1,0,0],offset_mm:2,keep:'positive'};assert.equal(annotationLayout([note],full,settings,defaultCamera,400,400).length,0);settings.clip.keep='negative';assert.equal(annotationLayout([note],full,settings,defaultCamera,400,400).length,1);
 assert.equal(annotationLayout([note],full,settings,{...defaultCamera,pan:[20,0]},400,400).length,0);
});
test('review pins do not replace source buffers or source picks and PNG capture composites their labels',()=>{
 const {canvas,stats}=mockCanvas(),draw=[];canvas.ownerDocument={createElement:()=>({getContext:()=>new Proxy({measureText:text=>({width:text.length*6})},{get:(o,k)=>k in o?o[k]:(...args)=>draw.push([k,...args])}),toDataURL:()=> 'data:image/png;base64,withpins'})};
 const layouts=[],renderer=new Renderer(canvas,{onAnnotations:layout=>layouts.push(layout)});renderer.load(base);renderer.setCamera(defaultCamera);const buffers=stats.createdBuffers,source=renderer.fullModel,note=reviewNote(base);renderer.setAnnotations([note]);flush();assert.equal(stats.createdBuffers,buffers);assert.equal(renderer.fullModel,source);assert.equal(layouts.at(-1)[0].id,note.id);
 assert.equal(renderer.capture(),'data:image/png;base64,withpins');assert.ok(draw.some(row=>row[0]==='drawImage'&&row[1]===canvas));assert.ok(draw.some(row=>row[0]==='fillText'&&row[1]===note.text));assert.ok(draw.some(row=>row[0]==='fillText'&&row[1]==='1'));
 assert.equal(at(renderer.model,[-.1,-.1,0]).id,'face-1');renderer.load(base);assert.equal(renderer.annotations.length,0);renderer.destroy();assert.equal(layouts.at(-1).length,0);
});
test('review overlays survive graphics restoration and never capture retired anchors',()=>{
 const {canvas,stats}=mockCanvas(),layouts=[],renderer=new Renderer(canvas,{onAnnotations:layout=>layouts.push(layout)});renderer.load(base);renderer.setAnnotations([reviewNote(base)]);flush();stats.listeners.get('webglcontextlost')({preventDefault(){}});renderer.setAnnotations([reviewNote(base)]);assert.equal(layouts.at(-1).length,0);assert.throws(()=>renderer.capture());stats.listeners.get('webglcontextrestored')();flush();assert.equal(layouts.at(-1).length,1);renderer.setAnnotations([{...reviewNote(base),status:'retired',evaluation_id:'old'}]);assert.match(renderer.capture(),/^data:image\/png/);assert.equal(layouts.at(-1).length,0);renderer.destroy();
});
test('captured labels stay in the viewport without covering their numbered pin at edges',()=>{
 for(const [width,height,pin,textWidth] of [[640,480,{x:550,y:240},300],[640,480,{x:12,y:12},300],[180,100,{x:90,y:50},500],[180,100,{x:90,y:85},500]]) {
  const box=annotationLabelBounds(pin,textWidth,width,height);
  assert.ok(box.x>=4&&box.y>=4&&box.x+box.width<=width-4&&box.y+box.height<=height-4);
  assert.ok(box.x>=pin.x+11||box.x+box.width<=pin.x-11||box.y>=pin.y+11||box.y+box.height<=pin.y-11);
 }
 const {canvas}=mockCanvas(),draw=[];canvas.ownerDocument={createElement:()=>({getContext:()=>new Proxy({measureText:text=>({width:text.length*6})},{get:(o,k)=>k in o?o[k]:(...args)=>draw.push([k,...args])}),toDataURL:()=> 'data:image/png;base64,withpins'})};
 const renderer=new Renderer(canvas);renderer.load(base);renderer.setCamera({...defaultCamera,pan:[.45,0]});renderer.setAnnotations([{...reviewNote(base),text:'Review the current revision housing near the viewport edge.'}]);flush();renderer.capture();
 const arc=draw.find(row=>row[0]==='arc'),box=draw.find(row=>row[0]==='fillRect');assert.ok(box[1]+box[3]<=arc[1]-arc[3]);assert.ok(draw.some(row=>row[0]==='fillText'&&row[1]==='1'));renderer.destroy();
});
// ---- Shapr3D-style navigation math ----
const M=Renderer.math;
const near=(a,b,eps=1e-9)=>assert.ok(Math.abs(a-b)<=eps,`${a} != ${b}`);
const plain=value=>JSON.parse(JSON.stringify(value));
test('standard views look along the documented axes',()=>{
  const look=name=>{const [yaw,pitch]=M.STANDARD_VIEWS[name];return plain(M.basis({yaw,pitch,zoom:1,pan:[0,0]})[2]).map(v=>Math.round(v*1e9)/1e9+0);};
  assert.deepEqual(look('front'),[0,1,0]);assert.deepEqual(look('back'),[0,-1,0]);
  assert.deepEqual(look('right'),[-1,0,0]);assert.deepEqual(look('left'),[1,0,0]);
  assert.deepEqual(look('top'),[0,0,-1]);assert.deepEqual(look('bottom'),[0,0,1]);
  assert.deepEqual(Object.keys(M.STANDARD_VIEWS).sort(),['back','bottom','front','iso','left','right','top']);
});
test('viewFromDirection reproduces the axis views and gives a true isometric corner',()=>{
  for(const [name,d] of [['front',[0,-1,0]],['back',[0,1,0]],['right',[1,0,0]],['left',[-1,0,0]],['top',[0,0,1]],['bottom',[0,0,-1]]]){
    const v=M.viewFromDirection(d),[yaw,pitch]=M.STANDARD_VIEWS[name];
    near(Math.cos(v.yaw),Math.cos(yaw));near(Math.sin(v.yaw),Math.sin(yaw));near(v.pitch,pitch);
  }
  const corner=M.viewFromDirection([1,-1,1]);near(corner.yaw,-Math.PI/4);near(corner.pitch,Math.asin(1/Math.sqrt(3)));
  assert.throws(()=>M.viewFromDirection([0,0,0]));
  assert.throws(()=>M.viewFromDirection([1,NaN,0]));
});
test('lerpCamera hits both endpoints, takes the short way round and eases',()=>{
  const a={yaw:3,pitch:0,zoom:1,pan:[0,0]},b={yaw:-3,pitch:.5,zoom:4,pan:[.2,-.2]};
  assert.deepEqual(plain(M.lerpCamera(a,b,0)),a);
  const end=M.lerpCamera(a,b,1);near(Math.cos(end.yaw),Math.cos(b.yaw));near(Math.sin(end.yaw),Math.sin(b.yaw));near(end.zoom,4);near(end.pitch,.5);
  const mid=M.lerpCamera(a,b,.5);near(Math.abs(Math.cos(mid.yaw)),1,1e-2);near(mid.zoom,2);
  near(M.easeOut(0),0);near(M.easeOut(1),1);assert.ok(M.easeOut(.5)>.5);
});
test('zoomAbout keeps the world point under the cursor fixed',()=>{
  const c={yaw:.4,pitch:.7,zoom:1.3,pan:[.05,-.08]},p=[.12,-.2,.07],[x,y]=M.project(p,c,640,480);
  for(const factor of [1.7,.4,3]){const z=M.zoomAbout(c,factor,x,y,640,480),[x2,y2]=M.project(p,z,640,480);near(x2,x,1e-7);near(y2,y,1e-7);near(z.zoom,Math.min(50,Math.max(.05,c.zoom*factor)),1e-12);}
  assert.equal(M.zoomAbout({...c,zoom:49},10,100,100,640,480).zoom,50);
});
test('fitCamera frames bounds inside the viewport and centred',()=>{
  const c={yaw:-.65,pitch:.6,zoom:1,pan:[0,0]},bounds={min:[-.5,-.5,-.5],max:[.5,.5,.5]},f=M.fitCamera(c,bounds,640,480);
  const corners=[];for(const x of [-.5,.5])for(const y of [-.5,.5])for(const z of [-.5,.5])corners.push(M.project([x,y,z],f,640,480));
  for(const [x,y] of corners)assert.ok(x>0&&x<640&&y>0&&y<480);
  near((Math.min(...corners.map(p=>p[0]))+Math.max(...corners.map(p=>p[0])))/2,320,1e-6);
  near((Math.min(...corners.map(p=>p[1]))+Math.max(...corners.map(p=>p[1])))/2,240,1e-6);
});
test('entityBounds finds a face or edge extent and rejects unknown ids',()=>{
  const model=prepare(evaluation([[-1,-1,0],[1,-1,0],[-1,1,0]],[[0,1,2]],['face-1'],[{id:'edge-1',points:[[-.8,-.4,0],[.2,-.4,0]]}]));
  const face=M.entityBounds(model,'face','face-1'),edge=M.entityBounds(model,'edge','edge-1');
  assert.ok(face.max[0]-face.min[0]>=.1);assert.ok(edge.max[0]-edge.min[0]>.2);
  assert.equal(M.entityBounds(model,'face','nope'),null);assert.equal(M.entityBounds(model,'edge','nope'),null);
});
test('setView accepts the new names and reset still frames the model',()=>{
  const {canvas}=mockCanvas(),r=new Renderer(canvas);r.load(base);
  for(const name of ['iso','top','bottom','front','back','right','left'])r.setView(name);
  near(Math.abs(r.getCamera().yaw),Math.PI/2);
  assert.throws(()=>r.setView('diagonal'));r.reset();assert.ok(r.getCamera().zoom>0);r.destroy();
});

// ---- Unified face/edge picking and hover ----
const withEdge=()=>evaluation([[-1,-1,0],[1,-1,0],[-1,1,0]],[[0,1,2]],['face-1'],[{id:'edge-1',points:[[-.8,-.4,0],[.2,-.4,0]]}]);
const edgeMid=(model,width=400,height=400)=>{const p=model.edges[0].points,a=project([p[0],p[1],p[2]],defaultCamera,width,height),b=project([p[3],p[4],p[5]],defaultCamera,width,height);return [(a[0]+b[0])/2,(a[1]+b[1])/2];};
test('auto pick: an edge within 6px wins, a face wins beyond it, edge mode keeps 9px',()=>{
  const model=prepare(withEdge()),[x,y]=edgeMid(model);
  let r=pick(model,defaultCamera,400,400,x,y+3,'auto');assert.equal(r.id,'edge-1');assert.equal(r.kind,'edge');
  r=pick(model,defaultCamera,400,400,x,y+8,'auto');assert.equal(r.id,'face-1');assert.equal(r.kind,'face');
  assert.equal(pick(model,defaultCamera,400,400,x,y+8,'edge').id,'edge-1');
  assert.equal(pick(model,defaultCamera,400,400,x,y+8,'face').id,'face-1');
  assert.equal('kind' in pick(model,defaultCamera,400,400,x,y+8,'face'),false,'face/edge modes keep their original result shape');
});
test('auto pick: empty space selects nothing and ambiguous edges fall through to the face',()=>{
  const model=prepare(withEdge());
  assert.equal(pick(model,defaultCamera,400,400,5,5,'auto').id,null);
  const twin=withEdge();twin.mesh.edges.push({id:'edge-2',points:[[-.8,-.4,0],[.2,-.4,0]]});twin.topology.edges.push({id:'edge-2'});
  const m2=prepare(twin),[x,y]=edgeMid(m2),r=pick(m2,defaultCamera,400,400,x,y,'auto');
  assert.equal(r.id,'face-1');assert.equal(r.kind,'face');
});
test('auto pick reports overlapping faces without selecting either',()=>{
  const d=evaluation([...flat,...flat],[[0,1,2],[3,4,5]],['face-1','face-2']),m=prepare(d),p=project([-.2,-.2,0],defaultCamera,400,400);
  const r=pick(m,defaultCamera,400,400,p[0],p[1],'auto');assert.equal(r.id,null);assert.equal(r.ambiguous,true);
});
test('a click selects whatever auto resolves and clears on empty space; mode compatibility remains',()=>{
  const {canvas}=mockCanvas(),picks=[],r=new Renderer(canvas,{onPick:(...v)=>picks.push(v)});
  assert.equal(r.mode,'auto');r.load(withEdge());r.setCamera(defaultCamera);
  const [x,y]=edgeMid(r.model,640,480);
  r._pick(x,y+2);assert.equal(picks.at(-1)[0].reference.kind,'edge');assert.equal(r.selection.kind,'edge');
  const f=project([-.2,-.2,0],defaultCamera,640,480);r._pick(f[0],f[1]);assert.equal(picks.at(-1)[0].reference.kind,'face');assert.equal(r.selection.kind,'face');
  r._pick(2,2);assert.equal(picks.at(-1)[0],null);assert.equal(r.selection,null);
  r.setMode('edge');assert.equal(r.mode,'edge');r.setMode('face');r.setMode('auto');assert.throws(()=>r.setMode('vertex'));
  r.destroy();
});
test('hover highlights the entity under an idle cursor without selecting, one pick per frame',()=>{
  const {canvas,stats}=mockCanvas(),hovers=[],picks=[],r=new Renderer(canvas,{onHover:h=>hovers.push(h),onPick:(...v)=>picks.push(v)});
  r.load(withEdge());r.setCamera(defaultCamera);flush();
  const [cx,cy]=edgeMid(r.model,640,480),move=(x,y)=>stats.listeners.get('pointermove')({pointerId:1,clientX:x,clientY:y,buttons:0});
  move(cx,cy+1);move(cx,cy+2);move(cx,cy+3);flush();
  assert.deepEqual(plain(r.hover),{kind:'edge',entity_id:'edge-1'});assert.equal(hovers.length,1);assert.equal(r.selection,null);assert.equal(picks.length,0);
  const f=project([-.2,-.2,0],defaultCamera,640,480);move(f[0],f[1]);flush();assert.deepEqual(plain(r.hover),{kind:'face',entity_id:'face-1'});
  stats.listeners.get('pointerleave')({});flush();assert.equal(r.hover,null);assert.equal(hovers.at(-1),null);
  r.destroy();assert.equal(stats.listeners.size,0);
});
test('hover is skipped while dragging and cleared when the model changes',()=>{
  const {canvas,stats}=mockCanvas(),r=new Renderer(canvas);r.load(withEdge());r.setCamera(defaultCamera);flush();
  const f=project([-.2,-.2,0],defaultCamera,640,480),L=type=>stats.listeners.get(type);
  L('pointerdown')({pointerId:1,button:0,clientX:f[0],clientY:f[1],shiftKey:false,preventDefault(){}});
  L('pointermove')({pointerId:1,clientX:f[0]+30,clientY:f[1]+30,buttons:1});flush();assert.equal(r.hover,null);
  L('pointerup')({pointerId:1,clientX:f[0]+30,clientY:f[1]+30});
  L('pointermove')({pointerId:1,clientX:f[0],clientY:f[1],buttons:0});flush();assert.ok(r.hover);
  r.load({...withEdge(),evaluation_id:'next'});assert.equal(r.hover,null);r.destroy();
});

// ---- Navigation input, animation, theme ----
const L=(stats,type)=>stats.listeners.get(type);
const down=(stats,o)=>L(stats,'pointerdown')({pointerId:1,button:0,shiftKey:false,preventDefault(){},...o});
test('mouse mapping: left and right orbit, middle and Shift pan',()=>{
  const {canvas,stats}=mockCanvas(),r=new Renderer(canvas);r.load(withEdge());r.setCamera({yaw:0,pitch:.3,zoom:1,pan:[0,0]});
  const drag=(o,dx,dy)=>{down(stats,{clientX:100,clientY:100,...o});L(stats,'pointermove')({pointerId:1,clientX:100+dx,clientY:100+dy,buttons:1});L(stats,'pointerup')({pointerId:1,clientX:100+dx,clientY:100+dy});};
  let c=plain(r.getCamera());drag({button:0},40,0);assert.ok(r.getCamera().yaw!==c.yaw);assert.deepEqual(plain(r.getCamera().pan),c.pan);
  c=plain(r.getCamera());drag({button:2},0,30);assert.ok(r.getCamera().pitch!==c.pitch);assert.deepEqual(plain(r.getCamera().pan),c.pan);
  c=plain(r.getCamera());drag({button:1},40,10);assert.equal(r.getCamera().yaw,c.yaw);assert.ok(r.getCamera().pan[0]!==c.pan[0]);
  c=plain(r.getCamera());drag({button:0,shiftKey:true},-40,0);assert.equal(r.getCamera().yaw,c.yaw);assert.ok(r.getCamera().pan[0]!==c.pan[0]);
  c=plain(r.getCamera());drag({button:2,shiftKey:true},0,30);assert.equal(r.getCamera().pitch,c.pitch);assert.ok(r.getCamera().pan[1]!==c.pan[1]);
  r.destroy();
});
test('a right-button click does not select; a left click does',()=>{
  const {canvas,stats}=mockCanvas(),picks=[],r=new Renderer(canvas,{onPick:(...v)=>picks.push(v)});r.load(withEdge());r.setCamera(defaultCamera);
  const f=project([-.2,-.2,0],defaultCamera,640,480);
  down(stats,{button:2,clientX:f[0],clientY:f[1]});L(stats,'pointerup')({pointerId:1,clientX:f[0],clientY:f[1]});assert.equal(picks.length,0);
  down(stats,{button:0,clientX:f[0],clientY:f[1]});L(stats,'pointerup')({pointerId:1,clientX:f[0],clientY:f[1]});assert.equal(picks.length,1);r.destroy();
});
test('wheel zooms about the cursor',()=>{
  const {canvas,stats}=mockCanvas(),r=new Renderer(canvas);r.load(withEdge());r.setCamera({yaw:.4,pitch:.5,zoom:1,pan:[.02,.03]});
  const c=plain(r.getCamera()),p=[.1,-.1,0],[x,y]=project(p,c,640,480);
  L(stats,'wheel')({preventDefault(){},deltaY:-240,deltaMode:0,clientX:x,clientY:y});
  const [x2,y2]=project(p,r.getCamera(),640,480);near(x2,x,1e-6);near(y2,y,1e-6);assert.ok(r.getCamera().zoom>c.zoom);r.destroy();
});
test('animateTo interpolates over 250ms, saves once, is cancelled by input and is instant for reduced motion',()=>{
  let t=0;const {canvas,stats}=mockCanvas(),moves=[],r=new Renderer(canvas,{now:()=>t,reducedMotion:()=>false,onCamera:c=>moves.push(c)});
  r.load(withEdge());r.setCamera({yaw:0,pitch:0,zoom:1,pan:[0,0]});moves.length=0;
  r.animateTo({yaw:1,pitch:.5,zoom:2,pan:[0,0]});assert.equal(moves.length,0);
  t=125;flush();const mid=r.getCamera();assert.ok(mid.yaw>0&&mid.yaw<1);assert.equal(moves.length,0,'camera is not saved mid-animation');
  t=250;flush();near(r.getCamera().yaw,1);near(r.getCamera().zoom,2);assert.equal(moves.length,1,'camera saved once at the end');
  r.animateTo({yaw:0,pitch:0,zoom:1,pan:[0,0]});t=300;flush();const held=r.getCamera();
  down(stats,{button:0,clientX:5,clientY:5});assert.equal(r.anim,null);t=600;flush();near(r.getCamera().yaw,held.yaw);
  L(stats,'pointerup')({pointerId:1,clientX:5,clientY:5});
  const instant=new Renderer(mockCanvas().canvas,{reducedMotion:()=>true});instant.load(withEdge());instant.animateTo({yaw:1,pitch:.5,zoom:2,pan:[0,0]});near(instant.getCamera().yaw,1);
  r.destroy();instant.destroy();
});
test('double-click frames the entity under the cursor, or fits when empty; Space frames the hover',()=>{
  const {canvas,stats}=mockCanvas(),r=new Renderer(canvas,{reducedMotion:()=>true});r.load(withEdge());r.setCamera(defaultCamera);
  const f=project([-.2,-.2,0],defaultCamera,640,480),before=r.getCamera();
  L(stats,'dblclick')({clientX:f[0],clientY:f[1],preventDefault(){}});assert.ok(r.getCamera().zoom>before.zoom,'framing a small face zooms in');
  r.setCamera({...defaultCamera,zoom:30});L(stats,'dblclick')({clientX:3,clientY:3,preventDefault(){}});assert.ok(r.getCamera().zoom<30,'empty double-click fits all');
  r.setCamera(defaultCamera);L(stats,'pointermove')({pointerId:1,clientX:f[0],clientY:f[1],buttons:0});flush();
  const z=r.getCamera().zoom;L(stats,'keydown')({key:' ',preventDefault(){},shiftKey:false});assert.ok(r.getCamera().zoom>z);r.destroy();
});
test('digit keys select standard views',()=>{
  const {canvas,stats}=mockCanvas(),r=new Renderer(canvas,{reducedMotion:()=>true}),key=k=>L(stats,'keydown')({key:k,preventDefault(){},shiftKey:false});
  r.load(withEdge());
  for(const [k,name] of [['1','iso'],['2','front'],['3','back'],['4','top'],['5','bottom'],['6','right'],['7','left']]){
    key(k);const [yaw,pitch]=M.STANDARD_VIEWS[name];near(Math.cos(r.getCamera().yaw),Math.cos(yaw));near(Math.sin(r.getCamera().yaw),Math.sin(yaw));near(r.getCamera().pitch,pitch);
  }
  r.destroy();
});
test('theme: light by default, dark available, validated, drawn through the backdrop pass and kept across context loss',()=>{
  const {canvas,stats}=mockCanvas(),r=new Renderer(canvas);r.load(withEdge());flush();
  assert.deepEqual(r.getTheme(),M.defaultTheme());assert.notDeepEqual(M.darkTheme(),M.defaultTheme());
  r.setTheme(M.darkTheme());assert.deepEqual(r.getTheme(),M.darkTheme());
  assert.throws(()=>r.setTheme({background:{top:[2,0,0],bottom:[0,0,0]},line:[0,0,0],select:[0,0,0],hover:[0,0,0]}));
  assert.throws(()=>r.setTheme({line:[0,0,0]}));
  assert.equal(stats.createdPrograms,2,'main program plus WebGL2 backdrop program');
  L(stats,'webglcontextlost')({preventDefault(){}});L(stats,'webglcontextrestored')();flush();assert.deepEqual(r.getTheme(),M.darkTheme());
  const deletedBefore=stats.deletedPrograms;r.destroy();assert.equal(stats.deletedPrograms-deletedBefore,2);
});
test('WebGL1 has no backdrop program and still draws with a solid theme colour',()=>{
  const {canvas,stats}=mockCanvas({webgl2:false}),r=new Renderer(canvas);r.load(withEdge());flush();
  assert.equal(stats.createdPrograms,1);assert.ok(stats.draws>0);r.destroy();assert.equal(stats.createdPrograms,stats.deletedPrograms);
});

test('onView reports orientation changes, including state-driven ones, but not pan or zoom',()=>{
  const {canvas}=mockCanvas(),views=[],r=new Renderer(canvas,{onView:c=>views.push(plain(c))});
  r.load(withEdge());flush();assert.ok(views.length>=1,'initial orientation is reported once the first frame draws');views.length=0;
  r.setCamera({yaw:.5,pitch:.2,zoom:1,pan:[0,0]});flush();assert.equal(views.length,1);near(views[0].yaw,.5);
  r.setCamera({yaw:.5,pitch:.2,zoom:3,pan:[.1,.1]});flush();assert.equal(views.length,1,'zoom and pan do not rotate the cube');
  r.setCamera({yaw:.9,pitch:.2,zoom:3,pan:[.1,.1]});flush();assert.equal(views.length,2);near(views[1].yaw,.9);
  r.destroy();
});

test('fitCamera centres the model in the usable area when chrome covers the edges',()=>{
  const c={yaw:-.65,pitch:.6,zoom:1,pan:[0,0]},bounds={min:[-.5,-.5,-.5],max:[.5,.5,.5]},insets={top:56,right:84,bottom:96,left:256};
  const f=M.fitCamera(c,bounds,900,600,insets),corners=[];
  for(const x of [-.5,.5])for(const y of [-.5,.5])for(const z of [-.5,.5])corners.push(M.project([x,y,z],f,900,600));
  const xs=corners.map(p=>p[0]),ys=corners.map(p=>p[1]);
  assert.ok(Math.min(...xs)>=insets.left&&Math.max(...xs)<=900-insets.right,'fits horizontally inside the usable rectangle');
  assert.ok(Math.min(...ys)>=insets.top&&Math.max(...ys)<=600-insets.bottom,'fits vertically inside the usable rectangle');
  near((Math.min(...xs)+Math.max(...xs))/2,insets.left+(900-insets.left-insets.right)/2,1e-6);
  near((Math.min(...ys)+Math.max(...ys))/2,insets.top+(600-insets.top-insets.bottom)/2,1e-6);
  assert.deepEqual(plain(M.fitCamera(c,bounds,900,600)),plain(M.fitCamera(c,bounds,900,600,{top:0,right:0,bottom:0,left:0})),'zero insets match the previous behaviour');
});
test('renderer insets drive reset, fit-all and framing, and reject bad values',()=>{
  const {canvas}=mockCanvas(),r=new Renderer(canvas,{reducedMotion:()=>true});r.load(withEdge());
  assert.throws(()=>r.setInsets({top:-1,right:0,bottom:0,left:0}));assert.throws(()=>r.setInsets({top:0,right:0,bottom:NaN,left:0}));assert.throws(()=>r.setInsets(null));
  r.setInsets({top:0,right:0,bottom:200,left:0});r.reset();
  const b=r.model.bounds,ys=[];for(const x of [b.min[0],b.max[0]])for(const y of [b.min[1],b.max[1]])for(const z of [b.min[2],b.max[2]])ys.push(project([x,y,z],r.getCamera(),640,480)[1]);
  assert.ok(Math.max(...ys)<=480-200,'reset keeps the model above the bottom inset');
  r.setCamera({...r.getCamera(),zoom:30});r.fitAll();
  const after=[];for(const x of [b.min[0],b.max[0]])for(const y of [b.min[1],b.max[1]])for(const z of [b.min[2],b.max[2]])after.push(project([x,y,z],r.getCamera(),640,480)[1]);
  assert.ok(Math.max(...after)<=480-200,'fit-all honours the inset too');r.destroy();
});

test('hover backs off after a slow pick and resumes with the latest pointer position',()=>{
  let t=0;const {canvas,stats}=mockCanvas(),hovers=[],r=new Renderer(canvas,{now:()=>(t+=20),onHover:h=>hovers.push(plain(h))});
  r.load(withEdge());r.setCamera(defaultCamera);flush();
  const [cx,cy]=edgeMid(r.model,640,480),f=project([-.2,-.2,0],defaultCamera,640,480),move=(x,y)=>stats.listeners.get('pointermove')({pointerId:1,clientX:x,clientY:y,buttons:0});
  move(cx,cy);flush();assert.equal(hovers.length,1);assert.equal(hovers[0].kind,'edge');
  move(f[0],f[1]);flush();assert.equal(hovers.length,1,'a pick inside the back-off window is deferred, not dropped');
  for(let i=0;i<8&&hovers.length<2;i++)flush();
  assert.equal(hovers.length,2);assert.equal(hovers[1].kind,'face','the deferred pick uses the latest pointer position');
  r.destroy();
});
test('fast picks never back off',()=>{
  const {canvas,stats}=mockCanvas(),hovers=[],r=new Renderer(canvas,{now:()=>0,onHover:h=>hovers.push(plain(h))});
  r.load(withEdge());r.setCamera(defaultCamera);flush();
  const [cx,cy]=edgeMid(r.model,640,480),f=project([-.2,-.2,0],defaultCamera,640,480),move=(x,y)=>stats.listeners.get('pointermove')({pointerId:1,clientX:x,clientY:y,buttons:0});
  move(cx,cy);flush();move(f[0],f[1]);flush();move(cx,cy);flush();assert.equal(hovers.length,3);r.destroy();
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
    test('native declarative exploded playback preserves source mesh, identity and topology owners',()=>{
      const creation=JSON.parse(fs.readFileSync(new URL('../examples/articulated-arm.create.json',import.meta.url),'utf8'));call('cad_create',creation);
      const data=call('cad_query',{document_id:creation.document_id,revision:1,kind:'mesh'}),raw=JSON.stringify(data),full=prepare(data),start={time_s:0,presentation:defaultPresentation(),joints:[]};
      start.presentation.explode.directions=[{part_id:'lever',direction:[1,0,0]}];const end=structuredClone(start);end.time_s=2;end.presentation.explode.distance_mm=20;
      const sample=environment.CadLiveState.sequenceSample({frames:[start,end]},1),shown=presentedModel(full,sample.presentation);
      assert.equal(sample.presentation.explode.distance_mm,10);assert.equal(shown.identity,full.identity);assert.equal(JSON.stringify(data),raw);
      const index=data.mesh.triangle_faces.findIndex(id=>full.geometries.face.get(id).part_id==='lever'),originalVertex=data.mesh.triangles[index][0],presentedVertex=shown.indices[3*index];
      assert.ok(Math.abs(shown.positions[3*presentedVertex]-full.positions[3*originalVertex]-10/full.span)<1e-6);
      assert.equal(shown.geometries.face.get(data.mesh.triangle_faces[index]),full.geometries.face.get(data.mesh.triangle_faces[index]));
      const {canvas,stats}=mockCanvas(),renderer=new Renderer(canvas);renderer.load(data);renderer.setPresentation(sample.presentation);flush();assert.match(renderer.capture(),/^data:image\/png/);renderer.destroy();assert.equal(stats.createdBuffers,stats.deletedBuffers);
    });
    const ringModel={schema_version:1,units:'mm',parameters:{},features:[{id:'outer',type:'cylinder',radius:5,height:10},{id:'inner',type:'cylinder',radius:2,height:10},{id:'ring',type:'cut',left:'outer',right:'inner'},{id:'backing',type:'box',size:[12,12,1]},
      {id:'review',type:'assembly',parts:[{id:'housing',input:'ring',placement:{translation:[0,0,0]}},{id:'backing',input:'backing',placement:{translation:[-6,-6,-3]}}],mates:[]}],output:'review'};
    call('cad_create',{document_id:'section_ring',model:ringModel});
    const ringEvaluation=call('cad_query',{document_id:'section_ring',revision:1,kind:'mesh'}),ringSource=prepare(ringEvaluation);
    const measure=(settings,part_ids)=>call('cad_measure',{...ringSource.identity,query:{action:'section',...sectionGeometry(settings),...(part_ids?{part_ids}:{})}});
    test('actual native annular cap preserves holes, exact area and source picking through its bore',()=>{
      const settings=sectionSettings(5),result=measure(settings),raw=JSON.stringify(ringEvaluation),model=withSection(ringEvaluation,result,settings);
      assert.ok(Math.abs(result.report.area_mm2-21*Math.PI)<1e-6);assert.equal(result.report.regions.length,1);assert.equal(result.report.regions[0].wire_count,2);assert.ok(model.section.indices.length>0);
      const ringPoint=[3/ringSource.span,0,(5-ringSource.center[2])/ringSource.span],borePoint=[0,0,(5-ringSource.center[2])/ringSource.span];
      assert.equal(at(model,ringPoint).section,true);assert.equal(at(model,ringPoint).id,null);
      const bore=at(model,borePoint);assert.ok(bore.id);assert.equal(model.geometries.face.get(bore.id).part_id,'backing');assert.equal(bore.section,undefined);
      const hidden=withSection(ringEvaluation,result,settings,['housing']);assert.equal(at(hidden,ringPoint).section,undefined);assert.equal(hidden.section.indices.length,0);assert.equal(JSON.stringify(ringEvaluation),raw);
      const {canvas,stats}=mockCanvas(),renderer=new Renderer(canvas);renderer.load(ringEvaluation);renderer.setPresentation(settings);renderer.setSection(result);flush();assert.ok(renderer.resources.capFaceCount>0&&renderer.resources.capEdgeCount>0);assert.match(renderer.capture(),/^data:image\/png/);renderer.destroy();assert.equal(stats.createdBuffers,stats.deletedBuffers);
    });
    test('native exploded section aligns scoped source caps with displayed plane and canonical overrides',()=>{
      const settings=sectionSettings(7);settings.explode={distance_mm:2,directions:[{part_id:'housing',direction:[0,0,1]},{part_id:'backing',direction:[0,0,-1]}]};
      const result=measure(settings,['housing']),model=withSection(ringEvaluation,result,settings);assert.equal(result.report.coverage,'explicit_leaf_subset');assert.ok(Math.abs(result.report.sections[0].source_plane_offset_mm-5)<1e-8);
      for(let i=2;i<model.section.positions.length;i+=3)assert.ok(Math.abs(model.section.positions[i]-(7-ringSource.center[2])/ringSource.span)<1e-8);
      const reordered=structuredClone(settings);reordered.explode.directions.reverse();reordered.clip.keep='positive';assert.equal(validateSection(result,ringSource,reordered),result);
      const {canvas}=mockCanvas(),renderer=new Renderer(canvas);renderer.load(ringEvaluation);renderer.setPresentation(settings);renderer.setSection(result);const changed=structuredClone(settings);changed.explode.distance_mm=3;renderer.setPresentation(changed);assert.equal(renderer.sectionResult,null);assert.equal(renderer.resources.capFaces,null);renderer.destroy();
    });
    test('actual native tangent and outside sections never invent filled material caps',()=>{
      for(const settings of [sectionSettings(1e12),{clip:{normal:[1,0,0],offset_mm:5,keep:'negative'},explode:{distance_mm:0,directions:[]}}]) {
        const result=measure(settings,['housing']),model=withSection(ringEvaluation,result,settings);assert.equal(result.report.regions.length,0);assert.equal(model.section.indices.length,0);
        if(settings.clip.normal[0]===1){assert.equal(result.report.status,'tangent');assert.ok(result.report.curves.length>0);assert.ok(model.section.edges.length>0);assert.ok(result.report.boundary_length_mm>9.99);}else assert.equal(result.report.status,'empty');
      }
    });
    test('actual native point tangency remains contact data with no invented cap or curve',()=>{
      const n=1/Math.sqrt(3),settings={clip:{normal:[n,n,n],offset_mm:10*n,keep:'negative'},explode:{distance_mm:0,directions:[]}},result=measure(settings,['backing']),model=withSection(ringEvaluation,result,settings);
      assert.equal(result.report.status,'tangent');assert.equal(result.report.regions.length,0);assert.equal(result.report.curves.length,0);assert.equal(result.report.sections[0].contact_points.length,1);assert.equal(model.section.indices.length,0);assert.equal(model.section.edges.length,0);
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
