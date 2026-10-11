/* Dependency-free CAD viewport. All topology IDs belong to one evaluation. */
(() => {
  'use strict';
  const MAX_VERTICES=200000, MAX_TRIANGLES=200000, MAX_ENTITIES=10000, MAX_EDGE_POINTS=200000;
  const MAX_PIXELS=4000000, PICK_BUDGET=2000000, DEPTH_EPS=1e-8;
  const EDGE_TOLERANCE=9, AUTO_EDGE_TOLERANCE=6;
  // Hover repeats the click hit test on every pointer move. Past this cost it backs off so huge models stay responsive.
  const HOVER_SLOW_MS=8, HOVER_BACKOFF=4;
  const CLIP_EPS=1e-7;
  const DEFAULT_CAMERA={yaw:-.65,pitch:.6,zoom:1,pan:[0,0]};
  const cloneCamera=c=>({yaw:c.yaw,pitch:c.pitch,zoom:c.zoom,pan:[...c.pan],...(c.projection?{projection:c.projection,lens_mm:c.lens_mm||50,distance:c.distance||3}: {})});
  const finitePoint=p=>Array.isArray(p)&&p.length===3&&p.every(n=>Number.isFinite(n)&&Math.abs(n)<=1e12);
  const fail=message=>{throw new Error(message);};
  function validate(d) {
    if(!d||d.schema_version!==1||!d.mesh||!d.topology||!d.summary)fail('Unsupported CAD evaluation.');
    for(const key of ['document_id','evaluation_id','feature_id'])
      if(typeof d[key]!=='string'||!d[key].length||d[key].length>256)fail('Missing evaluation identity.');
    if(!Number.isSafeInteger(d.revision)||d.revision<0)fail('Invalid revision identity.');
    const m=d.mesh,t=d.topology,b=d.summary.bounds_mm;
    if(!b||!finitePoint(b.min)||!finitePoint(b.max)||b.min.some((v,i)=>v>b.max[i])||Math.max(...b.max.map((v,i)=>v-b.min[i]))<=0)fail('Invalid model bounds.');
    if(m.linear_deflection_mm!==undefined&&!(typeof m.linear_deflection_mm==='number'&&Number.isFinite(m.linear_deflection_mm)&&m.linear_deflection_mm>=0&&m.linear_deflection_mm<=1e6))fail('Invalid mesh deflection.');
    for(const section of [m,t]) {
      if(section.schema_version!==1||section.feature_id!==d.feature_id||section.selection_lifetime!=='evaluation')fail('Mesh and topology must share the displayed feature and evaluation.');
    }
    const inside=p=>finitePoint(p)&&p.every((v,i)=>v>=b.min[i]-1e-5&&v<=b.max[i]+1e-5);
    if(!Array.isArray(m.positions)||!m.positions.length||m.positions.length>MAX_VERTICES||!m.positions.every(inside))fail('Invalid or oversized vertex payload.');
    if(!Array.isArray(m.triangles)||!m.triangles.length||m.triangles.length>MAX_TRIANGLES||!m.triangles.every(v=>Array.isArray(v)&&v.length===3&&v.every(i=>Number.isInteger(i)&&i>=0&&i<m.positions.length)))fail('Invalid or oversized triangle payload.');
    if(!Array.isArray(t.faces)||!Array.isArray(t.edges)||t.faces.length+t.edges.length>MAX_ENTITIES)fail('Topology exceeds its entity limit.');
    const faces=new Set(),edges=new Set();
    const parts=d.summary.assembly?.parts,partIds=new Set();
    if(parts!==undefined) {
      if(!Array.isArray(parts)||!parts.length||parts.length>1024)fail('Invalid assembly parts.');
      for(const part of parts) {
        if(!part||typeof part.id!=='string'||!/^[A-Za-z][A-Za-z0-9_-]{0,63}(\/[A-Za-z][A-Za-z0-9_-]{0,63}){0,7}$/.test(part.id)||partIds.has(part.id)||!part.bounds_mm||!inside(part.bounds_mm.min)||!inside(part.bounds_mm.max)||part.bounds_mm.min.some((v,i)=>v>part.bounds_mm.max[i]))fail('Invalid assembly part bounds or identity.');
        partIds.add(part.id);
      }
      const tree=d.summary.assembly.tree;
      if(tree!==undefined) {
        if(!Array.isArray(tree)||!tree.length||tree.length>8192)fail('Invalid assembly hierarchy.');
        const nodes=new Map(),leaves=new Set(),populated=new Set();
        const identifier=value=>typeof value==='string'&&/^[A-Za-z][A-Za-z0-9_-]{0,63}$/.test(value);
        for(const node of tree) {
          if(!node||typeof node.id!=='string'||!/^[A-Za-z][A-Za-z0-9_-]{0,63}(\/[A-Za-z][A-Za-z0-9_-]{0,63}){0,7}$/.test(node.id)||nodes.has(node.id)||!identifier(node.input)||!identifier(node.assembly_id)||!['assembly','part'].includes(node.kind))fail('Invalid assembly hierarchy node.');
          const parent=node.id.includes('/')?node.id.slice(0,node.id.lastIndexOf('/')):'';
          if(node.parent_id!==parent||(parent&&nodes.get(parent)?.kind!=='assembly'))fail('Invalid assembly hierarchy parent.');
          nodes.set(node.id,node);if(parent)populated.add(parent);
          if(node.kind==='part')leaves.add(node.id);
        }
        if(leaves.size!==partIds.size||[...partIds].some(id=>!leaves.has(id))||[...nodes].some(([id,node])=>node.kind==='assembly'&&!populated.has(id)))fail('Assembly hierarchy must cover every displayed part.');
      }
    }
    for(const [entities,ids,prefix] of [[t.faces,faces,'face'],[t.edges,edges,'edge']])
      for(const entity of entities) {
        if(!entity||typeof entity.id!=='string'||entity.id.length>32||!new RegExp('^'+prefix+'-[1-9][0-9]*$').test(entity.id)||ids.has(entity.id))fail('Invalid or duplicate topology identity.');
        if(parts&&!partIds.has(entity.part_id))fail('Assembly topology must identify its owning part.');
        ids.add(entity.id);
      }
    if(!Array.isArray(m.triangle_faces)||m.triangle_faces.length!==m.triangles.length||!m.triangle_faces.every(id=>faces.has(id))||new Set(m.triangle_faces).size!==faces.size)fail('Triangle mapping does not cover the displayed faces.');
    if(!Array.isArray(m.edges)||m.edges.length!==edges.size)fail('Polyline mapping does not cover the displayed edges.');
    let count=0;const seen=new Set(),edgeOwners=new Map(t.edges.map(edge=>[edge.id,edge.part_id]));
    for(const edge of m.edges) {
      if(!edge||!edges.has(edge.id)||seen.has(edge.id)||!Array.isArray(edge.points))fail('Invalid edge mapping.');
      if(parts&&edge.part_id!==edgeOwners.get(edge.id))fail('Assembly edge ownership does not match topology.');
      count+=edge.points.length;if(count>MAX_EDGE_POINTS)fail('Edge payload exceeds its point limit.');
      if(!edge.points.every(inside))fail('Invalid edge coordinates.');
      seen.add(edge.id);
    }
    return d;
  }
  function annotations(notes,source,{draft=false}={}) {
    const closed=(v,keys)=>v&&typeof v==='object'&&!Array.isArray(v)&&Object.keys(v).length===keys.length&&keys.every(k=>Object.hasOwn(v,k));
    const id=v=>typeof v==='string'&&/^[A-Za-z][A-Za-z0-9_-]{0,63}$/.test(v);
    const owner=v=>typeof v==='string'&&/^[A-Za-z][A-Za-z0-9_-]{0,63}(\/[A-Za-z][A-Za-z0-9_-]{0,63}){0,7}$/.test(v);
    if(!Array.isArray(notes)||notes.length>32)fail('A view stores up to 32 review annotations.');
    const ids=new Set();
    for(const note of notes) {
      if(!closed(note,['id','text','document_id','revision','evaluation_id','feature_id','anchor_lifetime','coordinate_space','status','anchor'])||!id(note.id)||ids.has(note.id)||!id(note.document_id)||!id(note.evaluation_id)||!id(note.feature_id)||!Number.isSafeInteger(note.revision)||note.revision<1||note.anchor_lifetime!=='evaluation'||note.coordinate_space!=='committed_source_pose'||!['current','retired'].includes(note.status)||typeof note.text!=='string'||(!note.text.length||/^[ \t\n]*$/.test(note.text))||new TextEncoder().encode(note.text).length>512||/[\u0000-\u0008\u000b-\u001f\u007f]/.test(note.text))fail('Invalid bounded review annotation.');
      ids.add(note.id);const a=note.anchor,entity=a?.kind==='entity';
      if(!closed(a,['kind','part_id','point_mm','position_semantics',...(entity?['reference']:[])])||!['model','part','entity'].includes(a.kind)||!finitePoint(a.point_mm)||a.position_semantics!==(entity?'entity_center':'bounds_center')||(a.kind==='model'?a.part_id!==null:a.kind==='part'?!owner(a.part_id):a.part_id!==null&&!owner(a.part_id)))fail('Invalid inspection anchor.');
      if(entity) {
        const r=a.reference;
        if(!closed(r,['document_id','revision','evaluation_id','feature_id','kind','entity_id'])||!['face','edge'].includes(r.kind)||!id(r.entity_id)||['document_id','revision','evaluation_id','feature_id'].some(k=>r[k]!==note[k]))fail('Inspection evidence must retain its own source identity.');
      }
      if(note.status==='current') {
        if(!source||draft||Object.entries(source.identity).some(([k,v])=>note[k]!==v)||a.part_id!==null&&!source.partBounds.has(a.part_id))fail('Current annotation belongs to another evaluation.');
        const p=a.point_mm.map((v,i)=>(v-source.center[i])/source.span),bounds=a.part_id===null?source.bounds:source.partBounds.get(a.part_id);
        if(p.some((v,i)=>v<bounds.min[i]-1e-5||v>bounds.max[i]+1e-5))fail('Inspection center is outside its source bounds.');
        if(entity){const g=source.geometries[a.reference.kind].get(a.reference.entity_id);if(!g||(g.part_id??null)!==a.part_id||!finitePoint(g.center_mm)||g.center_mm.some((v,i)=>Math.abs(v-a.point_mm[i])>1e-6))fail('Inspection anchor differs from resolved source center.');}
        else if(p.some((v,i)=>Math.abs(v-(bounds.min[i]+bounds.max[i])/2)>1e-6))fail('Inspection anchor differs from native bounds center.');
      }
    }
    return JSON.parse(JSON.stringify(notes));
  }
  function annotationLayout(notes,source,settings,c,width,height,hiddenPartIds=[]) {
    if(!source)return [];
    const offsets=presentationOffsets(source,settings),hidden=new Set(hiddenPartIds),clip=settings.clip;
    const layout=[];
    notes.forEach((note,index)=>{
      const a=note.anchor;if(note.status!=='current'||hidden.has(a.part_id))return;
      const shift=offsets.get(a.part_id)||[0,0,0],p=a.point_mm.map((v,i)=>(v-source.center[i])/source.span+shift[i]);
      if(clip&&(dot(clip.normal,p)-(clip.offset_mm-dot(clip.normal,source.center))/source.span)*(clip.keep==='positive'?1:-1)<-CLIP_EPS)return;
      const screen=project(p,c,width,height);if(screen[0]<0||screen[1]<0||screen[0]>width||screen[1]>height)return;
      layout.push({id:note.id,number:index+1,text:note.text,x:screen[0],y:screen[1],point:p});
    });return layout;
  }
  function annotationLabelBounds(pin,textWidth,width,height) {
    const w=Math.min(Math.max(1,width-8),textWidth+12),clamp=(v,min,max)=>Math.max(min,Math.min(max,v));
    let x,y=clamp(pin.y-10,4,height-24);
    if(pin.x+15+w<=width-4)x=pin.x+15;
    else if(pin.x-15-w>=4)x=pin.x-15-w;
    else {
      x=clamp(pin.x-w/2,4,width-w-4);
      y=pin.y+35<=height-4?pin.y+15:pin.y-35;
      y=clamp(y,4,height-24);
    }
    return {x,y,width:w,height:20};
  }
  // Artifact labels are review-local mesh groups/curves, never native topology.
  function validateArtifact(d) {
    if(!d||d.schema_version!==1||d.kind!=='artifact'||d.read_only!==true||d.document_id!==null||d.revision!==null||d.feature_id!==null||d.draft!==false||d.selection_lifetime!=='review_sha256'||d.mesh||d.topology)fail('Unsupported read-only artifact review.');
    const a=d.artifact,g=d.artifact_geometry,s=d.summary,hash=/^[0-9a-f]{64}$/;
    const posed=/^apose_[0-9a-f]{58}$/.test(d.evaluation_id),pose=d.artifact_pose;
    if(posed){if(!pose||typeof pose!=='object'||Array.isArray(pose)||Object.keys(pose).some(k=>!['joints','animation','time_seconds'].includes(k))||!['glb','urdf','sdf','srdf'].includes(a?.source?.format))fail('Invalid artifact pose qualification.');
      if('joints'in pose&&(!pose.joints||typeof pose.joints!=='object'||Array.isArray(pose.joints)||Object.keys(pose.joints).length>1024||Object.entries(pose.joints).some(([k,v])=>!k.length||k.length>4096||!Number.isFinite(v)||Math.abs(v)>1e6)))fail('Invalid artifact joint coordinates.');
      if('animation'in pose&&(!Number.isInteger(pose.animation)||pose.animation<0||pose.animation>255)||'time_seconds'in pose&&(!Number.isFinite(pose.time_seconds)||pose.time_seconds<0||pose.time_seconds>1e9))fail('Invalid artifact animation time.');
      if(a.source.format==='glb'?('joints'in pose||('time_seconds'in pose&&!('animation'in pose))):('animation'in pose||'time_seconds'in pose))fail('Artifact pose does not match its source format.');}
    if(!a||!hash.test(a.review_sha256)||!a.source||!hash.test(a.source.sha256)||(!posed&&d.evaluation_id!=='artifact_'+a.review_sha256.slice(0,55))||JSON.stringify(a.summary)!==JSON.stringify(posed?{...s,bounds:a.summary.bounds}:s))fail('Invalid artifact source/review identity.');
    const representations={step:'exact_brep_import',stl:'triangle_mesh','3mf':'triangle_mesh',glb:'triangle_mesh',dxf:'drawing_curves',urdf:'robot_description',sdf:'robot_description',srdf:'robot_semantics'};
    if(!s||s.editable!==false||s.units!=='mm'||s.representation!==representations[a.source.format]||!Array.isArray(d.limitations)||!d.metadata||typeof d.metadata!=='object')fail('Invalid artifact review qualification.');
    if(!g||!['positions','triangles','triangle_groups','groups','polylines'].every(k=>Array.isArray(g[k]))||g.positions.length>MAX_VERTICES||g.triangles.length>MAX_TRIANGLES||g.groups.length>MAX_ENTITIES||g.polylines.length>MAX_ENTITIES)fail('Invalid or oversized artifact geometry.');
    const b=s.bounds,points=[...g.positions];let curvePoints=0;
    const label=(v,prefix,i)=>v&&v.id===prefix+'-'+(i+1)&&v.editable===false&&typeof v.source_ref==='string'&&v.source_ref.length>0&&v.source_ref.length<=4096;
    if(!g.groups.every((v,i)=>label(v,'artifact',i))||!g.polylines.every((v,i)=>label(v,'curve',i)&&Array.isArray(v.points)&&v.points.length>=2))fail('Invalid artifact mesh/curve labels.');
    for(const line of g.polylines){curvePoints+=line.points.length;if(curvePoints>MAX_EDGE_POINTS)fail('Artifact curve payload exceeds its point budget.');for(const p of line.points)points.push(p);}
    if(!points.every(p=>finitePoint(p)&&p.every(v=>Math.abs(v)<=1e9)))fail('Invalid artifact coordinates.');
    if(points.length){if(!b||!finitePoint(b.min)||!finitePoint(b.max)||b.min.some((v,i)=>v>b.max[i])||points.some(p=>p.some((v,i)=>v<b.min[i]-1e-6||v>b.max[i]+1e-6)))fail('Invalid artifact bounds.');}
    else if(b!==null)fail('A semantic review cannot invent geometry bounds.');
    if(!g.triangles.every(t=>Array.isArray(t)&&t.length===3&&new Set(t).size===3&&t.every(i=>Number.isSafeInteger(i)&&i>=0&&i<g.positions.length)))fail('Invalid artifact triangles.');
    const ids=new Set(g.groups.map(v=>v.id));if(g.triangle_groups.length!==g.triangles.length||!g.triangle_groups.every(id=>ids.has(id))||new Set(g.triangle_groups).size!==ids.size)fail('Artifact triangles do not cover their mesh groups.');
    for(const [key,value] of [['vertices',g.positions.length],['triangles',g.triangles.length],['groups',g.groups.length],['curves',g.polylines.length]])if(s[key]!==value)fail('Artifact summary differs from displayed geometry.');
    return d;
  }
  function prepareArtifact(d) {
    validateArtifact(d);const g=d.artifact_geometry,b=d.summary.bounds,frame=d.artifact.summary.bounds||b;
    const span=frame?Math.max(1e-9,...frame.max.map((v,i)=>v-frame.min[i])):1,center=frame?frame.min.map((v,i)=>v+(frame.max[i]-v)/2):[0,0,0],normalized=p=>p.map((v,i)=>(v-center[i])/span);
    const positions=Float64Array.from(g.positions.flatMap(normalized)),indices=Uint32Array.from(g.triangles.flat()),faces=g.groups.map(v=>v.id),faceNumbers=new Map(faces.map((id,i)=>[id,i+1]));
    const edges=g.polylines.filter(v=>!v.text).map(v=>({id:v.id,points:Float64Array.from(v.points.flatMap(normalized))}));
    return {read_only:true,identity:{review_sha256:d.artifact.review_sha256,evaluation_id:d.evaluation_id,read_only:true},positions,indices,faces,faceNumbers,triangleFaces:[...g.triangle_groups],edges,
      geometries:{face:new Map(g.groups.map(v=>[v.id,v])),edge:new Map(g.polylines.map(v=>[v.id,v]))},tree:indices.length?buildTree(positions,indices):{nodes:[],order:new Uint32Array()},partBounds:new Map(),center,span,bounds:b?{min:normalized(b.min),max:normalized(b.max)}:null,hiddenPartIds:[],edgeAllowance:0};
  }
  function artifactOverlay(model,c,width,height) {
    const texts=[],fills=[];if(!model?.read_only)return {texts,fills};const norm=p=>p.map((v,i)=>(v-model.center[i])/model.span),screen=p=>project(norm(p),c,width,height);
    for(const line of model.geometries.edge.values()){
      if(line.text){const origin=line.points[0],anchor=screen(origin),end=screen(line.points[1]),m=line.text_transform_mm,h=line.height_mm||1,up=m?.length===16?[m[1]*h,m[5]*h,m[9]*h]:[0,h,0],top=screen(origin.map((v,i)=>v+up[i]));
        const align=line.attachment?['left','center','right'][(line.attachment-1)%3]:[0,3,5].includes(line.horizontal_alignment||0)?'left':[1,4].includes(line.horizontal_alignment)?'center':'right',baseline=line.attachment?['top','middle','bottom'][Math.floor((line.attachment-1)/3)]:['alphabetic','bottom','middle','top'][line.vertical_alignment||0];
        texts.push({align,baseline,text:line.text,x:anchor[0],y:anchor[1],angle:Math.atan2(end[1]-anchor[1],end[0]-anchor[0]),font_px:Math.max(.2,Math.hypot(top[0]-anchor[0],top[1]-anchor[1])),width_factor:line.width_factor||1,color:line.color||[.2,.25,.3]});}
      if(line.fill&&Array.isArray(line.hatch_loops))fills.push({loops:line.hatch_loops.map(loop=>(loop.points||loop).map(screen)),color:line.color||[.4,.5,.65]});
    }return {texts,fills};
  }
  function paintArtifactOverlay(ctx,layout) {
    const color=v=>'rgb('+v.slice(0,3).map(n=>Math.round(n*255)).join(',')+')';
    for(const fill of layout.fills){ctx.beginPath();for(const loop of fill.loops){if(!loop.length)continue;ctx.moveTo(loop[0][0],loop[0][1]);for(const p of loop.slice(1))ctx.lineTo(p[0],p[1]);ctx.closePath();}ctx.fillStyle=color(fill.color);ctx.globalAlpha=.65;ctx.fill('evenodd');ctx.globalAlpha=1;}
    for(const text of layout.texts){ctx.save();ctx.translate(text.x,text.y);ctx.rotate(text.angle);ctx.scale(text.width_factor,1);ctx.font=text.font_px+'px sans-serif';ctx.fillStyle=color(text.color);ctx.textBaseline=text.baseline||'alphabetic';ctx.textAlign=text.align||'left';const lines=text.text.split('\n'),shift=text.baseline==='middle'?-(lines.length-1)*text.font_px*.6:text.baseline==='bottom'?-(lines.length-1)*text.font_px*1.2:0;lines.forEach((line,i)=>ctx.fillText(line,0,shift+i*text.font_px*1.2));ctx.restore();}
  }
  function camera(value) {
    if(!value||!['yaw','pitch','zoom'].every(k=>Number.isFinite(value[k]))||!Array.isArray(value.pan)||value.pan.length!==2||!value.pan.every(Number.isFinite))fail('Invalid camera state.');
    return {yaw:Math.atan2(Math.sin(value.yaw),Math.cos(value.yaw)),pitch:Math.max(-Math.PI/2,Math.min(Math.PI/2,value.pitch)),zoom:Math.max(.05,Math.min(50,value.zoom)),pan:value.pan.map(n=>Math.max(-20,Math.min(20,n))),...(value.projection?{projection:value.projection,lens_mm:value.lens_mm||50,distance:value.distance||3}:{})};
  }
  function basis(c) {
    const cy=Math.cos(c.yaw),sy=Math.sin(c.yaw),cp=Math.cos(c.pitch),sp=Math.sin(c.pitch);
    return [[cy,-sy,0],[sy*sp,cy*sp,cp],[sy*cp,cy*cp,-sp]];
  }
  const dot=(a,b)=>a[0]*b[0]+a[1]*b[1]+a[2]*b[2];
  function project(p,c,width,height) {
    const b=basis(c),size=Math.min(width,height),scale=size*.68*c.zoom;
    const z=dot(b[2],p),d=c.distance||3,f=c.projection==='perspective'?d/(d+z):1;
    return [width/2+dot(b[0],p)*scale*f+c.pan[0]*size,height/2-dot(b[1],p)*scale*f+c.pan[1]*size,z,...(c.projection==='perspective'?[1/(d+z)]:[])];
  }
  function screenRay(x,y,c,width,height,extent=2) {
    const b=basis(c),size=Math.min(width,height),scale=size*.68*c.zoom;
    const u=(x-width/2-c.pan[0]*size)/scale,v=(height/2+c.pan[1]*size-y)/scale;
    if(c.projection==='perspective'){const d=c.distance||3;return {origin:b[2].map(n=>-d*n),direction:b[2].map((n,i)=>n+b[0][i]*u/d+b[1][i]*v/d),far:d+extent,offset:d};}
    return {origin:b[0].map((n,i)=>n*u+b[1][i]*v-extent*b[2][i]),direction:b[2],far:extent*2,offset:extent};
  }
  const STANDARD_VIEWS=Object.freeze({iso:[-.65,.6],top:[0,Math.PI/2],bottom:[0,-Math.PI/2],front:[0,0],back:[Math.PI,0],right:[-Math.PI/2,0],left:[Math.PI/2,0]});
  const DIGIT_VIEWS=Object.freeze({1:'iso',2:'front',3:'back',4:'top',5:'bottom',6:'right',7:'left'});
  const defaultNow=()=>globalThis.performance?.now?.()??Date.now();
  const defaultReducedMotion=()=>!!globalThis.matchMedia?.('(prefers-reduced-motion: reduce)')?.matches;
  const rgb=(r,g,b)=>[r/255,g/255,b/255];
  const defaultTheme=()=>({background:{top:rgb(240,240,240),bottom:rgb(240,240,240)},line:rgb(92,107,123),select:rgb(0,122,255),hover:rgb(110,173,255)});
  const darkTheme=()=>({background:{top:rgb(46,53,61),bottom:rgb(25,30,36)},line:rgb(34,41,50),select:rgb(90,162,255),hover:rgb(140,190,255)});
  function themeValue(value) {
    const unit=v=>Array.isArray(v)&&v.length===3&&v.every(n=>Number.isFinite(n)&&n>=0&&n<=1);
    if(!value||!value.background||![value.background.top,value.background.bottom,value.line,value.select,value.hover].every(unit))fail('Invalid viewer theme.');
    return {background:{top:[...value.background.top],bottom:[...value.background.bottom]},line:[...value.line],select:[...value.select],hover:[...value.hover]};
  }
  function stageSettings(model) {
    if (!model?.bounds) return { floor: -.5, step: .1, origin: [0,0,-.5], shift: [0,0] };
    const raw = model.span / 8, magnitude = Math.pow(10, Math.floor(Math.log10(raw))), fraction = raw / magnitude;
    const step = (fraction <= 1 ? 1 : fraction <= 2 ? 2 : fraction <= 5 ? 5 : 10) * magnitude / model.span;
    return { floor: model.bounds.min[2] - .0001, step, origin: [model.bounds.min[0] - .16, model.bounds.min[1] - .16, model.bounds.min[2]],
      shift: model.center.slice(0,2).map(n => (-n / model.span) % step) };
  }
  const wrapAngle=a=>Math.atan2(Math.sin(a),Math.cos(a));
  const easeOut=t=>1-Math.pow(1-Math.max(0,Math.min(1,t)),3);
  function viewFromDirection(d) {
    if(!Array.isArray(d)||d.length!==3||!d.every(Number.isFinite))fail('View direction must be three finite numbers.');
    const n=Math.hypot(...d);if(!(n>0))fail('View direction must be nonzero.');
    const c=d.map(v=>v/n),flat=Math.hypot(c[0],c[1]);
    return {yaw:flat<1e-9?0:Math.atan2(-c[0],-c[1]),pitch:Math.asin(Math.max(-1,Math.min(1,c[2])))};
  }
  function lerpCamera(a,b,t) {
    const k=Math.max(0,Math.min(1,t));
    return {yaw:a.yaw+wrapAngle(b.yaw-a.yaw)*k,pitch:a.pitch+(b.pitch-a.pitch)*k,zoom:a.zoom*Math.pow(b.zoom/a.zoom,k),pan:[a.pan[0]+(b.pan[0]-a.pan[0])*k,a.pan[1]+(b.pan[1]-a.pan[1])*k]};
  }
  // Orthographic: keeping the world point under (x,y) fixed only moves the pan.
  function zoomAbout(c,factor,x,y,width,height) {
    const zoom=Math.max(.05,Math.min(50,c.zoom*factor)),k=zoom/c.zoom,size=Math.min(width,height);
    return {yaw:c.yaw,pitch:c.pitch,zoom,pan:[c.pan[0]*k+(x-width/2)/size*(1-k),c.pan[1]*k+(y-height/2)/size*(1-k)]};
  }
  // `insets` are CSS pixels of the canvas covered by floating chrome; the model is
  // fitted and centred in what remains.
  function fitCamera(c,bounds,width,height,insets) {
    const left=insets?.left||0,right=insets?.right||0,top=insets?.top||0,bottom=insets?.bottom||0;
    const usableWidth=Math.max(1,width-left-right),usableHeight=Math.max(1,height-top-bottom);
    const b=basis(c),corners=[];
    for(const x of [bounds.min[0],bounds.max[0]])for(const y of [bounds.min[1],bounds.max[1]])for(const z of [bounds.min[2],bounds.max[2]]){const p=[x,y,z],d=c.distance||3,f=c.projection==='perspective'?d/(d+dot(b[2],p)):1;corners.push([dot(b[0],p)*f,dot(b[1],p)*f]);}
    const low=[0,1].map(a=>Math.min(...corners.map(p=>p[a]))),high=[0,1].map(a=>Math.max(...corners.map(p=>p[a]))),size=Math.min(width,height);
    const zoom=Math.max(.05,Math.min(50,Math.min(usableWidth*.86/(Math.max(1e-12,high[0]-low[0])*.68*size),usableHeight*.86/(Math.max(1e-12,high[1]-low[1])*.68*size))));
    return {yaw:c.yaw,pitch:c.pitch,zoom,pan:[-(low[0]+high[0])/2*.68*zoom+(left-right)/2/size,(low[1]+high[1])/2*.68*zoom+(top-bottom)/2/size]};
  }
  // Extent of one face or edge in normalized model units, padded so a straight
  // edge or flat face still frames at a sensible zoom.
  function entityBounds(model,kind,id) {
    if(!model||!model.geometries[kind]?.has(id))return null;
    const min=[Infinity,Infinity,Infinity],max=[-Infinity,-Infinity,-Infinity];
    const add=(values,i)=>{for(let k=0;k<3;k++){const v=values[i+k];if(v<min[k])min[k]=v;if(v>max[k])max[k]=v;}};
    if(kind==='edge') {
      const edge=model.edges.find(e=>e.id===id);if(!edge)return null;
      for(let i=0;i<edge.points.length;i+=3)add(edge.points,i);
    } else {
      const owners=model.triangleFaces;
      for(let t=0;t<model.indices.length/3;t++){if(owners[t]!==id)continue;for(let j=0;j<3;j++)add(model.positions,3*model.indices[3*t+j]);}
    }
    if(min[0]===Infinity)return null;
    for(let k=0;k<3;k++){const pad=Math.max(0,.1-(max[k]-min[k]))/2;min[k]-=pad;max[k]+=pad;}
    return {min,max};
  }
  function vertex(positions,i) {return [positions[i*3],positions[i*3+1],positions[i*3+2]];}
  function expandBits(n) {n=(n|(n<<16))&0x030000FF;n=(n|(n<<8))&0x0300F00F;n=(n|(n<<4))&0x030C30C3;return (n|(n<<2))&0x09249249;}
  // A fixed-size Morton tree makes ray queries independent of screen size.
  function buildTree(positions,indices) {
    const count=indices.length/3,order=Uint32Array.from({length:count},(_,i)=>i),codes=new Uint32Array(count),nodes=[];
    for(let i=0;i<count;i++) {
      const center=[0,0,0];
      for(let j=0;j<3;j++)for(let a=0;a<3;a++)center[a]+=positions[3*indices[3*i+j]+a]/3;
      const q=center.map(n=>Math.max(0,Math.min(1023,Math.floor((n+.5)*1023))));
      codes[i]=expandBits(q[0])|(expandBits(q[1])<<1)|(expandBits(q[2])<<2);
    }
    order.sort((a,b)=>codes[a]-codes[b]);
    const build=(start,end)=>{
      const node={min:[Infinity,Infinity,Infinity],max:[-Infinity,-Infinity,-Infinity],start,count:end-start,left:-1,right:-1},id=nodes.length;nodes.push(node);
      if(end-start>16) {
        const middle=(start+end)>>>1;node.left=build(start,middle);node.right=build(middle,end);
        for(const child of [nodes[node.left],nodes[node.right]])for(let a=0;a<3;a++){node.min[a]=Math.min(node.min[a],child.min[a]);node.max[a]=Math.max(node.max[a],child.max[a]);}
      } else for(let i=start;i<end;i++)for(let j=0;j<3;j++)for(let a=0;a<3;a++) {
        const value=positions[3*indices[3*order[i]+j]+a];node.min[a]=Math.min(node.min[a],value);node.max[a]=Math.max(node.max[a],value);
      }
      return id;
    };
    build(0,count);return {nodes,order};
  }
  function prepare(d) {
    validate(d);
    const bounds=d.summary.bounds_mm,span=Math.max(...bounds.max.map((v,i)=>v-bounds.min[i])),center=bounds.min.map((v,i)=>v+(bounds.max[i]-v)/2);
    const normalized=p=>p.map((v,i)=>(v-center[i])/span),positions=new Float64Array(d.mesh.positions.length*3),indices=new Uint32Array(d.mesh.triangles.length*3);
    d.mesh.positions.forEach((p,i)=>positions.set(normalized(p),i*3));d.mesh.triangles.forEach((t,i)=>indices.set(t,i*3));
    const faces=d.topology.faces.map(f=>f.id),faceNumbers=new Map(faces.map((id,i)=>[id,i+1]));
    const edges=d.mesh.edges.map(e=>({id:e.id,points:Float64Array.from(e.points.flatMap(normalized))}));
    const geometries={face:new Map(d.topology.faces.map(f=>[f.id,f])),edge:new Map(d.topology.edges.map(e=>[e.id,e]))};
    const identity={document_id:d.document_id,revision:d.revision,evaluation_id:d.evaluation_id,feature_id:d.feature_id};
    const partBounds=new Map((d.summary.assembly?.parts||[]).map(part=>[part.id,{min:normalized(part.bounds_mm.min),max:normalized(part.bounds_mm.max)}]));
    // Edge polylines and face triangulations are sampled independently, each
    // within the kernel's linear deflection of the exact geometry. A visible edge
    // sample may therefore sit up to twice that distance behind its own faces.
    // Pick depth is in model units over the bounds span (orthographic: zoom and
    // pan scale only the screen), so the conversion to ray depth is 1/span.
    const edgeAllowance=2*(d.mesh.linear_deflection_mm??0)/span;
    return {identity,positions,indices,faces,faceNumbers,triangleFaces:[...d.mesh.triangle_faces],edges,geometries,tree:buildTree(positions,indices),partBounds,center,span,
      bounds:{min:normalized(bounds.min),max:normalized(bounds.max)},hiddenPartIds:[],edgeAllowance};
  }
  const defaultPresentation=()=>({clip:null,explode:{distance_mm:0,directions:[]}});
  function presentation(value,source,{prune=false}={}) {
    const keys=(v,expected)=>v&&typeof v==='object'&&!Array.isArray(v)&&Object.keys(v).length===expected.length&&expected.every(k=>Object.hasOwn(v,k));
    const unit=v=>finitePoint(v)&&v.every(n=>Math.abs(n)<=1)&&Math.abs(dot(v,v)-1)<=1e-6;
    if(!keys(value,['clip','explode']))fail('Invalid presentation state.');
    if(value.clip!==null&&(!keys(value.clip,['normal','offset_mm','keep'])||!unit(value.clip.normal)||!Number.isFinite(value.clip.offset_mm)||Math.abs(value.clip.offset_mm)>1e12||!['positive','negative'].includes(value.clip.keep)))fail('Invalid clipping plane.');
    const e=value.explode;
    if(!keys(e,['distance_mm','directions'])||!Number.isFinite(e.distance_mm)||e.distance_mm<0||e.distance_mm>1e6||!Array.isArray(e.directions)||e.directions.length>1024)fail('Invalid exploded presentation.');
    const seen=new Set(),directions=[];
    for(const item of e.directions) {
      if(!keys(item,['part_id','direction'])||typeof item.part_id!=='string'||!unit(item.direction)||seen.has(item.part_id))fail('Invalid exploded occurrence direction.');
      seen.add(item.part_id);
      if(!source.partBounds.has(item.part_id)){if(prune)continue;fail('Exploded direction must name a current leaf occurrence.');}
      directions.push({part_id:item.part_id,direction:[...item.direction]});
    }
    if(e.distance_mm>0&&!source.partBounds.size&&!prune)fail('Load an assembly before exploding parts.');
    return {clip:value.clip?{normal:[...value.clip.normal],offset_mm:value.clip.offset_mm,keep:value.clip.keep}:null,
      explode:{distance_mm:source.partBounds.size?e.distance_mm:0,directions}};
  }
  // Only geometry inputs qualify a native section: changing the kept side
  // changes its outward normal, while a zero explosion has no displacement.
  const RENDER_MODES=['solid','render','xray','hidden_line','wireframe'];
  const FINISHES=['matte','satin','glossy','metal'];
  function material(value={}) {
    if(!value||typeof value!=='object'||Array.isArray(value)||Object.keys(value).some(k=>!['opacity','finish'].includes(k))||('opacity'in value&&(!Number.isFinite(value.opacity)||value.opacity<0||value.opacity>1))||('finish'in value&&!FINISHES.includes(value.finish)))fail('Invalid CAD material.');
    return {opacity:value.opacity??1,finish:value.finish??'satin'};
  }
  function renderSettings(value={}) {
    const closed=(v,keys)=>v&&typeof v==='object'&&!Array.isArray(v)&&Object.keys(v).every(k=>keys.includes(k));
    if(!closed(value,['mode','projection','lens_mm','surfaces','lighting','background','floor','grid','axes','edges']))fail('Invalid CAD render settings.');
    const mode=value.mode||'solid';if(!RENDER_MODES.includes(mode))fail('Unknown CAD render preset.');
    const studio=mode==='render',out={mode,projection:studio?'perspective':'orthographic',lens_mm:50,surfaces:{opacity:mode==='xray'?.22:1,finish:'satin'},lighting:{enabled:true,exposure:1,rotation_deg:0,fill:.35},background:{color:studio?[.91,.93,.96]:[.941,.941,.941]},floor:{enabled:studio,color:[.8,.82,.85],opacity:1,finish:'matte'},grid:!studio,axes:!studio,edges:!studio};
    for(const key of ['surfaces','lighting','background','floor'])if(key in value){if(!closed(value[key],Object.keys(out[key])))fail('Invalid '+key+' render settings.');Object.assign(out[key],value[key]);}
    for(const key of ['projection','lens_mm','grid','axes','edges'])if(key in value)out[key]=value[key];
    const unit=v=>Array.isArray(v)&&v.length===3&&v.every(n=>Number.isFinite(n)&&n>=0&&n<=1),range=(v,a,b)=>Number.isFinite(v)&&v>=a&&v<=b;
    if(!['orthographic','perspective'].includes(out.projection)||!range(out.lens_mm,18,120)||!['grid','axes','edges'].every(k=>typeof out[k]==='boolean')||!range(out.surfaces.opacity,0,1)||!FINISHES.includes(out.surfaces.finish)||typeof out.lighting.enabled!=='boolean'||!range(out.lighting.exposure,.1,4)||!range(out.lighting.rotation_deg,-360,360)||!range(out.lighting.fill,0,1)||!unit(out.background.color)||typeof out.floor.enabled!=='boolean'||!unit(out.floor.color)||!range(out.floor.opacity,0,1)||!['matte','glossy'].includes(out.floor.finish))fail('Invalid CAD render setting value.');
    return out;
  }
  const defaultAppearance=()=>({default_color:[.66,.75,.80],parts:[]});
  function appearance(value,source,{prune=false}={}) {
    const keys=(v,list)=>v&&typeof v==='object'&&!Array.isArray(v)&&Object.keys(v).every(k=>list.includes(k)||k==='material')&&list.every(k=>Object.hasOwn(v,k));
    const rgb=v=>Array.isArray(v)&&v.length===3&&v.every(n=>typeof n==='number'&&Number.isFinite(n)&&n>=0&&n<=1);
    if(!keys(value,['default_color','parts'])||!rgb(value.default_color)||!Array.isArray(value.parts)||value.parts.length>1024)fail('Invalid CAD appearance.');
    const seen=new Set(),parts=[];
    for(const item of value.parts){
      if(!keys(item,['part_id','color'])||typeof item.part_id!=='string'||!rgb(item.color)||seen.has(item.part_id))fail('Invalid or duplicate occurrence appearance.');
      seen.add(item.part_id);if(!source?.partBounds.has(item.part_id)){if(prune)continue;fail('Appearance must name a current leaf occurrence.');}
      parts.push({part_id:item.part_id,color:[...item.color],...(item.material?{material:material(item.material)}:{})});
    }
    return {default_color:[...value.default_color],parts,...(value.material?{material:material(value.material)}:{})};
  }
  function surfaceStyle(model,value,owner,settings) {
    const part=value.parts.find(p=>p.part_id===owner),group=model.read_only?model.geometries.face.get(owner):null,authored=group?.material;
    const base=part?.color||authored?.base_color?.slice(0,3)||value.default_color,m=material(part?.material||value.material||{}),roughness={matte:.9,satin:.5,glossy:.15,metal:.24};
    return {color:base,opacity:(authored?.alpha_mode==='OPAQUE'?1:(authored?.base_color?.[3]??m.opacity))*(settings?.surfaces.opacity??1),roughness:authored?.roughness??roughness[part?.material?.finish||value.material?.finish||settings?.surfaces.finish||'satin'],metallic:authored?.metallic??((part?.material?.finish||value.material?.finish||settings?.surfaces.finish||'satin')==='metal'?1:0),emissive:authored?.emissive||[0,0,0],alpha_mode:authored?.alpha_mode||'BLEND',alpha_cutoff:authored?.alpha_cutoff??.5};
  }
  function triangleOwner(model,index){const id=model.triangleFaces[index];return model.read_only?id:model.geometries.face.get(id)?.part_id;}
  function appearanceData(model,value) {
    const colors=new Map(value.parts.map(v=>[v.part_id,v.color]));
    if(!colors.size&&!model.read_only)return new Float32Array();
    const data=new Float32Array(model.indices.length*3);
    for(let i=0;i<model.indices.length;i++){const owner=triangleOwner(model,Math.floor(i/3)),style=surfaceStyle(model,value,owner),group=model.read_only?model.geometries.face.get(owner):null,vertexColor=group?.vertex_colors?.[model.indices[i]-(group.vertex_start||0)];data.set(style.color.map((v,k)=>v*(vertexColor?.[k]??1)),i*3);}
    return data;
  }
  function materialData(model,value,settings) {
    const data=new Float32Array(model.indices.length*3),uvs=new Float32Array(model.indices.length*2),groups=[];
    for(let i=0;i<model.indices.length;i++){const owner=triangleOwner(model,Math.floor(i/3)),style=surfaceStyle(model,value,owner,settings);const g=model.read_only?model.geometries.face.get(owner):null,vertexAlpha=g?.vertex_colors?.[model.indices[i]-(g.vertex_start||0)]?.[3]??1;data.set([style.opacity*vertexAlpha,style.roughness,style.metallic],i*3);
      const group=model.read_only?model.geometries.face.get(owner):null,uv=group?.texture_coordinates?.[model.indices[i]-(group?.vertex_start||0)];if(uv)uvs.set(uv,i*2);
      if(i%3===0){const last=groups.at(-1);if(last&&last.owner===owner)last.count+=3;else groups.push({owner,start:i,count:3,style});}}
    const varied=model.read_only||value.material||value.parts.some(p=>p.material)||settings.surfaces.opacity!==1||settings.surfaces.finish!=='satin';
    return {materials:varied?data:new Float32Array(),uvs:model.read_only?uvs:new Float32Array(),groups};
  }
  function studioLight(settings) {const t=settings.lighting.rotation_deg*Math.PI/180,c=Math.cos(t),s=Math.sin(t);return [-.45*c+.55*s,-.45*s-.55*c,1];}
  function studioGeometry(model,settings) {
    if(!settings.floor.enabled||!model?.bounds)return new Float32Array();const light=studioLight(settings),floor=model.bounds.min[2]-.0002,values=[];
    for(let i=0;i<model.indices.length;i+=3){let polygon=[0,1,2].map(k=>vertex(model.positions,model.indices[i+k]));
      if(model.clip){const clipped=[];for(let k=0;k<polygon.length;k++){const a=polygon[k],b=polygon[(k+1)%polygon.length],da=clipDistance(model,a),db=clipDistance(model,b);if(da>=-CLIP_EPS)clipped.push(a);if((da<0)!==(db<0)){const t=da/(da-db);clipped.push(a.map((v,j)=>v+t*(b[j]-v)));}}polygon=clipped;}
      for(let k=1;k+1<polygon.length;k++)for(const p of [polygon[0],polygon[k],polygon[k+1]]){const t=(p[2]-floor)/light[2];values.push(p[0]-light[0]*t,p[1]-light[1]*t,floor,0,0,1,0);}}
    return Float32Array.from(values);
  }
  function sectionGeometry(value) {
    if(!value?.clip)return null;
    const e=value.explode;
    return {plane:{normal:[...value.clip.normal],offset_mm:value.clip.offset_mm},
      explode:{distance_mm:e.distance_mm,directions:e.distance_mm===0?[]:e.directions.map(v=>({part_id:v.part_id,direction:[...v.direction]})).sort((a,b)=>a.part_id.localeCompare(b.part_id))}};
  }
  function presentationOffsets(source,settings) {
    const distance=settings.explode.distance_mm/source.span,offsets=new Map(),explicit=new Map(settings.explode.directions.map(v=>[v.part_id,v.direction]));
    if(!Number.isFinite(distance)||distance>1e9)fail('Exploded distance exceeds viewport coordinate precision.');
    if(distance>0)[...source.partBounds.keys()].sort().forEach((id,index)=>{
      const b=source.partBounds.get(id);let direction=explicit.get(id)||b.min.map((v,i)=>(v+b.max[i])/2),length=Math.hypot(...direction);
      if(!explicit.has(id)&&length<1e-12){direction=[0,0,0];direction[index%3]=index%2?-1:1;length=1;}
      offsets.set(id,direction.map(v=>v/length*distance));
    });
    return offsets;
  }
  function validateSection(result,source,value) {
    const closed=(v,required,optional=[])=>v&&typeof v==='object'&&!Array.isArray(v)&&required.every(k=>Object.hasOwn(v,k))&&Object.keys(v).every(k=>required.includes(k)||optional.includes(k));
    const bounded=(n,max=Infinity)=>typeof n==='number'&&Number.isFinite(n)&&n>=0&&n<=max;
    const count=(n,max=MAX_ENTITIES,min=0)=>Number.isInteger(n)&&n>=min&&n<=max;
    const unit=v=>finitePoint(v)&&v.every(n=>Math.abs(n)<=1)&&Math.abs(dot(v,v)-1)<=1e-6;
    const header=['document_id','revision','evaluation_id','feature_id','kernel_version','model_sha256','native_build','report'];
    if(!source||!closed(result,header)||Object.entries(source.identity).some(([k,v])=>result[k]!==v)||result.kernel_version!=='8.0.1'||!/^[a-f0-9]{64}$/.test(result.model_sha256)||typeof result.native_build!=='string'||!result.native_build.length)fail('Section belongs to another evaluation or an unqualified native build.');
    const settings=presentation(value,source),wanted=sectionGeometry(settings),r=result.report;
    // All accepted report strings are closed enum/ASCII occurrence paths; its
    // JSON character count therefore equals its UTF-8 byte count.
    if(!r||JSON.stringify(r).length>8*1024*1024)fail('Section report exceeds its byte limit.');
    if(!wanted||!closed(r,['schema_version','units','action','method','coordinate_space','plane_coordinate_space','coverage','area_semantics','plane','explode','sections','regions','curves','mesh','area_mm2','boundary_length_mm','status','selection_lifetime','tolerance_mm','point_tolerance_mm'])||r.schema_version!==1||r.units!=='mm'||r.action!=='section'||r.method!=='native_BRep_planar_section'||r.coordinate_space!=='committed_source_pose'||r.plane_coordinate_space!=='displayed_world_mm'||r.area_semantics!=='sum_of_solid_sections'||r.selection_lifetime!=='section_result'||r.tolerance_mm!==1e-7||r.point_tolerance_mm!==1e-6)fail('Invalid native section report.');
    if(!closed(r.plane,['normal','offset_mm'])||!unit(r.plane.normal)||!Number.isFinite(r.plane.offset_mm)||Math.abs(r.plane.offset_mm)>1e12||!closed(r.explode,['distance_mm','directions']))fail('Invalid section geometry inputs.');
    const resolved=presentation({clip:{...r.plane,keep:settings.clip.keep},explode:r.explode},source);
    if(JSON.stringify(sectionGeometry(resolved))!==JSON.stringify(wanted))fail('Section does not match the current plane and exploded placement.');
    if(!Array.isArray(r.sections)||!r.sections.length||r.sections.length>1024||!Array.isArray(r.regions)||!Array.isArray(r.curves)||r.regions.length+r.curves.length>MAX_ENTITIES)fail('Section exceeds its scope or entity limit.');
    const assembly=source.partBounds.size>0,scopes=new Map(),offsets=presentationOffsets(source,settings),tol=r.point_tolerance_mm;
    const status=v=>['empty','tangent','area'].includes(v),owner=v=>assembly?source.partBounds.has(v):v===null;
    let contacts=0;
    for(const scope of r.sections) {
      if(!closed(scope,['part_id','source_plane_offset_mm','displacement_mm','area_mm2','boundary_length_mm','region_count','curve_count','contact_points','status'])||!owner(scope.part_id)||scopes.has(scope.part_id)||!finitePoint(scope.displacement_mm)||!Number.isFinite(scope.source_plane_offset_mm)||Math.abs(scope.source_plane_offset_mm)>1e12||!bounded(scope.area_mm2)||!bounded(scope.boundary_length_mm)||!count(scope.region_count)||!count(scope.curve_count)||!Array.isArray(scope.contact_points)||!status(scope.status))fail('Invalid section scope.');
      const shift=(offsets.get(scope.part_id)||[0,0,0]).map(v=>v*source.span);
      if(scope.displacement_mm.some((v,i)=>Math.abs(v-shift[i])>tol)||Math.abs(scope.source_plane_offset_mm-(r.plane.offset_mm-dot(r.plane.normal,shift)))>tol)fail('Section displacement does not match source placement.');
      contacts+=scope.contact_points.length;if(contacts+r.regions.length+r.curves.length>MAX_ENTITIES)fail('Section exceeds its contact limit.');
      scopes.set(scope.part_id,scope);
    }
    if(assembly?(!['all_assembly_leaves','explicit_leaf_subset'].includes(r.coverage)||(r.coverage==='all_assembly_leaves'&&scopes.size!==source.partBounds.size)):(r.coverage!=='feature_solids'||scopes.size!==1))fail('Invalid section coverage.');
    const insideSource=(p,part)=>{const box=assembly?source.partBounds.get(part):source.bounds;return box&&p.every((v,i)=>{const n=(v-source.center[i])/source.span;return n>=box.min[i]-1e-5/source.span&&n<=box.max[i]+1e-5/source.span;});};
    const onPlane=(p,part)=>finitePoint(p)&&scopes.has(part)&&insideSource(p,part)&&Math.abs(dot(r.plane.normal,p)-scopes.get(part).source_plane_offset_mm)<=tol;
    const regions=new Map(),curves=new Map(),regionCounts=new Map(),curveCounts=new Map(),areas=new Map(),lengths=new Map();
    for(const scope of scopes.values())if(!scope.contact_points.every(p=>onPlane(p,scope.part_id)))fail('Section contact lies off its source plane.');
    for(const region of r.regions) {
      if(!closed(region,['id','part_id','solid_index','area_mm2','perimeter_mm','center_mm','wire_count'])||!/^cap-[1-9][0-9]*$/.test(region.id)||regions.has(region.id)||!scopes.has(region.part_id)||!count(region.solid_index,1024,1)||!bounded(region.area_mm2)||region.area_mm2===0||!bounded(region.perimeter_mm)||!count(region.wire_count,MAX_ENTITIES,1)||!onPlane(region.center_mm,region.part_id))fail('Invalid native cap region.');
      regions.set(region.id,region);regionCounts.set(region.part_id,(regionCounts.get(region.part_id)||0)+1);areas.set(region.part_id,(areas.get(region.part_id)||0)+region.area_mm2);
    }
    let points=0;
    for(const curve of r.curves) {
      if(!closed(curve,['id','part_id','solid_index','curve_kind','length_mm','center_mm','bounds_mm','degenerate','points'],['direction','axis','radius_mm','closed','endpoints_mm'])||!/^section-[1-9][0-9]*$/.test(curve.id)||curves.has(curve.id)||!scopes.has(curve.part_id)||!count(curve.solid_index,1024,1)||!['line','circle','ellipse','hyperbola','parabola','bezier','bspline','offset','other'].includes(curve.curve_kind)||!bounded(curve.length_mm)||!onPlane(curve.center_mm,curve.part_id)||!closed(curve.bounds_mm,['min','max'])||!finitePoint(curve.bounds_mm.min)||!finitePoint(curve.bounds_mm.max)||curve.bounds_mm.min.some((v,i)=>v>curve.bounds_mm.max[i])||typeof curve.degenerate!=='boolean'||!Array.isArray(curve.points)||!curve.points.every(p=>onPlane(p,curve.part_id))||['direction','axis'].some(k=>Object.hasOwn(curve,k)&&!finitePoint(curve[k]))||(Object.hasOwn(curve,'radius_mm')&&!bounded(curve.radius_mm)))fail('Invalid native section curve.');
      if((Object.hasOwn(curve,'closed')&&typeof curve.closed!=='boolean')||(Object.hasOwn(curve,'endpoints_mm')&&(!Array.isArray(curve.endpoints_mm)||curve.endpoints_mm.length!==2||!curve.endpoints_mm.every(p=>onPlane(p,curve.part_id)))))fail('Invalid native section endpoints or closure.');
      points+=curve.points.length;if(points>MAX_EDGE_POINTS)fail('Section curves exceed their point limit.');
      curves.set(curve.id,curve);curveCounts.set(curve.part_id,(curveCounts.get(curve.part_id)||0)+1);lengths.set(curve.part_id,(lengths.get(curve.part_id)||0)+curve.length_mm);
    }
    const near=(a,b)=>Math.abs(a-b)<=Math.max(tol,Math.max(Math.abs(a),Math.abs(b))*1e-10);
    let area=0,length=0;
    for(const scope of scopes.values()) {
      if(scope.region_count!==(regionCounts.get(scope.part_id)||0)||scope.curve_count!==(curveCounts.get(scope.part_id)||0)||!near(scope.area_mm2,areas.get(scope.part_id)||0)||!near(scope.boundary_length_mm,lengths.get(scope.part_id)||0)||scope.status!==(scope.region_count?'area':scope.curve_count||scope.contact_points.length?'tangent':'empty'))fail('Section scope totals do not match native geometry.');
      area+=scope.area_mm2;length+=scope.boundary_length_mm;
    }
    if(!bounded(r.area_mm2)||!bounded(r.boundary_length_mm)||!near(r.area_mm2,area)||!near(r.boundary_length_mm,length)||!status(r.status)||r.status!==(r.regions.length?'area':r.curves.length||contacts?'tangent':'empty'))fail('Invalid section aggregate measurements.');
    const m=r.mesh;
    if(!closed(m,['positions','triangles','triangle_regions','linear_deflection_mm'])||m.linear_deflection_mm!==.1||!Array.isArray(m.positions)||m.positions.length>MAX_VERTICES||!m.positions.every(finitePoint)||!Array.isArray(m.triangles)||m.triangles.length>MAX_TRIANGLES||!Array.isArray(m.triangle_regions)||m.triangle_regions.length!==m.triangles.length)fail('Invalid or oversized native cap mesh.');
    const represented=new Set(),used=new Set();
    for(let i=0;i<m.triangles.length;i++) {
      const region=regions.get(m.triangle_regions[i]),triangle=m.triangles[i];
      if(!region||!Array.isArray(triangle)||triangle.length!==3||!triangle.every(v=>count(v,m.positions.length-1)&&onPlane(m.positions[v],region.part_id)))fail('Native cap triangle has invalid ownership or coordinates.');
      represented.add(region.id);triangle.forEach(v=>used.add(v));
    }
    if(represented.size!==regions.size)fail('Native cap mesh must cover every region.');
    // OCCT may retain triangulation vertices that no triangle references. They
    // do not draw or pick, but still need finite qualified source-plane data.
    for(let i=0;i<m.positions.length;i++)if(!used.has(i)&&![...scopes.keys()].some(part=>onPlane(m.positions[i],part)))fail('Unused native cap vertex lies off every source plane.');
    return result;
  }
  function sectionModel(result,source,hiddenPartIds=[]) {
    const r=result.report,hidden=new Set(hiddenPartIds),scopes=new Map(r.sections.map(v=>[v.part_id,v])),regions=new Map(r.regions.map(v=>[v.id,v])),values=[],indices=[],triangleFaces=[],vertices=new Map();
    const normalized=(p,part)=>p.map((v,i)=>(v-source.center[i]+scopes.get(part).displacement_mm[i])/source.span);
    r.mesh.triangles.forEach((t,i)=>{
      const region=regions.get(r.mesh.triangle_regions[i]);if(hidden.has(region.part_id))return;
      for(const original of t){const key=original+'/'+region.part_id;let target=vertices.get(key);if(target===undefined){target=values.length/3;vertices.set(key,target);values.push(...normalized(r.mesh.positions[original],region.part_id));}indices.push(target);}triangleFaces.push(region.id);
    });
    const positions=Float64Array.from(values),indexArray=Uint32Array.from(indices),faces=[...new Set(triangleFaces)],edges=r.curves.filter(v=>!hidden.has(v.part_id)).map(v=>({id:v.id,points:Float64Array.from(v.points.flatMap(p=>normalized(p,v.part_id)))}));
    return {positions,indices:indexArray,triangleFaces,faces,faceNumbers:new Map(faces.map((v,i)=>[v,i+1])),edges,tree:indexArray.length?buildTree(positions,indexArray):{nodes:[],order:new Uint32Array()},clip:null};
  }
  function depthExtent(model) {
    if(!model?.bounds)return 2;
    const b=model.bounds;return Math.max(2,1+Math.hypot(...b.min.map((v,i)=>Math.max(Math.abs(v),Math.abs(b.max[i])))));
  }
  function clipPlane(source,value) {
    if(!value)return null;
    const offset=(value.offset_mm-dot(value.normal,source.center))/source.span;
    if(!Number.isFinite(offset)||Math.abs(offset)>1e12)fail('Clipping offset exceeds viewport coordinate precision.');
    return {normal:value.normal,offset,sign:value.keep==='positive'?1:-1};
  }
  // Derive presentation coordinates, never change source geometry or references.
  // Duplicate only vertex/owner pairs, since separate parts may share vertices.
  function presentedModel(source,value) {
    const settings=presentation(value,source),distance=settings.explode.distance_mm/source.span,offsets=presentationOffsets(source,settings);
    let model=source;
    if(distance>0) {
      const values=[],indices=new Uint32Array(source.indices.length),vertices=new Map();
      for(let i=0;i<source.indices.length;i++) {
        const owner=source.geometries.face.get(source.triangleFaces[Math.floor(i/3)])?.part_id,original=source.indices[i],key=original+'/'+owner;
        let target=vertices.get(key);
        if(target===undefined){target=values.length/3;vertices.set(key,target);const shift=offsets.get(owner)||[0,0,0];values.push(...vertex(source.positions,original).map((v,k)=>v+shift[k]));}
        indices[i]=target;
      }
      const positions=Float64Array.from(values),edges=source.edges.map(e=>{const shift=offsets.get(source.geometries.edge.get(e.id)?.part_id)||[0,0,0];return {...e,points:Float64Array.from(e.points,(v,i)=>v+shift[i%3])};});
      const partBounds=new Map([...source.partBounds].map(([id,b])=>[id,{min:b.min.map((v,i)=>v+offsets.get(id)[i]),max:b.max.map((v,i)=>v+offsets.get(id)[i])}]));
      const bounds=source.bounds?{min:[Infinity,Infinity,Infinity],max:[-Infinity,-Infinity,-Infinity]}:null;
      if(bounds)for(const id of partBounds.keys())if(!source.hiddenPartIds.includes(id)){const b=partBounds.get(id);for(let i=0;i<3;i++){bounds.min[i]=Math.min(bounds.min[i],b.min[i]);bounds.max[i]=Math.max(bounds.max[i],b.max[i]);}}
      model={...source,positions,indices,edges,partBounds,bounds,tree:indices.length?buildTree(positions,indices):{nodes:[],order:new Uint32Array()}};
    }
    const clip=clipPlane(source,settings.clip);
    return {...model,clip,presentation:settings,depthExtent:depthExtent(model)};
  }
  const clipDistance=(model,point)=>model.clip?model.clip.sign*(dot(model.clip.normal,point)-model.clip.offset):Infinity;
  function clipSegment(model,a,b) {
    const da=clipDistance(model,a),db=clipDistance(model,b);
    if(da < -CLIP_EPS && db < -CLIP_EPS)return null;
    if(da < -CLIP_EPS||db < -CLIP_EPS){const t=(da+CLIP_EPS)/(da-db),p=a.map((v,i)=>v+t*(b[i]-v));return da < -CLIP_EPS?[p,b]:[a,p];}
    return [a,b];
  }
  // Derived draw/pick data retains full evaluation identity and normalization.
  // Build only on a visibility change, never on a camera/context poll.
  function visibleModel(source,ids) {
    if(!Array.isArray(ids)||ids.length>1024||new Set(ids).size!==ids.length||ids.some(id=>!source.partBounds.has(id)))fail('Visibility must name unique current assembly parts.');
    if(!ids.length)return source;
    const hidden=new Set(ids),visible=entity=>!hidden.has(entity.part_id),geometries={face:new Map([...source.geometries.face].filter(([,e])=>visible(e))),edge:new Map([...source.geometries.edge].filter(([,e])=>visible(e)))};
    const triangles=[];for(let i=0;i<source.triangleFaces.length;i++)if(geometries.face.has(source.triangleFaces[i]))triangles.push(i);
    const indices=new Uint32Array(triangles.length*3),triangleFaces=[];
    triangles.forEach((index,i)=>{indices.set(source.indices.subarray(index*3,index*3+3),i*3);triangleFaces.push(source.triangleFaces[index]);});
    const edges=source.edges.filter(edge=>geometries.edge.has(edge.id)),bounds={min:[Infinity,Infinity,Infinity],max:[-Infinity,-Infinity,-Infinity]};
    let count=0;for(const [id,box] of source.partBounds)if(!hidden.has(id)){count++;for(let i=0;i<3;i++){bounds.min[i]=Math.min(bounds.min[i],box.min[i]);bounds.max[i]=Math.max(bounds.max[i],box.max[i]);}}
    return {...source,indices,triangleFaces,edges,geometries,faces:source.faces.filter(id=>geometries.face.has(id)),tree:buildTree(source.positions,indices),bounds:count?bounds:null,hiddenPartIds:[...ids]};
  }
  function boxEntry(node,ray,limit) {
    let lo=0,hi=limit;
    for(let i=0;i<3;i++) {
      const direction=ray.direction[i],origin=ray.origin[i];
      if(Math.abs(direction)<1e-15){if(origin<node.min[i]-DEPTH_EPS||origin>node.max[i]+DEPTH_EPS)return Infinity;continue;}
      let a=(node.min[i]-origin-DEPTH_EPS)/direction,b=(node.max[i]-origin+DEPTH_EPS)/direction;
      if(a>b)[a,b]=[b,a];lo=Math.max(lo,a);hi=Math.min(hi,b);if(lo>hi)return Infinity;
    }
    return lo;
  }
  function triangleHit(model,index,ray) {
    const a=vertex(model.positions,model.indices[3*index]),b=vertex(model.positions,model.indices[3*index+1]),c=vertex(model.positions,model.indices[3*index+2]);
    const e=b.map((v,i)=>v-a[i]),f=c.map((v,i)=>v-a[i]),d=ray.direction;
    const p=[d[1]*f[2]-d[2]*f[1],d[2]*f[0]-d[0]*f[2],d[0]*f[1]-d[1]*f[0]],det=dot(e,p);
    if(Math.abs(det)<=1e-12*Math.hypot(...e)*Math.hypot(...f))return Infinity;
    const s=ray.origin.map((v,i)=>v-a[i]),u=dot(s,p)/det;if(u< -1e-9||u>1+1e-9)return Infinity;
    const q=[s[1]*e[2]-s[2]*e[1],s[2]*e[0]-s[0]*e[2],s[0]*e[1]-s[1]*e[0]],v=dot(d,q)/det;
    if(v< -1e-9||u+v>1+1e-9)return Infinity;
    const t=dot(f,q)/det;return t>=0&&t<=(ray.far??4) ? t : Infinity;
  }
  // |cos| between a triangle's normal and the view ray.
  function facing(model,index,direction) {
    const a=vertex(model.positions,model.indices[3*index]),b=vertex(model.positions,model.indices[3*index+1]),c=vertex(model.positions,model.indices[3*index+2]);
    const e=b.map((v,i)=>v-a[i]),f=c.map((v,i)=>v-a[i]),n=[e[1]*f[2]-e[2]*f[1],e[2]*f[0]-e[0]*f[2],e[0]*f[1]-e[1]*f[0]],length=Math.hypot(...n);
    return length>0?Math.abs(dot(n,direction))/(length*Math.hypot(...direction)):1;
  }
  function traceTriangles(model,ray,budget) {
    if(!model.indices.length)return {depth:Infinity,ids:new Set(),facing:1};
    let depth=Infinity,cosine=1;const hits=new Map(),stack=[0],{nodes,order}=model.tree;
    while(stack.length) {
      if(--budget.remaining<0)fail('Selection exceeds its work limit. Inspect a smaller feature.');
      const node=nodes[stack.pop()];if(boxEntry(node,ray,Math.min(ray.far??4,depth+DEPTH_EPS))===Infinity)continue;
      if(node.left>=0) {
        const a=boxEntry(nodes[node.left],ray,Math.min(ray.far??4,depth+DEPTH_EPS)),b=boxEntry(nodes[node.right],ray,Math.min(ray.far??4,depth+DEPTH_EPS));
        if(a<b){if(b!==Infinity)stack.push(node.right);if(a!==Infinity)stack.push(node.left);}else{if(a!==Infinity)stack.push(node.left);if(b!==Infinity)stack.push(node.right);}
      } else for(let i=node.start;i<node.start+node.count;i++) {
        if(--budget.remaining<0)fail('Selection exceeds its work limit. Inspect a smaller feature.');
        const index=order[i],t=triangleHit(model,index,ray);if(t===Infinity||clipDistance(model,ray.origin.map((v,i)=>v+t*ray.direction[i])) < -CLIP_EPS)continue;
        if(t<depth){depth=t;cosine=facing(model,index,ray.direction);for(const [id,distance] of hits)if(distance>depth+DEPTH_EPS)hits.delete(id);}
        if(t<=depth+DEPTH_EPS){const id=model.triangleFaces[index];hits.set(id,Math.min(t,hits.get(id)??Infinity));}
      }
    }
    return {depth,ids:new Set(hits.keys()),facing:cosine};
  }
  function trace(model,ray,budget={remaining:PICK_BUDGET}) {
    const source=traceTriangles(model,ray,budget);
    if(!model.section)return source;
    const cap=traceTriangles(model.section,ray,budget);
    if(cap.depth!==Infinity&&cap.depth<=source.depth+DEPTH_EPS)return {depth:cap.depth,ids:new Set(),facing:cap.facing,section:true};
    return source;
  }
  function nearestSegment(p,a,b) {
    const dx=b[0]-a[0],dy=b[1]-a[1],length=dx*dx+dy*dy;
    const t=length>1e-16?Math.max(0,Math.min(1,((p[0]-a[0])*dx+(p[1]-a[1])*dy)/length)):(a[2]<=b[2]?0:1);
    const q=a.map((v,i)=>v+t*(b[i]-v));if(a.length===4&&b.length===4)q[2]=((1-t)*a[2]*a[3]+t*b[2]*b[3])/((1-t)*a[3]+t*b[3]);return {point:q,distance:Math.hypot(q[0]-p[0],q[1]-p[1])};
  }
  function pickFace(model,c,width,height,x,y,budget) {
    const hit=trace(model,screenRay(x,y,c,width,height,model.depthExtent??2),budget);
    return {id:hit.ids.size===1?[...hit.ids][0]:null,ambiguous:hit.ids.size>1,...(hit.section?{section:true}:{})};
  }
  function pickEdge(model,c,width,height,x,y,tolerance,budget) {
    const candidates=[];
    for(const edge of model.edges) {
      for(let i=3;i<edge.points.length;i+=3) {
        const segment=clipSegment(model,edge.points.subarray(i-3,i),edge.points.subarray(i,i+3));
        if(segment){const q=nearestSegment([x,y],...segment.map(p=>project(p,c,width,height)));if(q.distance<=tolerance&&q.point[0]>=0&&q.point[1]>=0&&q.point[0]<=width&&q.point[1]<=height)candidates.push({...q,id:edge.id});}
      }
    }
    candidates.sort((a,b)=>a.distance-b.distance);
    let best=Infinity,depth=Infinity;const ids=new Set();
    for(const candidate of candidates) {
      if(candidate.distance>best+.25)break;
      const q=candidate.point,extent=model.depthExtent??2,hit=trace(model,screenRay(q[0],q[1],c,width,height,extent),budget),z=q[2]+(c.projection==='perspective'?(c.distance||3):extent);
      // The deflection offset is normal to the surface; along the ray it grows
      // as 1/cos against the occluding triangle, capped at 4x for grazing views.
      if(z>hit.depth+DEPTH_EPS*4+(hit.section?0:(model.edgeAllowance||0)/Math.max(hit.facing,.25)))continue;
      if(best===Infinity){best=candidate.distance;depth=z;ids.add(candidate.id);}
      else if(z<depth-DEPTH_EPS) {depth=z;ids.clear();ids.add(candidate.id);}
      else if(Math.abs(candidate.distance-best)<=.25&&Math.abs(z-depth)<=DEPTH_EPS)ids.add(candidate.id);
    }
    const readonly=!ids.size&&model.section&&trace(model,screenRay(x,y,c,width,height,model.depthExtent??2),budget).section;
    return {id:ids.size===1?[...ids][0]:null,ambiguous:ids.size>1,...(readonly?{section:true}:{})};
  }
  // 'auto' resolves one cursor to an edge when exactly one visible edge is within
  // a few pixels, otherwise to the face. Coincident edges fall through to the face.
  function pick(model,c,width,height,x,y,mode='face') {
    if(!model||!Number.isFinite(width)||!Number.isFinite(height)||width<=0||height<=0||!Number.isFinite(x)||!Number.isFinite(y)||x<0||y<0||x>width||y>height)return {id:null,ambiguous:false};
    const budget={remaining:PICK_BUDGET};
    if(mode==='face')return pickFace(model,c,width,height,x,y,budget);
    if(mode==='edge')return pickEdge(model,c,width,height,x,y,EDGE_TOLERANCE,budget);
    const edge=pickEdge(model,c,width,height,x,y,AUTO_EDGE_TOLERANCE,budget);
    if(edge.id)return {...edge,kind:'edge'};
    const face=pickFace(model,c,width,height,x,y,budget);
    if(face.id)return {...face,kind:'face'};
    return {...face,ambiguous:face.ambiguous||edge.ambiguous};
  }
  // User-defined locations are ray hits on the displayed tessellation, rather
  // than face centres or the closest points of two native geometry targets.
  function pickPoint(model,c,width,height,x,y) {
    if(!model||![width,height,x,y].every(Number.isFinite)||width<=0||height<=0||x<0||y<0||x>width||y>height)return null;
    const ray=screenRay(x,y,c,width,height,model.depthExtent??2),hit=trace(model,ray);
    if(!Number.isFinite(hit.depth))return null;
    return ray.origin.map((v,i)=>model.center[i]+(v+hit.depth*ray.direction[i])*model.span);
  }
  function pointDistance(a,b) {
    if(![a,b].every(p=>finitePoint(p)&&p.every(v=>Math.abs(v)<=1e9)))fail('Measurement points require three finite coordinates in millimeters.');
    const delta_mm=b.map((v,i)=>v-a[i]);return {delta_mm,distance_mm:Math.hypot(...delta_mm)};
  }
  function pickMeasurementPoint(model,c,width,height,x,y) {
    if(!model||![width,height,x,y].every(Number.isFinite)||width<=0||height<=0||x<0||y<0||x>width||y>height)return null;
    const candidates=[],budget={remaining:PICK_BUDGET},extent=model.depthExtent??2;
    for(const edge of model.edges)for(let i=3;i<edge.points.length;i+=3){
      const segment=clipSegment(model,edge.points.subarray(i-3,i),edge.points.subarray(i,i+3));if(!segment)continue;
      const screen=segment.map(p=>project(p,c,width,height)),q=nearestSegment([x,y],...screen);
      if(q.distance<=AUTO_EDGE_TOLERANCE&&q.point[0]>=0&&q.point[1]>=0&&q.point[0]<=width&&q.point[1]<=height)candidates.push(q);
    }
    candidates.sort((a,b)=>a.distance-b.distance||a.point[2]-b.point[2]);
    for(const q of candidates){
      const ray=screenRay(q.point[0],q.point[1],c,width,height,extent),hit=trace(model,ray,budget),depth=q.point[2]+(c.projection==='perspective'?(c.distance||3):extent);
      if(depth>hit.depth+DEPTH_EPS*4+(hit.section?0:(model.edgeAllowance||0)/Math.max(hit.facing,.25)))continue;
      return {point_mm:ray.origin.map((v,i)=>model.center[i]+(v+depth*ray.direction[i])*model.span),input:'edge'};
    }
    const point_mm=pickPoint(model,c,width,height,x,y);return point_mm?{point_mm,input:'surface'}:null;
  }
  function pointMeasurementLayout(points,model,c,width,height) {
    if(!model||!points||width<=0||height<=0)return null;
    const result={};
    for(const slot of ['a','b'])if(points[slot]) {
      if(!finitePoint(points[slot]))fail('Invalid measurement point.');
      result[slot]=project(points[slot].map((v,i)=>(v-model.center[i])/model.span),c,width,height).slice(0,2);
    }
    if(!result.a&&!result.b)return null;
    if(result.a&&result.b)Object.assign(result,pointDistance(points.a,points.b));
    return result;
  }
  function gpuData(model) {
    const normals=new Float64Array(model.positions.length);
    for(let i=0;i<model.indices.length;i+=3) {
      const a=vertex(model.positions,model.indices[i]),b=vertex(model.positions,model.indices[i+1]),c=vertex(model.positions,model.indices[i+2]),u=b.map((v,j)=>v-a[j]),v=c.map((n,j)=>n-a[j]);
      const n=[u[1]*v[2]-u[2]*v[1],u[2]*v[0]-u[0]*v[2],u[0]*v[1]-u[1]*v[0]];
      for(let k=0;k<3;k++)for(let j=0;j<3;j++)normals[model.indices[i+k]*3+j]+=n[j];
    }
    const faces=new Float32Array(model.indices.length*7);
    for(let i=0;i<model.indices.length;i++) {
      const index=model.indices[i],group=model.read_only?model.geometries.face.get(model.triangleFaces[Math.floor(i/3)]):null,authored=group?.normals_mm?.[index-(group.vertex_start||0)],normal=authored||vertex(normals,index),length=Math.hypot(...normal)||1;
      faces.set(vertex(model.positions,index),i*7);faces.set(normal.map(n=>n/length),i*7+3);faces[i*7+6]=model.faceNumbers.get(model.triangleFaces[Math.floor(i/3)]);
    }
    let segments=0;for(const edge of model.edges)segments+=Math.max(0,edge.points.length/3-1);
    const edges=new Float32Array(segments*8),ranges=new Map();let offset=0;
    model.edges.forEach((edge,index)=>{
      const start=offset/4;
      for(let i=3;i<edge.points.length;i+=3)for(const j of [i-3,i]) {edges.set(edge.points.subarray(j,j+3),offset);edges[offset+3]=index+1;offset+=4;}
      ranges.set(edge.id,{start,count:offset/4-start,number:index+1});
    });
    return {faces,edges,ranges};
  }
  function program(gl,webgl2) {
    const vertexSource=(webgl2?'#version 300 es\n':'')+`
      precision highp float;
      ${webgl2?'in':'attribute'} vec3 aPosition;
      ${webgl2?'in':'attribute'} vec3 aNormal;
      ${webgl2?'in':'attribute'} float aEntity;
      ${webgl2?'in':'attribute'} vec3 aColor;
      ${webgl2?'in':'attribute'} vec3 aMaterial;${webgl2?'in':'attribute'} vec2 aUV;
      uniform mat3 uBasis; uniform vec2 uScale; uniform vec2 uPan; uniform float uDepth; uniform vec2 uLineShift;uniform float uDistance;uniform bool uPerspective;
      ${webgl2?'out':'varying'} vec3 vNormal;
      ${webgl2?'out':'varying'} float vEntity;
      ${webgl2?'out':'varying'} vec3 vPosition;
      ${webgl2?'out':'varying'} vec3 vColor;
      ${webgl2?'out':'varying'} vec3 vMaterial;${webgl2?'out':'varying'} vec2 vUV;
      void main(){vec3 p=uBasis*aPosition;float w=uPerspective?1.+p.z/uDistance:1.;float z=uPerspective?((uDistance/uDepth)*p.z+uDepth)/uDistance:p.z/uDepth;gl_Position=vec4(p.xy*uScale+(uPan+uLineShift)*w,z,w);vNormal=uBasis*aNormal;vEntity=aEntity;vPosition=aPosition;vColor=aColor;vMaterial=aMaterial;vUV=aUV;}`;
    const fragmentSource=(webgl2?'#version 300 es\n':'')+`
      precision highp float;
      ${webgl2?'in':'varying'} vec3 vNormal;
      ${webgl2?'in':'varying'} float vEntity;
      ${webgl2?'in':'varying'} vec3 vPosition;
      ${webgl2?'in':'varying'} vec3 vColor;
      ${webgl2?'in':'varying'} vec3 vMaterial;${webgl2?'in':'varying'} vec2 vUV;
      uniform float uSelected;uniform float uHover;uniform bool uLines;uniform bool uSection;uniform vec3 uSectionNormal;uniform vec3 uLine;uniform vec3 uSelectColor;uniform vec3 uHoverColor;
      uniform bool uClipping;uniform vec4 uClip;uniform float uClipSign;uniform bool uLighting;uniform vec3 uKey;uniform float uFill;uniform float uExposure;uniform bool uShadow;uniform float uAlpha;uniform bool uTexture;uniform sampler2D uSampler;uniform vec3 uEmissive;uniform float uAlphaCutoff;uniform bool uOpaque;
      ${webgl2?'out vec4 outColor;':''}
      void main(){
        if(uClipping&&uClipSign*(dot(uClip.xyz,vPosition)-uClip.w)<-0.0000001)discard;
        bool selected=!uSection&&abs(vEntity-uSelected)<.25;
        bool hovered=!uSection&&!selected&&abs(vEntity-uHover)<.25;
        vec3 color;float alpha=1.;vec4 tex=uTexture?texture${webgl2?'':'2D'}(uSampler,vUV):vec4(1.);
        if(uShadow){${webgl2?'outColor':'gl_FragColor'}=vec4(.08,.10,.13,uAlpha);return;}
        if(uLines){color=uSection?vec3(.94,.65,.25):selected?uSelectColor:hovered?uHoverColor:uLine;}
        else {vec3 n=normalize(uSection?uSectionNormal:vNormal);if(!uSection&&!gl_FrontFacing)n=-n;
          vec3 key=normalize(uKey);float diffuse=max(0.,dot(n,key)),fill=max(0.,dot(n,normalize(vec3(.55,.3,-.4))));
          float rim=pow(1.-abs(n.z),3.);
          float spec=pow(max(0.,dot(reflect(-key,n),vec3(0.,0.,-1.))),max(2.,128.*(1.-vMaterial.y)));spec*=.08+.5*vMaterial.z;
          vec3 base=uSection?vec3(.94,.55,.18):selected?mix(vColor,uSelectColor,.55):hovered?mix(vColor,uHoverColor,.35):vColor;base*=tex.rgb;
          color=uLighting?(base*(.35+uFill*fill+.65*diffuse)+vec3(.025)*rim+mix(vec3(spec),base*spec,vMaterial.z))*uExposure:base; color+=uEmissive;alpha=uSection?1.:vMaterial.x*(uOpaque?1.:tex.a);if(alpha<uAlphaCutoff)discard;if(uAlphaCutoff>0.)alpha=1.;}
        ${webgl2?'outColor':'gl_FragColor'}=vec4(color,alpha);
      }`;
    const shaders=[];let linked=null;
    try {
      for(const [type,source] of [[gl.VERTEX_SHADER,vertexSource],[gl.FRAGMENT_SHADER,fragmentSource]]) {
        const shader=gl.createShader(type);if(!shader)fail('Unable to allocate a WebGL shader.');shaders.push(shader);gl.shaderSource(shader,source);gl.compileShader(shader);
        if(!gl.getShaderParameter(shader,gl.COMPILE_STATUS))fail('WebGL shader compilation failed: '+gl.getShaderInfoLog(shader));
      }
      linked=gl.createProgram();if(!linked)fail('Unable to allocate a WebGL program.');for(const shader of shaders)gl.attachShader(linked,shader);gl.linkProgram(linked);
      if(!gl.getProgramParameter(linked,gl.LINK_STATUS))fail('WebGL shader linking failed: '+gl.getProgramInfoLog(linked));
      return linked;
    } catch(error){if(linked)gl.deleteProgram(linked);throw error;}
    finally {for(const shader of shaders)gl.deleteShader(shader);}
  }
  // Attribute-less full-screen gradient (WebGL2 only). WebGL1, or a driver that
  // rejects it, keeps the solid theme-coloured clear: the backdrop is cosmetic.
  function backdropProgram(gl) {
    const vertexSource=`#version 300 es
      precision highp float;
      out float vT;out vec2 vScreen;
      void main(){vec2 p=vec2(float((gl_VertexID<<1)&2),float(gl_VertexID&2));vT=p.y;vScreen=p*2.-1.;gl_Position=vec4(vScreen,1.,1.);}`;
    const fragmentSource=`#version 300 es
      precision highp float;
      in float vT;in vec2 vScreen;uniform vec3 uTop;uniform vec3 uBottom;
      uniform mat3 uBasis;uniform vec2 uScale;uniform vec2 uPan;uniform vec2 uViewport;
      uniform bool uGrid;uniform bool uAxes;uniform float uDistance;uniform bool uPerspective;uniform bool uStudioFloor;uniform vec3 uFloorColor;uniform float uFloorOpacity;uniform bool uGlossyFloor;uniform float uFloor;uniform float uStep;uniform vec2 uShift;uniform vec3 uOrigin;
      out vec4 outColor;
      vec2 screen(vec3 p){vec3 q=uBasis*p;float w=uPerspective?1.+q.z/uDistance:1.;return (q.xy*uScale/w+uPan)*uViewport*.5;}
      float axis(vec3 direction){
        vec2 a=screen(uOrigin),b=screen(uOrigin+direction*.24),p=vScreen*uViewport*.5;
        vec2 d=b-a;float length2=dot(d,d);if(length2<1.)return 0.;
        float t=clamp(dot(p-a,d)/length2,0.,1.);return 1.-smoothstep(1.7,2.7,length(p-a-t*d));
      }
      void main(){
        vec3 color=mix(uBottom,uTop,vT);float dark=step(.5,dot(uTop,vec3(.2126,.7152,.0722)));
        if(uGrid||uStudioFloor){
          vec2 screenPoint=(vScreen-uPan)/uScale;vec3 direction=transpose(uBasis)*(uPerspective?vec3(screenPoint/uDistance,1.):vec3(0.,0.,1.));
          if(abs(direction.z)>.005){
            vec3 point=transpose(uBasis)*(uPerspective?vec3(0.,0.,-uDistance):vec3(screenPoint,0.));
            float distance=(uFloor-point.z)/direction.z;point+=direction*distance;
            if(uStudioFloor&&(!uPerspective||distance>0.)){float horizon=1.-smoothstep(2.,8.,length(point.xy));vec3 floorColor=uFloorColor;if(uGlossyFloor)floorColor+=vec3(.12)*exp(-dot(point.xy-vec2(-.5,-.5),point.xy-vec2(-.5,-.5))*.3);color=mix(color,floorColor,horizon*uFloorOpacity);}
            vec2 coord=(point.xy-uShift)/uStep;
            vec2 minor=abs(fract(coord-.5)-.5)/max(fwidth(coord),vec2(.0001));
            vec2 major=abs(fract(coord/5.-.5)-.5)/max(fwidth(coord/5.),vec2(.0001));
            float fine=1.-smoothstep(.35,1.,min(minor.x,minor.y));
            float bold=1.-smoothstep(.5,1.5,min(major.x,major.y));
            float fade=1.-smoothstep(3.,7.,length(point.xy));
            if(uGrid)color=mix(color,mix(vec3(.48),vec3(.65),dark),max(fine*.30,bold*.65)*fade);
          }
        }
        if(uAxes){color=mix(color,vec3(.95,.12,.15),axis(vec3(1.,0.,0.)));color=mix(color,vec3(.10,.71,.25),axis(vec3(0.,1.,0.)));color=mix(color,vec3(0.,.48,.95),axis(vec3(0.,0.,1.)));}
        outColor=vec4(color,alpha);
      }`;
    const shaders=[];let linked=null;
    try {
      for(const [type,source] of [[gl.VERTEX_SHADER,vertexSource],[gl.FRAGMENT_SHADER,fragmentSource]]) {
        const shader=gl.createShader(type);if(!shader)fail('Unable to allocate a backdrop shader.');shaders.push(shader);gl.shaderSource(shader,source);gl.compileShader(shader);
        if(!gl.getShaderParameter(shader,gl.COMPILE_STATUS))fail('Backdrop shader compilation failed: '+gl.getShaderInfoLog(shader));
      }
      linked=gl.createProgram();if(!linked)fail('Unable to allocate the backdrop program.');for(const shader of shaders)gl.attachShader(linked,shader);gl.linkProgram(linked);
      if(!gl.getProgramParameter(linked,gl.LINK_STATUS))fail('Backdrop shader linking failed: '+gl.getProgramInfoLog(linked));
      const result={program:linked};
      for(const name of ['uTop','uBottom','uBasis','uScale','uPan','uViewport','uGrid','uAxes','uFloor','uStep','uShift','uOrigin','uDistance','uPerspective','uStudioFloor','uFloorColor','uFloorOpacity','uGlossyFloor'])result[name]=gl.getUniformLocation(linked,name);
      return result;
    } catch(error){if(linked)gl.deleteProgram(linked);throw error;}
    finally {for(const shader of shaders)gl.deleteShader(shader);}
  }
  class CadRenderer {
    constructor(canvas,{onPick=()=>{},onPointPick=()=>{},onPointMeasurement=()=>{},onHover=()=>{},onView=()=>{},onCamera=()=>{},onError=()=>{},onReady=()=>{},onAnnotations=()=>{},now=defaultNow,reducedMotion=defaultReducedMotion}={}) {
      if(!canvas||typeof canvas.getContext!=='function')fail('A canvas is required.');
      this.canvas=canvas;this.callbacks={onPick,onHover,onView,onCamera,onError,onReady,onAnnotations};this.annotations=[];this.camera=cloneCamera(DEFAULT_CAMERA);this.insets={top:0,right:0,bottom:0,left:0};this.view=null;this.theme=defaultTheme();this.anim=null;this.now=now;this.reducedMotion=reducedMotion;this.mode='auto';this.hover=null;this.hoverPending=null;this.hoverPoint=null;this.hoverResumeAt=0;this.model=null;this.fullModel=null;this.presentation=defaultPresentation();this.appearance=defaultAppearance();this.hiddenPartIds=[];this.sectionResult=null;this.selection=null;this.gl=null;this.resources=null;this.destroyed=false;this.lost=false;this.ready=false;this.pending=null;this.listeners=[];this.drag=null;
      this.render=renderSettings();this.renderBackgroundExplicit=false;this.display={grid:true,axes:true,edges:false};
      Object.assign(this.callbacks,{onPointPick,onPointMeasurement});this.measurementPoints=null;this.pointPicking=false;this.measurementActive=false;this.lastPointPick=false;
      this._listen(canvas,'webglcontextlost',event=>{event.preventDefault();this.lost=true;this.resources=null;this.callbacks.onAnnotations([]);this.callbacks.onPointMeasurement(null);this._error(new Error('WebGL context was lost. Waiting for graphics recovery.'));});
      this._listen(canvas,'webglcontextrestored',()=>{this.lost=false;try{this._init();if(this.model)this._upload();this._schedule();}catch(error){this._error(error);}});
      this._controls();
      if(typeof ResizeObserver!=='undefined'){this.observer=new ResizeObserver(()=>this._schedule());this.observer.observe(canvas);}else if(globalThis.addEventListener)this._listen(globalThis,'resize',()=>this._schedule());
      try{this._init();this._schedule();}catch(error){this._error(error);}
    }
    _error(error){this.ready=false;this.callbacks.onError(error instanceof Error?error:new Error(String(error)));}
    _checked(action){try{if(this.destroyed)fail('The CAD viewport has been disposed.');return action();}catch(error){this._error(error);throw error;}}
    _listen(target,type,handler,options){target.addEventListener(type,handler,options);this.listeners.push(()=>target.removeEventListener(type,handler,options));}
    _init() {
      const options={alpha:false,antialias:true,depth:true,stencil:false,preserveDrawingBuffer:false,powerPreference:'high-performance'};
      this.gl=this.canvas.getContext('webgl2',options);this.webgl2=!!this.gl;
      if(!this.gl)this.gl=this.canvas.getContext('webgl',options);
      if(!this.gl)fail('WebGL is unavailable. Enable browser hardware acceleration or use a browser with WebGL support.');
      const gl=this.gl,shader=program(gl,this.webgl2),uniforms={},attributes={};
      for(const name of ['uBasis','uScale','uPan','uSelected','uLines','uHover','uLine','uSelectColor','uHoverColor','uLineShift','uDepth','uClipping','uClip','uClipSign','uSection','uSectionNormal','uDistance','uPerspective','uLighting','uKey','uFill','uExposure','uShadow','uAlpha','uTexture','uSampler','uEmissive','uAlphaCutoff','uOpaque'])uniforms[name]=gl.getUniformLocation(shader,name);
      for(const name of ['aPosition','aNormal','aEntity','aColor','aMaterial','aUV'])attributes[name]=gl.getAttribLocation(shader,name);
      this.resources={program:shader,uniforms,attributes,faces:null,edges:null,ranges:new Map(),faceCount:0,edgeCount:0,capFaces:null,capEdges:null,colors:null,materials:null,uvs:null,shadow:null,textures:new Map(),texturePixels:0,groups:[],capFaceCount:0,capEdgeCount:0,backdrop:null};
      if(this.webgl2){try{this.resources.backdrop=backdropProgram(gl);}catch{this.resources.backdrop=null;}}
      this.maxDimension=Math.min(4096,gl.getParameter(gl.MAX_RENDERBUFFER_SIZE)||4096,gl.getParameter(gl.MAX_VIEWPORT_DIMS)?.[0]||4096,gl.getParameter(gl.MAX_VIEWPORT_DIMS)?.[1]||4096);
    }
    _deleteBuffers(keys=['faces','edges','capFaces','capEdges','colors','materials','uvs','shadow']){if(!this.resources||!this.gl)return;for(const key of keys)if(this.resources[key]){this.gl.deleteBuffer(this.resources[key]);this.resources[key]=null;}}
    _upload() {
      if(!this.gl||!this.resources||this.lost)return;
      const data=gpuData(this.model),gl=this.gl,r=this.resources;this._deleteBuffers(['faces','edges']);
      try {
        for(const key of ['faces','edges']) {const buffer=gl.createBuffer();if(!buffer)fail('Unable to allocate geometry buffers.');r[key]=buffer;gl.bindBuffer(gl.ARRAY_BUFFER,buffer);gl.bufferData(gl.ARRAY_BUFFER,data[key],gl.STATIC_DRAW);}
        if(gl.getError()!==gl.NO_ERROR)fail('The graphics device could not load this mesh. Inspect a smaller feature.');
        r.faceCount=data.faces.length/7;r.edgeCount=data.edges.length/4;r.ranges=data.ranges;
      } catch(error){this._deleteBuffers(['faces','edges']);throw error;}
      this._uploadAppearance();this._uploadSection();this._uploadStudio();
    }
    _uploadAppearance() {
      if(!this.gl||!this.resources||this.lost)return;
      const gl=this.gl,r=this.resources;this._deleteBuffers(['colors','materials','uvs']);const colors=appearanceData(this.model,this.appearance),data=materialData(this.model,this.appearance,this.render);r.groups=data.groups;
      for(const [key,values] of [['colors',colors],['materials',data.materials],['uvs',data.uvs]])if(values.length){const buffer=gl.createBuffer();if(!buffer)fail('Unable to allocate appearance buffers.');r[key]=buffer;gl.bindBuffer(gl.ARRAY_BUFFER,buffer);gl.bufferData(gl.ARRAY_BUFFER,values,gl.STATIC_DRAW);}
      if(gl.getError()!==gl.NO_ERROR)fail('The graphics device could not load surface appearance.');this._loadTextures();
    }
    _uploadStudio(){if(!this.gl||!this.resources||!this.model)return;this._deleteBuffers(['shadow']);const data=studioGeometry(this.model,this.render);if(!data.length)return;const gl=this.gl,r=this.resources;r.shadow=gl.createBuffer();gl.bindBuffer(gl.ARRAY_BUFFER,r.shadow);gl.bufferData(gl.ARRAY_BUFFER,data,gl.STATIC_DRAW);r.shadowCount=data.length/7;}
    _loadTextures(){
      if(typeof Image==='undefined')return;const gl=this.gl,r=this.resources,active=new Set(r.groups.map(group=>this.model.geometries.face.get(group.owner)?.material?.base_color_texture?.sha256).filter(Boolean));for(const [key,entry] of r.textures)if(!active.has(key)){if(entry.texture)gl.deleteTexture(entry.texture);r.texturePixels-=entry.pixels||0;r.textures.delete(key);}
      for(const group of r.groups){const tex=this.model.geometries.face.get(group.owner)?.material?.base_color_texture;if(!tex||r.textures.has(tex.sha256))continue;
        if(typeof tex.image_data_uri!=='string'||tex.image_data_uri.length>24*1024*1024||!/^data:image\/(png|jpeg|webp);base64,[A-Za-z0-9+/=]+$/.test(tex.image_data_uri))fail('Unsupported embedded surface texture.');
        const entry={texture:null,pending:true},img=new Image();r.textures.set(tex.sha256,entry);entry.promise=new Promise(resolve=>{img.onload=()=>{if(this.resources!==r||this.destroyed||r.textures.get(tex.sha256)!==entry){resolve();return;}if(img.width>4096||img.height>4096||r.texturePixels+img.width*img.height>16*1024*1024){entry.pending=false;entry.error='Texture exceeds the 4096 dimension or 16 megapixel scene budget';resolve();this._error(new Error(entry.error));return;}const texture=gl.createTexture();gl.bindTexture(gl.TEXTURE_2D,texture);gl.pixelStorei(gl.UNPACK_FLIP_Y_WEBGL,false);gl.texImage2D(gl.TEXTURE_2D,0,gl.RGBA,gl.RGBA,gl.UNSIGNED_BYTE,img);const pot=n=>(n&(n-1))===0,repeat=this.webgl2||(pot(img.width)&&pot(img.height));const min=tex.sampler?.min_filter||gl.LINEAR,mag=tex.sampler?.mag_filter||gl.LINEAR;if(repeat&&min>=9984)gl.generateMipmap(gl.TEXTURE_2D);gl.texParameteri(gl.TEXTURE_2D,gl.TEXTURE_MIN_FILTER,repeat?min:(min===gl.NEAREST?gl.NEAREST:gl.LINEAR));gl.texParameteri(gl.TEXTURE_2D,gl.TEXTURE_MAG_FILTER,mag);gl.texParameteri(gl.TEXTURE_2D,gl.TEXTURE_WRAP_S,repeat?(tex.sampler?.wrap_s||gl.REPEAT):gl.CLAMP_TO_EDGE);gl.texParameteri(gl.TEXTURE_2D,gl.TEXTURE_WRAP_T,repeat?(tex.sampler?.wrap_t||gl.REPEAT):gl.CLAMP_TO_EDGE);entry.texture=texture;entry.pixels=img.width*img.height;r.texturePixels+=entry.pixels;entry.pending=false;resolve();this._schedule();};img.onerror=()=>{if(r.textures.get(tex.sha256)!==entry){resolve();return;}entry.pending=false;entry.error='Embedded texture could not decode';resolve();this._error(new Error(entry.error));};img.src=tex.image_data_uri;});
      }
    }
    async whenTexturesReady(){await Promise.all([...this.resources?.textures?.values()||[]].map(t=>t.promise));const error=[...this.resources?.textures?.values()||[]].find(t=>t.error);if(error)fail(error.error);}
    _uploadSection() {
      if(!this.gl||!this.resources||this.lost)return;
      const gl=this.gl,r=this.resources;this._deleteBuffers(['capFaces','capEdges']);r.capFaceCount=0;r.capEdgeCount=0;
      if(!this.model?.section)return;
      const data=gpuData(this.model.section);
      try {
        for(const [key,values] of [['capFaces',data.faces],['capEdges',data.edges]])if(values.length){const buffer=gl.createBuffer();if(!buffer)fail('Unable to allocate section buffers.');r[key]=buffer;gl.bindBuffer(gl.ARRAY_BUFFER,buffer);gl.bufferData(gl.ARRAY_BUFFER,values,gl.STATIC_DRAW);}
        if(gl.getError()!==gl.NO_ERROR)fail('The graphics device could not load the native section.');
        r.capFaceCount=data.faces.length/7;r.capEdgeCount=data.edges.length/4;
      }catch(error){this._deleteBuffers(['capFaces','capEdges']);throw error;}
    }
    _schedule(){if(this.destroyed||this.pending!==null)return;this.pending=requestAnimationFrame(()=>{this.pending=null;try{this._stepAnimation();this._projection();this._draw();}catch(error){this._error(error);}});}
    _size(){const rect=this.canvas.getBoundingClientRect();return {width:Math.max(1,rect.width),height:Math.max(1,rect.height)};}
    _draw() {
      if(this.destroyed||this.lost||!this.gl||!this.resources)return;
      // Orientation only: the cube ignores pan and zoom. Fires for every cause of a change (drag, animation, saved state).
      if(!this.view||this.view.yaw!==this.camera.yaw||this.view.pitch!==this.camera.pitch){this.view={yaw:this.camera.yaw,pitch:this.camera.pitch};this.callbacks.onView(this.getCamera());}
      const {width,height}=this._size(),ratio=Math.min(globalThis.devicePixelRatio||1,2,this.maxDimension/width,this.maxDimension/height,Math.sqrt(MAX_PIXELS/(width*height)));
      const w=Math.max(1,Math.floor(width*ratio)),h=Math.max(1,Math.floor(height*ratio)),gl=this.gl,r=this.resources;
      if(this.canvas.width!==w||this.canvas.height!==h){this.canvas.width=w;this.canvas.height=h;}
      const theme=this.renderBackgroundExplicit?{...this.theme,background:{top:this.render.background.color,bottom:this.render.background.color}}:this.theme,mean=theme.background.top.map((v,i)=>(v+theme.background.bottom[i])/2);
      gl.viewport(0,0,w,h);gl.clearColor(mean[0],mean[1],mean[2],1);gl.clear(gl.COLOR_BUFFER_BIT|gl.DEPTH_BUFFER_BIT);
      this._drawBackdrop();
      this._annotationLayout();
      this._pointMeasurementLayout();this._drawArtifactOverlay();
      if(!this.model||!r.faces)return;
      gl.enable(gl.DEPTH_TEST);gl.depthFunc(gl.LEQUAL);gl.disable(gl.CULL_FACE);gl.disable(gl.BLEND);gl.useProgram(r.program);
      const b=basis(this.camera),size=Math.min(width,height),scale=size*.68*this.camera.zoom,u=r.uniforms,a=r.attributes;
      gl.uniformMatrix3fv(u.uBasis,false,new Float32Array([b[0][0],b[1][0],b[2][0],b[0][1],b[1][1],b[2][1],b[0][2],b[1][2],b[2][2]]));
      gl.uniform2f(u.uScale,2*scale/width,2*scale/height);gl.uniform2f(u.uPan,2*this.camera.pan[0]*size/width,-2*this.camera.pan[1]*size/height);
      gl.uniform1f(u.uDepth,this.model.depthExtent??2);gl.uniform1f(u.uDistance,this.camera.distance||3);gl.uniform1i(u.uPerspective,this.render.projection==='perspective');gl.uniform1i(u.uLighting,this.render.lighting.enabled);gl.uniform1f(u.uFill,this.render.lighting.fill);gl.uniform1f(u.uExposure,this.render.lighting.exposure);const key=studioLight(this.render);gl.uniform3f(u.uKey,...b.map(row=>dot(row,key)));gl.uniform1i(u.uShadow,false);gl.uniform1i(u.uTexture,false);gl.uniform1f(u.uAlphaCutoff,0);gl.uniform1i(u.uOpaque,false);gl.uniform3f(u.uEmissive,0,0,0);gl.uniform1i(u.uSampler,0);gl.uniform1i(u.uClipping,!!this.model.clip);
      const clip=this.model.clip;gl.uniform4f(u.uClip,...(clip?.normal||[0,0,1]),clip?.offset||0);gl.uniform1f(u.uClipSign,clip?.sign||1);
      gl.uniform3f(u.uLine,...theme.line);gl.uniform3f(u.uSelectColor,...theme.select);gl.uniform3f(u.uHoverColor,...theme.hover);gl.uniform2f(u.uLineShift,0,0);gl.uniform1i(u.uSection,false);
      const outward=(clip?.normal||[0,0,1]).map(v=>-v*(clip?.sign||1));gl.uniform3f(u.uSectionNormal,...b.map(row=>dot(row,outward)));
      if(r.colors){gl.bindBuffer(gl.ARRAY_BUFFER,r.colors);gl.enableVertexAttribArray(a.aColor);gl.vertexAttribPointer(a.aColor,3,gl.FLOAT,false,12,0);}
      else{gl.disableVertexAttribArray(a.aColor);gl.vertexAttrib3f(a.aColor,...this.appearance.default_color);}
      if(r.materials){gl.bindBuffer(gl.ARRAY_BUFFER,r.materials);gl.enableVertexAttribArray(a.aMaterial);gl.vertexAttribPointer(a.aMaterial,3,gl.FLOAT,false,12,0);}else{gl.disableVertexAttribArray(a.aMaterial);gl.vertexAttrib3f(a.aMaterial,1,.5,0);}
      if(r.uvs){gl.bindBuffer(gl.ARRAY_BUFFER,r.uvs);gl.enableVertexAttribArray(a.aUV);gl.vertexAttribPointer(a.aUV,2,gl.FLOAT,false,8,0);}else{gl.disableVertexAttribArray(a.aUV);gl.vertexAttrib2f(a.aUV,0,0);}
      gl.bindBuffer(gl.ARRAY_BUFFER,r.faces);
      gl.enableVertexAttribArray(a.aPosition);gl.vertexAttribPointer(a.aPosition,3,gl.FLOAT,false,28,0);
      gl.enableVertexAttribArray(a.aNormal);gl.vertexAttribPointer(a.aNormal,3,gl.FLOAT,false,28,12);
      gl.enableVertexAttribArray(a.aEntity);gl.vertexAttribPointer(a.aEntity,1,gl.FLOAT,false,28,24);
      gl.uniform1i(u.uLines,false);gl.uniform1f(u.uSelected,this.selection?.kind==='face'?this.model.faceNumbers.get(this.selection.entity_id):-1);gl.uniform1f(u.uHover,this.hover?.kind==='face'?(this.model.faceNumbers.get(this.hover.entity_id)??-1):-1);
      if(r.shadow&&this.render.floor.enabled){gl.bindBuffer(gl.ARRAY_BUFFER,r.shadow);gl.vertexAttribPointer(a.aPosition,3,gl.FLOAT,false,28,0);gl.vertexAttribPointer(a.aNormal,3,gl.FLOAT,false,28,12);gl.vertexAttribPointer(a.aEntity,1,gl.FLOAT,false,28,24);gl.uniform1i(u.uShadow,true);gl.uniform1i(u.uClipping,false);gl.enable(gl.BLEND);gl.blendFunc(gl.SRC_ALPHA,gl.ONE_MINUS_SRC_ALPHA);gl.depthMask(false);gl.uniform1f(u.uAlpha,.055*this.render.floor.opacity);for(const [dx,dy] of [[0,0],[2,0],[-2,0],[0,2],[0,-2],[3,3],[-3,3],[3,-3],[-3,-3]]){gl.uniform2f(u.uLineShift,dx*2/w,dy*2/h);gl.drawArrays(gl.TRIANGLES,0,r.shadowCount);}gl.uniform2f(u.uLineShift,0,0);gl.uniform1i(u.uShadow,false);gl.uniform1i(u.uClipping,!!clip);gl.depthMask(true);gl.disable(gl.BLEND);gl.bindBuffer(gl.ARRAY_BUFFER,r.faces);gl.vertexAttribPointer(a.aPosition,3,gl.FLOAT,false,28,0);gl.vertexAttribPointer(a.aNormal,3,gl.FLOAT,false,28,12);gl.vertexAttribPointer(a.aEntity,1,gl.FLOAT,false,28,24);}
      gl.enable(gl.POLYGON_OFFSET_FILL);gl.polygonOffset(1,1);
      const depthOnly=this.render.mode==='hidden_line',off=this.render.mode==='wireframe';if(depthOnly)gl.colorMask(false,false,false,false);
      if(!off){const opaque=[],transparent=[];for(const group of r.groups){(group.style.opacity<1||this.model.geometries.face.get(group.owner)?.material?.alpha_mode==='BLEND'?transparent:opaque).push(group);}const depth=g=>{let z=0;for(let i=g.start;i<g.start+g.count;i++)z+=dot(b[2],vertex(this.model.positions,this.model.indices[i]));return z/g.count;};transparent.sort((a,b)=>depth(b)-depth(a));
        for(const [groups,alpha] of [[opaque,false],[transparent,true]]){if(alpha){gl.enable(gl.BLEND);gl.blendFunc(gl.SRC_ALPHA,gl.ONE_MINUS_SRC_ALPHA);gl.depthMask(false);}for(const group of groups){const authored=this.model.geometries.face.get(group.owner)?.material;if(authored&&authored.double_sided===false){gl.enable(gl.CULL_FACE);gl.cullFace(gl.BACK);}else gl.disable(gl.CULL_FACE);const tex=authored?.base_color_texture,texture=tex?r.textures.get(tex.sha256)?.texture:null;gl.uniform1i(u.uTexture,!!texture);if(texture){gl.activeTexture(gl.TEXTURE0);gl.bindTexture(gl.TEXTURE_2D,texture);}gl.uniform1i(u.uOpaque,group.style.alpha_mode==='OPAQUE');gl.uniform3f(u.uEmissive,...group.style.emissive);gl.uniform1f(u.uAlphaCutoff,group.style.alpha_mode==='MASK'?group.style.alpha_cutoff:0);gl.drawArrays(gl.TRIANGLES,group.start,group.count);}if(alpha){gl.depthMask(true);gl.disable(gl.BLEND);}}}
      gl.colorMask(true,true,true,true);gl.uniform1i(u.uTexture,false);gl.uniform3f(u.uEmissive,0,0,0);gl.uniform1f(u.uAlphaCutoff,0);gl.disable(gl.POLYGON_OFFSET_FILL);
      gl.disableVertexAttribArray(a.aColor);gl.vertexAttrib3f(a.aColor,...this.appearance.default_color);
      if(r.capFaces&&this.render.mode!=='wireframe'){
        gl.disableVertexAttribArray(a.aMaterial);gl.vertexAttrib3f(a.aMaterial,1,.5,0);
        gl.uniform1i(u.uSection,true);gl.uniform1i(u.uClipping,false);gl.uniform1f(u.uSelected,-1);gl.bindBuffer(gl.ARRAY_BUFFER,r.capFaces);
        gl.vertexAttribPointer(a.aPosition,3,gl.FLOAT,false,28,0);gl.vertexAttribPointer(a.aNormal,3,gl.FLOAT,false,28,12);gl.vertexAttribPointer(a.aEntity,1,gl.FLOAT,false,28,24);
        gl.drawArrays(gl.TRIANGLES,0,r.capFaceCount);gl.uniform1i(u.uSection,false);gl.uniform1i(u.uClipping,!!clip);
      }
      gl.bindBuffer(gl.ARRAY_BUFFER,r.edges);gl.vertexAttribPointer(a.aPosition,3,gl.FLOAT,false,16,0);gl.vertexAttribPointer(a.aEntity,1,gl.FLOAT,false,16,12);gl.disableVertexAttribArray(a.aNormal);gl.vertexAttrib3f(a.aNormal,0,0,1);
      const selected=this.selection?.kind==='edge'?r.ranges.get(this.selection.entity_id):null;
      const hovered=this.hover?.kind==='edge'&&!(selected&&this.selection.entity_id===this.hover.entity_id)?r.ranges.get(this.hover.entity_id):null;
      gl.disableVertexAttribArray(a.aMaterial);gl.vertexAttrib3f(a.aMaterial,1,.5,0);if(['xray','wireframe'].includes(this.render.mode))gl.disable(gl.DEPTH_TEST);
      gl.uniform1i(u.uLines,true);gl.uniform1f(u.uSelected,selected?.number??-1);gl.uniform1f(u.uHover,hovered?.number??-1);gl.lineWidth(1);if(this.display.edges)gl.drawArrays(gl.LINES,0,r.edgeCount);
      // Most GPUs cap wide lines at 1px, so highlighted edges are re-drawn at small
      // screen-space offsets (px is device pixels per CSS pixel) to read as thicker.
      const thick=(range,radius)=>{for(const [dx,dy] of [[0,0],[1,0],[-1,0],[0,1],[0,-1]]){gl.uniform2f(u.uLineShift,2*dx*radius*ratio/w,-2*dy*radius*ratio/h);gl.drawArrays(gl.LINES,range.start,range.count);}gl.uniform2f(u.uLineShift,0,0);};
      if(hovered)thick(hovered,.8);
      if(selected)thick(selected,1.3);
      if(r.capEdges){
        gl.uniform1i(u.uSection,true);gl.uniform1i(u.uClipping,false);gl.uniform1f(u.uSelected,-1);gl.bindBuffer(gl.ARRAY_BUFFER,r.capEdges);
        gl.vertexAttribPointer(a.aPosition,3,gl.FLOAT,false,16,0);gl.vertexAttribPointer(a.aEntity,1,gl.FLOAT,false,16,12);gl.drawArrays(gl.LINES,0,r.capEdgeCount);
        gl.uniform1i(u.uSection,false);gl.uniform1i(u.uClipping,!!clip);
      }
      if(!this.ready){this.ready=true;this.callbacks.onReady({...this.model.identity});}
    }
    _drawArtifactOverlay(){
      const layout=artifactOverlay(this.model,this.camera,...Object.values(this._size()));this.artifactOverlayLayout=layout;
      if(!layout.texts.length&&!layout.fills.length&&!this.overlayCanvas)return;
      if(!this.overlayCanvas){const document=this.canvas.ownerDocument;if(!document||!this.canvas.parentElement)return;const overlay=document.createElement('canvas');Object.assign(overlay.style,{position:'absolute',pointerEvents:'none',zIndex:'1'});overlay.setAttribute('aria-hidden','true');this.canvas.parentElement.append(overlay);this.overlayCanvas=overlay;}
      const overlay=this.overlayCanvas,{width,height}=this._size();overlay.width=this.canvas.width;overlay.height=this.canvas.height;Object.assign(overlay.style,{left:this.canvas.offsetLeft+'px',top:this.canvas.offsetTop+'px',width:width+'px',height:height+'px'});const ctx=overlay.getContext('2d');ctx.clearRect(0,0,overlay.width,overlay.height);ctx.scale(overlay.width/width,overlay.height/height);paintArtifactOverlay(ctx,layout);
    }
    _annotationLayout(){const {width,height}=this._size();const layout=this.destroyed||this.lost||!this.gl||!this.resources?[]:annotationLayout(this.annotations,this.fullModel,this.presentation,this.camera,width,height,this.hiddenPartIds);this.callbacks.onAnnotations(layout);return layout;}
    _pointMeasurementLayout(){const {width,height}=this._size();const layout=this.destroyed||this.lost||!this.gl||!this.resources?null:pointMeasurementLayout(this.measurementPoints,this.fullModel,this.camera,width,height);this.callbacks.onPointMeasurement(layout);return layout;}
    setPointMeasurement(points,picking=false,active=picking){return this._checked(()=>{
      for(const slot of ['a','b'])if(points?.[slot]&&!finitePoint(points[slot]))fail('Invalid measurement point.');
      this.pointPicking=!!picking;this.measurementActive=!!active;if(this.measurementActive)this._setHover(null);this.canvas.style&&(this.canvas.style.cursor=this.measurementActive?'crosshair':'');
      if(JSON.stringify(points)===JSON.stringify(this.measurementPoints))return;
      this.measurementPoints=points?{a:points.a?[...points.a]:null,b:points.b?[...points.b]:null}:null;this._pointMeasurementLayout();this._schedule();
    });}
    _clearPointMeasurement(){this.measurementPoints=null;this.pointPicking=false;this.measurementActive=false;this.lastPointPick=false;if(this.canvas.style)this.canvas.style.cursor='';this.callbacks.onPointMeasurement(null);}
    setAnnotations(notes){return this._checked(()=>{this.annotations=annotations(notes,this.fullModel,{draft:!!this.draft});this._annotationLayout();this._schedule();});}
    _cameraChanged(){this._schedule();this.callbacks.onCamera(this.getCamera());}
    _controls() {
      const canvas=this.canvas;
      this._listen(canvas,'contextmenu',event=>event.preventDefault());
      this._listen(canvas,'pointerdown',event=>{
        if(this.drag||event.button>2)return;event.preventDefault();canvas.focus({preventScroll:true});this._cancelAnimation();
        this.drag={id:event.pointerId,x:event.clientX,y:event.clientY,lastX:event.clientX,lastY:event.clientY,moved:false,button:event.button,pan:event.button===1||event.shiftKey};
        canvas.setPointerCapture(event.pointerId);
      });
      this._listen(canvas,'pointermove',event=>{
        if(!this.drag&&event.buttons===0){const rect=canvas.getBoundingClientRect();this.hoverPoint={x:event.clientX-rect.left,y:event.clientY-rect.top};this._scheduleHover();}
        const drag=this.drag;if(!drag||drag.id!==event.pointerId)return;
        if(Math.hypot(event.clientX-drag.x,event.clientY-drag.y)>3)drag.moved=true;
        if(drag.moved){this.hoverPoint=null;this._setHover(null);const dx=event.clientX-drag.lastX,dy=event.clientY-drag.lastY,{width,height}=this._size();
          if(drag.pan){this.camera.pan[0]+=dx/Math.min(width,height);this.camera.pan[1]+=dy/Math.min(width,height);}else{this.camera.yaw+=dx*.008;this.camera.pitch+=dy*.008;}
          this.camera=camera(this.camera);this._cameraChanged();}
        drag.lastX=event.clientX;drag.lastY=event.clientY;
      });
      this._listen(canvas,'pointerup',event=>{
        const drag=this.drag;if(!drag||drag.id!==event.pointerId)return;this.drag=null;
        if(canvas.hasPointerCapture(event.pointerId))canvas.releasePointerCapture(event.pointerId);
        if(!drag.moved&&!drag.pan&&drag.button===0){const rect=canvas.getBoundingClientRect();this._pick(event.clientX-rect.left,event.clientY-rect.top);}
      });
      this._listen(canvas,'pointerleave',()=>{this.hoverPoint=null;this._setHover(null);});
      this._listen(canvas,'pointercancel',()=>{this.drag=null;});
      this._listen(canvas,'lostpointercapture',()=>{this.drag=null;});
      this._listen(canvas,'wheel',event=>{
        event.preventDefault();this._cancelAnimation();
        const delta=event.deltaY*(event.deltaMode===1?16:event.deltaMode===2?this._size().height:1),rect=canvas.getBoundingClientRect(),{width,height}=this._size();
        this.camera=camera(zoomAbout(this.camera,Math.exp(-delta*.001),event.clientX-rect.left,event.clientY-rect.top,width,height));this._cameraChanged();
      },{passive:false});
      this._listen(canvas,'dblclick',event=>{
        event.preventDefault();if(!this.model||this.lost||this.measurementActive||this.lastPointPick)return;
        const rect=canvas.getBoundingClientRect(),{width,height}=this._size(),result=pick(this.model,this.camera,width,height,event.clientX-rect.left,event.clientY-rect.top,'auto');
        if(result.id)this._frame(result.kind,result.id);else this.fitAll();
      });
      this._listen(canvas,'keydown',event=>{
        let handled=true;
        if(event.key.startsWith('Arrow')||event.key==='+'||event.key==='='||event.key==='-')this._cancelAnimation();
        if(event.key.startsWith('Arrow')) {
          const dx=event.key==='ArrowLeft'?-1:event.key==='ArrowRight'?1:0,dy=event.key==='ArrowUp'?-1:event.key==='ArrowDown'?1:0;
          if(event.shiftKey){this.camera.pan[0]+=.05*dx;this.camera.pan[1]+=.05*dy;}else{this.camera.yaw+=dx*.1;this.camera.pitch-=dy*.1;}
        } else if(event.key==='+'||event.key==='=')this.camera.zoom*=1.15;
        else if(event.key==='-')this.camera.zoom/=1.15;
        else if(event.key===' '){event.preventDefault();this.frameSelection();return;}
        else if(Object.hasOwn(DIGIT_VIEWS,event.key)){event.preventDefault();this.setView(DIGIT_VIEWS[event.key],{animate:true});return;}
        else if(event.key==='Home'||event.key==='0'){event.preventDefault();this.reset();return;}
        else if(event.key==='Escape'){this.selection=null;this.callbacks.onPick(null,{ambiguous:false,message:''});}
        else handled=false;
        if(handled){event.preventDefault();this.camera=camera(this.camera);this._cameraChanged();}
      });
    }
    _pick(x,y) {
      if(!this.model||this.lost||!this.gl)return;
      try {
        this.lastPointPick=this.measurementActive;if(this.measurementActive){
          const {width,height}=this._size();
          if(!this.pointPicking){const layout=pointMeasurementLayout(this.measurementPoints,this.fullModel,this.camera,width,height),slots=['a','b'].filter(slot=>layout?.[slot]).sort((a,b)=>Math.hypot(x-layout[a][0],y-layout[a][1])-Math.hypot(x-layout[b][0],y-layout[b][1]));const slot=slots[0];if(slot&&Math.hypot(x-layout[slot][0],y-layout[slot][1])<=12){this.callbacks.onPointPick(null,{endpoint:slot});return;}}
          const hit=pickMeasurementPoint(this.model,this.camera,width,height,x,y);this.callbacks.onPointPick(hit?.point_mm||null,{input:hit?.input});return;
        }
        const {width,height}=this._size(),result=pick(this.model,this.camera,width,height,x,y,this.mode),kind=this.mode==='auto'?result.kind:this.mode;
        this.selection=result.id?{...this.model.identity,kind,entity_id:result.id}:null;this._schedule();
        const selection=this.selection?{reference:this.model.read_only?{review_sha256:this.model.identity.review_sha256,kind:kind==='face'?'mesh_group':'curve',entity_id:result.id}:{...this.selection},geometry:this.model.geometries[kind].get(result.id)}:null;
        this.callbacks.onPick(selection,{artifact:!!this.model.read_only,ambiguous:result.ambiguous,section:!!result.section,message:result.section?'This is a section surface. Pick an original face or edge to edit the part.':result.ambiguous?'Geometry overlaps here. Rotate the view or inspect a specific feature.':''});
      }catch(error){this.selection=null;this._schedule();this.callbacks.onPick(null,{ambiguous:false,message:error.message});this._error(error);}
    }
    _scheduleHover(){if(this.destroyed||this.hoverPending!==null)return;this.hoverPending=requestAnimationFrame(()=>{this.hoverPending=null;this._updateHover();});}
    _updateHover(){
      if(this.destroyed||this.lost||!this.model||this.drag||this.measurementActive||!this.hoverPoint)return;
      const started=this.now();
      if(started<this.hoverResumeAt){this._scheduleHover();return;}
      const {width,height}=this._size(),mode=this.mode,result=pick(this.model,this.camera,width,height,this.hoverPoint.x,this.hoverPoint.y,mode);
      this._setHover(result.id?{kind:mode==='auto'?result.kind:mode,entity_id:result.id}:null);
      const elapsed=this.now()-started;
      if(elapsed>HOVER_SLOW_MS)this.hoverResumeAt=started+elapsed*HOVER_BACKOFF;
    }
    _setHover(value){
      const same=(a,b)=>a===b||(a&&b&&a.kind===b.kind&&a.entity_id===b.entity_id);
      if(same(this.hover,value))return;
      this.hover=value;this._schedule();this.callbacks.onHover(value?{...value}:null);
    }
    load(evaluation,{preserveCamera=true,hiddenPartIds=[],presentation:settings=defaultPresentation(),appearance:style=defaultAppearance()}={}) {return this._checked(()=>{const full=evaluation?.read_only===true?prepareArtifact(evaluation):prepare(evaluation),value=presentation(settings,full),color=appearance(style,full),model=presentedModel(visibleModel(full,hiddenPartIds),value);this._clearPointMeasurement();this.fullModel=full;this.draft=!!evaluation.draft;this.annotations=[];this.callbacks.onAnnotations([]);this.sectionResult=null;this.presentation=value;this.appearance=color;this.hiddenPartIds=[...hiddenPartIds];this.model=model;this.selection=null;this._setHover(null);this.ready=false;if(!preserveCamera)this.camera=cloneCamera(DEFAULT_CAMERA);this._upload();this._schedule();});}
    setPresentation(value){return this._checked(()=>{
      if(!this.fullModel)return;
      const settings=presentation(value,this.fullModel);if(JSON.stringify(settings)===JSON.stringify(this.presentation))return;
      const geometryChanged=JSON.stringify(settings.explode)!==JSON.stringify(this.presentation.explode);
      const sectionChanged=JSON.stringify(sectionGeometry(settings))!==JSON.stringify(sectionGeometry(this.presentation));
      const model=geometryChanged?presentedModel(visibleModel(this.fullModel,this.hiddenPartIds),settings):{...this.model,clip:clipPlane(this.fullModel,settings.clip),presentation:settings};
      if(sectionChanged){this.sectionResult=null;delete model.section;}else if(this.sectionResult&&geometryChanged)model.section=sectionModel(this.sectionResult,this.fullModel,this.hiddenPartIds);
      this._clearPointMeasurement();this.presentation=settings;this.model=model;this.selection=null;this._setHover(null);if(geometryChanged)this._upload();else if(sectionChanged)this._uploadSection();if(!geometryChanged)this._uploadStudio();this._schedule();
    });}
    setHiddenParts(ids){return this._checked(()=>{
      if(!Array.isArray(ids))fail('Visibility must be an array of part IDs.');
      if(!this.fullModel){if(ids.length)fail('Load an assembly before hiding parts.');return;}
      const canonical=[...ids].sort();
      if(JSON.stringify(canonical)===JSON.stringify([...this.model.hiddenPartIds].sort()))return;
      const model=presentedModel(visibleModel(this.fullModel,ids),this.presentation);if(this.sectionResult)model.section=sectionModel(this.sectionResult,this.fullModel,ids);this._clearPointMeasurement();this.hiddenPartIds=[...ids];this.model=model;this._setHover(null);
      if(this.selection&&!model.geometries[this.selection.kind].has(this.selection.entity_id))this.selection=null;
      this._upload();this._schedule();
    });}
    setAppearance(value){return this._checked(()=>{
      if(!this.fullModel)return;
      const style=appearance(value,this.fullModel);if(JSON.stringify(style)===JSON.stringify(this.appearance))return;
      this.appearance=style;this._uploadAppearance();this._schedule();
    });}
    setSection(result){return this._checked(()=>{
      if(result===this.sectionResult)return;
      if(result!==null&&this.fullModel?.read_only)fail('Exact sections require an editable native document.');
      if(result!==null)validateSection(result,this.fullModel,this.presentation);
      const section=result?sectionModel(result,this.fullModel,this.hiddenPartIds):null;
      this.sectionResult=result;if(this.model)this.model={...this.model,section};this._uploadSection();this._schedule();
    });}
    setMode(mode){return this._checked(()=>{if(!['auto','face','edge'].includes(mode))fail('Selection mode must be auto, face or edge.');this.mode=mode;this.selection=null;this._setHover(null);this._schedule();});}
    _drawBackdrop(){
      const backdrop=this.resources?.backdrop;if(!backdrop)return;
      const gl=this.gl;gl.disable(gl.DEPTH_TEST);gl.useProgram(backdrop.program);
      // WebGL validates enabled arrays even for this attribute-less pass. After
      // visibility reuploads, the previous arrays can still name deleted buffers.
      for(const location of Object.values(this.resources.attributes)) gl.disableVertexAttribArray(location);
      const {width,height}=this._size(),b=basis(this.camera),scale=Math.min(width,height)*.68*this.camera.zoom,stage=stageSettings(this.fullModel);
      gl.uniformMatrix3fv(backdrop.uBasis,false,new Float32Array([b[0][0],b[1][0],b[2][0],b[0][1],b[1][1],b[2][1],b[0][2],b[1][2],b[2][2]]));
      gl.uniform2f(backdrop.uScale,2*scale/width,2*scale/height);gl.uniform2f(backdrop.uPan,2*this.camera.pan[0]*Math.min(width,height)/width,-2*this.camera.pan[1]*Math.min(width,height)/height);
      gl.uniform2f(backdrop.uViewport,width,height);gl.uniform1i(backdrop.uGrid,!!this.model&&this.display.grid);gl.uniform1i(backdrop.uAxes,!!this.model&&this.display.axes);
      gl.uniform1f(backdrop.uDistance,this.camera.distance||3);gl.uniform1i(backdrop.uPerspective,this.render.projection==='perspective');gl.uniform1i(backdrop.uStudioFloor,this.render.floor.enabled);gl.uniform3f(backdrop.uFloorColor,...this.render.floor.color);gl.uniform1f(backdrop.uFloorOpacity,this.render.floor.opacity);gl.uniform1i(backdrop.uGlossyFloor,this.render.floor.finish==='glossy');gl.uniform1f(backdrop.uFloor,stage.floor);gl.uniform1f(backdrop.uStep,stage.step);gl.uniform2f(backdrop.uShift,...stage.shift);gl.uniform3f(backdrop.uOrigin,...stage.origin);
      gl.uniform3f(backdrop.uTop,...(this.renderBackgroundExplicit?this.render.background.color:this.theme.background.top));gl.uniform3f(backdrop.uBottom,...(this.renderBackgroundExplicit?this.render.background.color:this.theme.background.bottom));gl.drawArrays(gl.TRIANGLES,0,3);
    }
    setDisplay(settings){return this._checked(()=>{
      if(!settings||Object.keys(settings).some(key=>!['grid','axes','edges'].includes(key)||typeof settings[key]!=='boolean'))fail('Display settings must be grid, axes or edges booleans.');
      Object.assign(this.display,settings);this._schedule();
    });}
    setRender(value){return this._checked(()=>{const settings=renderSettings(value);if(JSON.stringify(settings)===JSON.stringify(this.render))return;this.render=settings;this.renderBackgroundExplicit=this.render.mode==='render'||JSON.stringify(this.render.background.color)!==JSON.stringify(renderSettings({mode:this.render.mode}).background.color);Object.assign(this.display,{grid:this.render.grid,axes:this.render.axes,edges:this.render.edges});this._projection();if(this.model){this._uploadAppearance();this._uploadStudio();}this._schedule();});}
    getRender(){return JSON.parse(JSON.stringify(this.render));}
    _projection(){this.camera.projection=this.render.projection;this.camera.lens_mm=this.render.lens_mm;this.camera.distance=Math.max(3,(this.model?.depthExtent||2)+1)*(this.render.lens_mm/50);this.camera.distance=Math.max((this.model?.depthExtent||2)+.25,this.camera.distance);}
    setTheme(value){return this._checked(()=>{this.theme=themeValue(value);this._schedule();});}
    getTheme(){return themeValue(this.theme);}
    // CSS pixels covered by floating chrome on each side; used by fit, reset and frame.
    setInsets(value){return this._checked(()=>{
      const sides=['top','right','bottom','left'];
      if(!value||!sides.every(side=>Number.isFinite(value[side])&&value[side]>=0))fail('Insets must be four non-negative numbers.');
      this.insets={top:value.top,right:value.right,bottom:value.bottom,left:value.left};
    });}
    _cancelAnimation(){if(!this.anim)return;this.anim=null;this.callbacks.onCamera(this.getCamera());}
    animateTo(target,{duration=250}={}){return this._checked(()=>{
      const to=camera(target);
      if(duration<=0||this.reducedMotion()){this.anim=null;this.camera=to;this._cameraChanged();return;}
      this.anim={from:cloneCamera(this.camera),to,start:this.now(),duration};this._schedule();
    });}
    _stepAnimation(){
      const a=this.anim;if(!a)return;
      const t=(this.now()-a.start)/a.duration;
      if(t>=1){this.camera=cloneCamera(a.to);this.anim=null;this.callbacks.onCamera(this.getCamera());return;}
      this.camera=camera(lerpCamera(a.from,a.to,easeOut(t)));this._schedule();
    }
    fitAll(){if(!this.model?.bounds)return;const {width,height}=this._size();this.animateTo(fitCamera(this.camera,this.model.bounds,width,height,this.insets));}
    _frame(kind,id){const bounds=entityBounds(this.model,kind,id);if(!bounds)return;const {width,height}=this._size();this.animateTo(fitCamera(this.camera,bounds,width,height,this.insets));}
    frameSelection(){const target=this.hover||this.selection;if(target)this._frame(target.kind,target.entity_id);}
    setCamera(value){return this._checked(()=>{this.anim=null;this.camera=camera(value);this._cameraChanged();});}
    getCamera(){return {yaw:this.camera.yaw,pitch:this.camera.pitch,zoom:this.camera.zoom,pan:[...this.camera.pan]};}
    reset(){return this._checked(()=>{
      this.anim=null;const bounds=this.model?.bounds,home={...cloneCamera(DEFAULT_CAMERA),projection:this.render.projection,lens_mm:this.render.lens_mm,distance:this.camera.distance};
      if(bounds){const {width,height}=this._size();this.camera=camera(fitCamera(home,bounds,width,height,this.insets));}else this.camera=home;
      this._cameraChanged();
    });}
    setView(view,{animate=false}={}){return this._checked(()=>{
      if(!Object.hasOwn(STANDARD_VIEWS,view))fail('Unknown camera view.');
      const [yaw,pitch]=STANDARD_VIEWS[view];
      if(animate){this.animateTo({...this.camera,yaw,pitch});return;}
      this.anim=null;this.camera.yaw=yaw;this.camera.pitch=pitch;this._cameraChanged();
    });}
    setSelection(reference){return this._checked(()=>{
      if(reference!==null&&this.model?.read_only) {
        const kind=reference.kind==='mesh_group'?'face':reference.kind==='curve'?'edge':null;
        if(!kind||Object.keys(reference).length!==3||reference.review_sha256!==this.model.identity.review_sha256||!this.model.geometries[kind].has(reference.entity_id))fail('Artifact selection belongs to another review or unknown label.');
        this.selection={...this.model.identity,kind,entity_id:reference.entity_id};this._schedule();return;
      }
      if(reference!==null) {
        if(!this.model||!reference||!['face','edge'].includes(reference.kind)||!this.model.geometries[reference.kind].has(reference.entity_id)||Object.entries(this.model.identity).some(([key,value])=>reference[key]!==value))fail('Selection belongs to another evaluation or unknown entity.');
        if(this.selection?.kind===reference.kind&&this.selection.entity_id===reference.entity_id)return;
        this.selection={...this.model.identity,kind:reference.kind,entity_id:reference.entity_id};
      }else {if(this.selection===null)return;this.selection=null;}
      this._schedule();
    });}
    capture(){return this._checked(()=>{
      if(!this.gl||this.lost||!this.resources)fail('WebGL is unavailable for capture.');this._draw();
      const pins=this._annotationLayout(),measure=this._pointMeasurementLayout();if(!pins.length&&!measure&&!this.artifactOverlayLayout?.texts.length&&!this.artifactOverlayLayout?.fills.length)return this.canvas.toDataURL('image/png');
      const image=this.canvas.ownerDocument?.createElement('canvas');if(!image)fail('Image composition is unavailable for review pins.');
      image.width=this.canvas.width;image.height=this.canvas.height;const ctx=image.getContext('2d');if(!ctx)fail('Image composition is unavailable for review pins.');
      ctx.drawImage(this.canvas,0,0);const size=this._size(),sx=image.width/size.width,sy=image.height/size.height;ctx.scale(sx,sy);
      paintArtifactOverlay(ctx,this.artifactOverlayLayout||{texts:[],fills:[]});ctx.font='12px sans-serif';ctx.textBaseline='middle';
      for(const pin of pins){
        ctx.fillStyle='#ffdb68';ctx.strokeStyle='#1d2b38';ctx.lineWidth=2;ctx.beginPath();ctx.arc(pin.x,pin.y,11,0,Math.PI*2);ctx.fill();ctx.stroke();ctx.fillStyle='#162431';ctx.textAlign='center';ctx.fillText(String(pin.number),pin.x,pin.y);
        const label=pin.text.replace(/\s+/g,' ').slice(0,80),box=annotationLabelBounds(pin,ctx.measureText(label).width,size.width,size.height);
        ctx.fillStyle='#162431';ctx.fillRect(box.x,box.y,box.width,box.height);ctx.fillStyle='#fff';ctx.textAlign='left';ctx.fillText(label,box.x+6,box.y+10,Math.max(1,box.width-12));
      }
      if(measure){
        ctx.strokeStyle='#007bff';ctx.fillStyle='#007bff';ctx.lineWidth=2;
        if(measure.a&&measure.b){ctx.beginPath();ctx.moveTo(...measure.a);ctx.lineTo(...measure.b);ctx.stroke();}
        for(const slot of ['a','b'])if(measure[slot]){const [x,y]=measure[slot];ctx.beginPath();ctx.arc(x,y,5,0,Math.PI*2);ctx.fill();ctx.fillStyle='#162431';ctx.textAlign='left';ctx.fillText(slot.toUpperCase(),x+9,y-9);ctx.fillStyle='#007bff';}
        if(measure.a&&measure.b){const x=(measure.a[0]+measure.b[0])/2,y=(measure.a[1]+measure.b[1])/2-15,text=measure.distance_mm.toLocaleString(undefined,{maximumSignificantDigits:8})+' mm',width=ctx.measureText(text).width+16;ctx.fillStyle='#162431';ctx.fillRect(x-width/2,y-11,width,22);ctx.fillStyle='#fff';ctx.textAlign='center';ctx.fillText(text,x,y);}
      }
      return image.toDataURL('image/png');
    });}
    destroy(){if(this.destroyed)return;this.destroyed=true;this.ready=false;this._clearPointMeasurement();if(this.pending!==null)cancelAnimationFrame(this.pending);this.pending=null;if(this.hoverPending!==null)cancelAnimationFrame(this.hoverPending);this.hoverPending=null;this.hover=null;this.hoverPoint=null;this.observer?.disconnect();this.overlayCanvas?.remove();this.overlayCanvas=null;for(const remove of this.listeners)remove();this.listeners=[];this.drag=null;this._deleteBuffers();if(this.gl&&this.resources){for(const texture of this.resources.textures.values())if(texture.texture)this.gl.deleteTexture(texture.texture);this.gl.deleteProgram(this.resources.program);if(this.resources.backdrop)this.gl.deleteProgram(this.resources.backdrop.program);}this.resources=null;this.model=null;this.fullModel=null;this.sectionResult=null;this.annotations=[];this.callbacks.onAnnotations([]);this.selection=null;this.gl=null;}
  }
  CadRenderer.math=Object.freeze({validate,validateArtifact,prepareArtifact,camera,basis,project,screenRay,prepare,visibleModel,trace,pick,pickPoint,pickMeasurementPoint,pointDistance,pointMeasurementLayout,nearestSegment,gpuData,presentation,presentedModel,defaultPresentation,clipSegment,depthExtent,sectionGeometry,validateSection,sectionModel,appearance,defaultAppearance,appearanceData,annotations,annotationLayout,annotationLabelBounds,STANDARD_VIEWS,viewFromDirection,lerpCamera,easeOut,zoomAbout,fitCamera,entityBounds,defaultTheme,darkTheme,stageSettings,material,renderSettings,RENDER_MODES,surfaceStyle,materialData,studioLight,studioGeometry,artifactOverlay,paintArtifactOverlay});
  globalThis.CadRenderer=CadRenderer;
})();
