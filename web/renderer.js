/* Dependency-free CAD viewport. All topology IDs belong to one evaluation. */
(() => {
  'use strict';
  const MAX_VERTICES=200000, MAX_TRIANGLES=200000, MAX_ENTITIES=10000, MAX_EDGE_POINTS=200000;
  const MAX_PIXELS=4000000, PICK_BUDGET=2000000, DEPTH_EPS=1e-8;
  const DEFAULT_CAMERA={yaw:-.65,pitch:.6,zoom:1,pan:[0,0]};
  const cloneCamera=c=>({yaw:c.yaw,pitch:c.pitch,zoom:c.zoom,pan:[...c.pan]});
  const finitePoint=p=>Array.isArray(p)&&p.length===3&&p.every(n=>Number.isFinite(n)&&Math.abs(n)<=1e12);
  const fail=message=>{throw new Error(message);};
  function validate(d) {
    if(!d||d.schema_version!==1||!d.mesh||!d.topology||!d.summary)fail('Unsupported CAD evaluation.');
    for(const key of ['document_id','evaluation_id','feature_id'])
      if(typeof d[key]!=='string'||!d[key].length||d[key].length>256)fail('Missing evaluation identity.');
    if(!Number.isSafeInteger(d.revision)||d.revision<0)fail('Invalid revision identity.');
    const m=d.mesh,t=d.topology,b=d.summary.bounds_mm;
    if(!b||!finitePoint(b.min)||!finitePoint(b.max)||b.min.some((v,i)=>v>b.max[i])||Math.max(...b.max.map((v,i)=>v-b.min[i]))<=0)fail('Invalid model bounds.');
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
      if(!Array.isArray(parts)||!parts.length||parts.length>64)fail('Invalid assembly parts.');
      for(const part of parts) {
        if(!part||typeof part.id!=='string'||!/^[A-Za-z][A-Za-z0-9_-]{0,63}$/.test(part.id)||partIds.has(part.id)||!part.bounds_mm||!inside(part.bounds_mm.min)||!inside(part.bounds_mm.max)||part.bounds_mm.min.some((v,i)=>v>part.bounds_mm.max[i]))fail('Invalid assembly part bounds or identity.');
        partIds.add(part.id);
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
  function camera(value) {
    if(!value||!['yaw','pitch','zoom'].every(k=>Number.isFinite(value[k]))||!Array.isArray(value.pan)||value.pan.length!==2||!value.pan.every(Number.isFinite))fail('Invalid camera state.');
    return {yaw:Math.atan2(Math.sin(value.yaw),Math.cos(value.yaw)),pitch:Math.max(-Math.PI/2,Math.min(Math.PI/2,value.pitch)),zoom:Math.max(.05,Math.min(50,value.zoom)),pan:value.pan.map(n=>Math.max(-20,Math.min(20,n)))};
  }
  function basis(c) {
    const cy=Math.cos(c.yaw),sy=Math.sin(c.yaw),cp=Math.cos(c.pitch),sp=Math.sin(c.pitch);
    return [[cy,-sy,0],[sy*sp,cy*sp,cp],[sy*cp,cy*cp,-sp]];
  }
  const dot=(a,b)=>a[0]*b[0]+a[1]*b[1]+a[2]*b[2];
  function project(p,c,width,height) {
    const b=basis(c),size=Math.min(width,height),scale=size*.68*c.zoom;
    return [width/2+dot(b[0],p)*scale+c.pan[0]*size,height/2-dot(b[1],p)*scale+c.pan[1]*size,dot(b[2],p)];
  }
  function screenRay(x,y,c,width,height) {
    const b=basis(c),size=Math.min(width,height),scale=size*.68*c.zoom;
    const u=(x-width/2-c.pan[0]*size)/scale,v=(height/2+c.pan[1]*size-y)/scale;
    return {origin:b[0].map((n,i)=>n*u+b[1][i]*v-2*b[2][i]),direction:b[2]};
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
    return {identity,positions,indices,faces,faceNumbers,triangleFaces:[...d.mesh.triangle_faces],edges,geometries,tree:buildTree(positions,indices),partBounds,
      bounds:{min:normalized(bounds.min),max:normalized(bounds.max)},hiddenPartIds:[]};
  }
  // Derived draw/pick data retains full evaluation identity and normalization.
  // Build only on a visibility change, never on a camera/context poll.
  function visibleModel(source,ids) {
    if(!Array.isArray(ids)||ids.length>64||new Set(ids).size!==ids.length||ids.some(id=>!source.partBounds.has(id)))fail('Visibility must name unique current assembly parts.');
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
    const t=dot(f,q)/det;return t>=0&&t<=4 ? t : Infinity;
  }
  function trace(model,ray,budget={remaining:PICK_BUDGET}) {
    if(!model.indices.length)return {depth:Infinity,ids:new Set()};
    let depth=Infinity;const hits=new Map(),stack=[0],{nodes,order}=model.tree;
    while(stack.length) {
      if(--budget.remaining<0)fail('Selection exceeds its work limit. Inspect a smaller feature.');
      const node=nodes[stack.pop()];if(boxEntry(node,ray,Math.min(4,depth+DEPTH_EPS))===Infinity)continue;
      if(node.left>=0) {
        const a=boxEntry(nodes[node.left],ray,Math.min(4,depth+DEPTH_EPS)),b=boxEntry(nodes[node.right],ray,Math.min(4,depth+DEPTH_EPS));
        if(a<b){if(b!==Infinity)stack.push(node.right);if(a!==Infinity)stack.push(node.left);}else{if(a!==Infinity)stack.push(node.left);if(b!==Infinity)stack.push(node.right);}
      } else for(let i=node.start;i<node.start+node.count;i++) {
        if(--budget.remaining<0)fail('Selection exceeds its work limit. Inspect a smaller feature.');
        const index=order[i],t=triangleHit(model,index,ray);if(t===Infinity)continue;
        if(t<depth){depth=t;for(const [id,distance] of hits)if(distance>depth+DEPTH_EPS)hits.delete(id);}
        if(t<=depth+DEPTH_EPS){const id=model.triangleFaces[index];hits.set(id,Math.min(t,hits.get(id)??Infinity));}
      }
    }
    return {depth,ids:new Set(hits.keys())};
  }
  function nearestSegment(p,a,b) {
    const dx=b[0]-a[0],dy=b[1]-a[1],length=dx*dx+dy*dy;
    const t=length>1e-16?Math.max(0,Math.min(1,((p[0]-a[0])*dx+(p[1]-a[1])*dy)/length)):(a[2]<=b[2]?0:1);
    const q=a.map((v,i)=>v+t*(b[i]-v));return {point:q,distance:Math.hypot(q[0]-p[0],q[1]-p[1])};
  }
  function pick(model,c,width,height,x,y,mode='face') {
    if(!model||!Number.isFinite(width)||!Number.isFinite(height)||width<=0||height<=0||!Number.isFinite(x)||!Number.isFinite(y)||x<0||y<0||x>width||y>height)return {id:null,ambiguous:false};
    const budget={remaining:PICK_BUDGET};
    if(mode==='face') {
      const hit=trace(model,screenRay(x,y,c,width,height),budget);
      return {id:hit.ids.size===1?[...hit.ids][0]:null,ambiguous:hit.ids.size>1};
    }
    const candidates=[];
    for(const edge of model.edges) {
      let previous=null;
      for(let i=0;i<edge.points.length;i+=3) {
        const current=project(edge.points.subarray(i,i+3),c,width,height);
        if(previous) {const q=nearestSegment([x,y],previous,current);if(q.distance<=9&&q.point[0]>=0&&q.point[1]>=0&&q.point[0]<=width&&q.point[1]<=height)candidates.push({...q,id:edge.id});}
        previous=current;
      }
    }
    candidates.sort((a,b)=>a.distance-b.distance);
    let best=Infinity,depth=Infinity;const ids=new Set();
    for(const candidate of candidates) {
      if(candidate.distance>best+.25)break;
      const q=candidate.point,hit=trace(model,screenRay(q[0],q[1],c,width,height),budget),z=q[2]+2;
      if(z>hit.depth+DEPTH_EPS*4)continue;
      if(best===Infinity){best=candidate.distance;depth=z;ids.add(candidate.id);}
      else if(z<depth-DEPTH_EPS) {depth=z;ids.clear();ids.add(candidate.id);}
      else if(Math.abs(candidate.distance-best)<=.25&&Math.abs(z-depth)<=DEPTH_EPS)ids.add(candidate.id);
    }
    return {id:ids.size===1?[...ids][0]:null,ambiguous:ids.size>1};
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
      const index=model.indices[i],normal=vertex(normals,index),length=Math.hypot(...normal)||1;
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
      uniform mat3 uBasis; uniform vec2 uScale; uniform vec2 uPan;
      ${webgl2?'out':'varying'} vec3 vNormal;
      ${webgl2?'out':'varying'} float vEntity;
      void main(){vec3 p=uBasis*aPosition;gl_Position=vec4(p.xy*uScale+uPan,p.z*.5,1.);vNormal=uBasis*aNormal;vEntity=aEntity;}`;
    const fragmentSource=(webgl2?'#version 300 es\n':'')+`
      precision highp float;
      ${webgl2?'in':'varying'} vec3 vNormal;
      ${webgl2?'in':'varying'} float vEntity;
      uniform float uSelected;uniform bool uLines;uniform bool uEdgeMode;
      ${webgl2?'out vec4 outColor;':''}
      void main(){
        bool selected=abs(vEntity-uSelected)<.25;
        vec3 color;
        if(uLines){color=selected?vec3(.24,.95,.78):(uEdgeMode?vec3(.29,.44,.51):vec3(.16,.25,.30));}
        else {vec3 n=normalize(vNormal);if(!gl_FrontFacing)n=-n;
          float diffuse=max(0.,dot(n,normalize(vec3(-.35,.65,-.85))));
          float rim=pow(1.-abs(n.z),3.);
          float spec=pow(max(0.,dot(reflect(normalize(vec3(.35,-.65,.85)),n),vec3(0.,0.,-1.))),28.);
          vec3 base=selected?vec3(.16,.74,.63):vec3(.66,.75,.80);
          color=base*(.50+.45*diffuse)+vec3(.08)*rim+vec3(.12)*spec;}
        ${webgl2?'outColor':'gl_FragColor'}=vec4(color,1.);
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
  class CadRenderer {
    constructor(canvas,{onPick=()=>{},onCamera=()=>{},onError=()=>{},onReady=()=>{}}={}) {
      if(!canvas||typeof canvas.getContext!=='function')fail('A canvas is required.');
      this.canvas=canvas;this.callbacks={onPick,onCamera,onError,onReady};this.camera=cloneCamera(DEFAULT_CAMERA);this.mode='face';this.model=null;this.fullModel=null;this.selection=null;this.gl=null;this.resources=null;this.destroyed=false;this.lost=false;this.ready=false;this.pending=null;this.listeners=[];this.drag=null;
      this._listen(canvas,'webglcontextlost',event=>{event.preventDefault();this.lost=true;this.resources=null;this._error(new Error('WebGL context was lost. Waiting for graphics recovery.'));});
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
      for(const name of ['uBasis','uScale','uPan','uSelected','uLines','uEdgeMode'])uniforms[name]=gl.getUniformLocation(shader,name);
      for(const name of ['aPosition','aNormal','aEntity'])attributes[name]=gl.getAttribLocation(shader,name);
      this.resources={program:shader,uniforms,attributes,faces:null,edges:null,ranges:new Map(),faceCount:0,edgeCount:0};
      this.maxDimension=Math.min(4096,gl.getParameter(gl.MAX_RENDERBUFFER_SIZE)||4096,gl.getParameter(gl.MAX_VIEWPORT_DIMS)?.[0]||4096,gl.getParameter(gl.MAX_VIEWPORT_DIMS)?.[1]||4096);
    }
    _deleteBuffers(){if(!this.resources||!this.gl)return;for(const key of ['faces','edges'])if(this.resources[key]){this.gl.deleteBuffer(this.resources[key]);this.resources[key]=null;}}
    _upload() {
      if(!this.gl||!this.resources||this.lost)return;
      const data=gpuData(this.model),gl=this.gl,r=this.resources;this._deleteBuffers();
      try {
        for(const key of ['faces','edges']) {const buffer=gl.createBuffer();if(!buffer)fail('Unable to allocate geometry buffers.');r[key]=buffer;gl.bindBuffer(gl.ARRAY_BUFFER,buffer);gl.bufferData(gl.ARRAY_BUFFER,data[key],gl.STATIC_DRAW);}
        if(gl.getError()!==gl.NO_ERROR)fail('The graphics device could not load this mesh. Inspect a smaller feature.');
        r.faceCount=data.faces.length/7;r.edgeCount=data.edges.length/4;r.ranges=data.ranges;
      } catch(error){this._deleteBuffers();throw error;}
    }
    _schedule(){if(this.destroyed||this.pending!==null)return;this.pending=requestAnimationFrame(()=>{this.pending=null;try{this._draw();}catch(error){this._error(error);}});}
    _size(){const rect=this.canvas.getBoundingClientRect();return {width:Math.max(1,rect.width),height:Math.max(1,rect.height)};}
    _draw() {
      if(this.destroyed||this.lost||!this.gl||!this.resources)return;
      const {width,height}=this._size(),ratio=Math.min(globalThis.devicePixelRatio||1,2,this.maxDimension/width,this.maxDimension/height,Math.sqrt(MAX_PIXELS/(width*height)));
      const w=Math.max(1,Math.floor(width*ratio)),h=Math.max(1,Math.floor(height*ratio)),gl=this.gl,r=this.resources;
      if(this.canvas.width!==w||this.canvas.height!==h){this.canvas.width=w;this.canvas.height=h;}
      gl.viewport(0,0,w,h);gl.clearColor(.075,.105,.135,1);gl.clear(gl.COLOR_BUFFER_BIT|gl.DEPTH_BUFFER_BIT);
      if(!this.model||!r.faces)return;
      gl.enable(gl.DEPTH_TEST);gl.depthFunc(gl.LEQUAL);gl.disable(gl.CULL_FACE);gl.disable(gl.BLEND);gl.useProgram(r.program);
      const b=basis(this.camera),size=Math.min(width,height),scale=size*.68*this.camera.zoom,u=r.uniforms,a=r.attributes;
      gl.uniformMatrix3fv(u.uBasis,false,new Float32Array([b[0][0],b[1][0],b[2][0],b[0][1],b[1][1],b[2][1],b[0][2],b[1][2],b[2][2]]));
      gl.uniform2f(u.uScale,2*scale/width,2*scale/height);gl.uniform2f(u.uPan,2*this.camera.pan[0]*size/width,-2*this.camera.pan[1]*size/height);
      gl.uniform1i(u.uEdgeMode,this.mode==='edge');
      gl.bindBuffer(gl.ARRAY_BUFFER,r.faces);
      gl.enableVertexAttribArray(a.aPosition);gl.vertexAttribPointer(a.aPosition,3,gl.FLOAT,false,28,0);
      gl.enableVertexAttribArray(a.aNormal);gl.vertexAttribPointer(a.aNormal,3,gl.FLOAT,false,28,12);
      gl.enableVertexAttribArray(a.aEntity);gl.vertexAttribPointer(a.aEntity,1,gl.FLOAT,false,28,24);
      gl.uniform1i(u.uLines,false);gl.uniform1f(u.uSelected,this.selection?.kind==='face'?this.model.faceNumbers.get(this.selection.entity_id):-1);
      gl.enable(gl.POLYGON_OFFSET_FILL);gl.polygonOffset(1,1);gl.drawArrays(gl.TRIANGLES,0,r.faceCount);gl.disable(gl.POLYGON_OFFSET_FILL);
      gl.bindBuffer(gl.ARRAY_BUFFER,r.edges);gl.vertexAttribPointer(a.aPosition,3,gl.FLOAT,false,16,0);gl.vertexAttribPointer(a.aEntity,1,gl.FLOAT,false,16,12);gl.disableVertexAttribArray(a.aNormal);gl.vertexAttrib3f(a.aNormal,0,0,1);
      const selected=this.selection?.kind==='edge'?r.ranges.get(this.selection.entity_id):null;
      gl.uniform1i(u.uLines,true);gl.uniform1f(u.uSelected,selected?.number??-1);gl.lineWidth(1);gl.drawArrays(gl.LINES,0,r.edgeCount);
      if(selected){const range=gl.getParameter(gl.ALIASED_LINE_WIDTH_RANGE);gl.lineWidth(Math.min(3,range?.[1]||1));gl.drawArrays(gl.LINES,selected.start,selected.count);gl.lineWidth(1);}
      if(!this.ready){this.ready=true;this.callbacks.onReady({...this.model.identity});}
    }
    _cameraChanged(){this._schedule();this.callbacks.onCamera(this.getCamera());}
    _controls() {
      const canvas=this.canvas;
      this._listen(canvas,'contextmenu',event=>event.preventDefault());
      this._listen(canvas,'pointerdown',event=>{
        if(this.drag||event.button>2)return;event.preventDefault();canvas.focus({preventScroll:true});
        this.drag={id:event.pointerId,x:event.clientX,y:event.clientY,lastX:event.clientX,lastY:event.clientY,moved:false,pan:event.button!==0||event.shiftKey};
        canvas.setPointerCapture(event.pointerId);
      });
      this._listen(canvas,'pointermove',event=>{
        const drag=this.drag;if(!drag||drag.id!==event.pointerId)return;
        if(Math.hypot(event.clientX-drag.x,event.clientY-drag.y)>3)drag.moved=true;
        if(drag.moved){const dx=event.clientX-drag.lastX,dy=event.clientY-drag.lastY,{width,height}=this._size();
          if(drag.pan){this.camera.pan[0]+=dx/Math.min(width,height);this.camera.pan[1]+=dy/Math.min(width,height);}else{this.camera.yaw+=dx*.008;this.camera.pitch+=dy*.008;}
          this.camera=camera(this.camera);this._cameraChanged();}
        drag.lastX=event.clientX;drag.lastY=event.clientY;
      });
      this._listen(canvas,'pointerup',event=>{
        const drag=this.drag;if(!drag||drag.id!==event.pointerId)return;this.drag=null;
        if(canvas.hasPointerCapture(event.pointerId))canvas.releasePointerCapture(event.pointerId);
        if(!drag.moved&&!drag.pan){const rect=canvas.getBoundingClientRect();this._pick(event.clientX-rect.left,event.clientY-rect.top);}
      });
      this._listen(canvas,'pointercancel',()=>{this.drag=null;});
      this._listen(canvas,'lostpointercapture',()=>{this.drag=null;});
      this._listen(canvas,'wheel',event=>{event.preventDefault();const delta=event.deltaY*(event.deltaMode===1?16:event.deltaMode===2?this._size().height:1);this.camera.zoom=Math.max(.05,Math.min(50,this.camera.zoom*Math.exp(-delta*.001)));this._cameraChanged();},{passive:false});
      this._listen(canvas,'keydown',event=>{
        let handled=true;
        if(event.key.startsWith('Arrow')) {
          const dx=event.key==='ArrowLeft'?-1:event.key==='ArrowRight'?1:0,dy=event.key==='ArrowUp'?-1:event.key==='ArrowDown'?1:0;
          if(event.shiftKey){this.camera.pan[0]+=.05*dx;this.camera.pan[1]+=.05*dy;}else{this.camera.yaw+=dx*.1;this.camera.pitch-=dy*.1;}
        } else if(event.key==='+'||event.key==='=')this.camera.zoom*=1.15;
        else if(event.key==='-')this.camera.zoom/=1.15;
        else if(event.key==='Home'||event.key==='0'){event.preventDefault();this.reset();return;}
        else if(event.key==='Escape'){this.selection=null;this.callbacks.onPick(null,{ambiguous:false,message:''});}
        else handled=false;
        if(handled){event.preventDefault();this.camera=camera(this.camera);this._cameraChanged();}
      });
    }
    _pick(x,y) {
      if(!this.model||this.lost||!this.gl)return;
      try {
        const {width,height}=this._size(),result=pick(this.model,this.camera,width,height,x,y,this.mode);
        this.selection=result.id?{...this.model.identity,kind:this.mode,entity_id:result.id}:null;this._schedule();
        const selection=this.selection?{reference:{...this.selection},geometry:this.model.geometries[this.mode].get(result.id)}:null;
        this.callbacks.onPick(selection,{ambiguous:result.ambiguous,message:result.ambiguous?'Geometry overlaps here. Rotate the view or inspect a specific feature.':''});
      }catch(error){this.selection=null;this._schedule();this.callbacks.onPick(null,{ambiguous:false,message:error.message});this._error(error);}
    }
    load(evaluation,{preserveCamera=true,hiddenPartIds=[]}={}) {return this._checked(()=>{const full=prepare(evaluation),model=visibleModel(full,hiddenPartIds);this.fullModel=full;this.model=model;this.selection=null;this.ready=false;if(!preserveCamera)this.camera=cloneCamera(DEFAULT_CAMERA);this._upload();this._schedule();});}
    setHiddenParts(ids){return this._checked(()=>{
      if(!Array.isArray(ids))fail('Visibility must be an array of part IDs.');
      if(!this.fullModel){if(ids.length)fail('Load an assembly before hiding parts.');return;}
      const canonical=[...ids].sort();
      if(JSON.stringify(canonical)===JSON.stringify([...this.model.hiddenPartIds].sort()))return;
      const model=visibleModel(this.fullModel,ids);this.model=model;
      if(this.selection&&!model.geometries[this.selection.kind].has(this.selection.entity_id))this.selection=null;
      this._upload();this._schedule();
    });}
    setMode(mode){return this._checked(()=>{if(mode!=='face'&&mode!=='edge')fail('Selection mode must be face or edge.');this.mode=mode;this.selection=null;this._schedule();});}
    setCamera(value){return this._checked(()=>{this.camera=camera(value);this._cameraChanged();});}
    getCamera(){return cloneCamera(this.camera);}
    reset(){return this._checked(()=>{
      this.camera=cloneCamera(DEFAULT_CAMERA);const bounds=this.model?.bounds;
      if(bounds){
        const {width,height}=this._size(),b=basis(this.camera),corners=[];
        for(const x of [bounds.min[0],bounds.max[0]])for(const y of [bounds.min[1],bounds.max[1]])for(const z of [bounds.min[2],bounds.max[2]])corners.push([dot(b[0],[x,y,z]),dot(b[1],[x,y,z])]);
        const low=[0,1].map(a=>Math.min(...corners.map(p=>p[a]))),high=[0,1].map(a=>Math.max(...corners.map(p=>p[a]))),size=Math.min(width,height);
        const zoom=Math.min(width*.82/(Math.max(1e-12,high[0]-low[0])*.68*size),height*.72/(Math.max(1e-12,high[1]-low[1])*.68*size));
        this.camera.zoom=Math.max(.05,Math.min(50,zoom));
        this.camera.pan=[-(low[0]+high[0])/2*.68*this.camera.zoom,(low[1]+high[1])/2*.68*this.camera.zoom];
        this.camera=camera(this.camera);
      }
      this._cameraChanged();
    });}
    setView(view){return this._checked(()=>{const views={iso:[-.65,.6],top:[0,Math.PI/2],front:[0,0],right:[-Math.PI/2,0]};if(!Object.hasOwn(views,view))fail('Unknown camera view.');[this.camera.yaw,this.camera.pitch]=views[view];this._cameraChanged();});}
    setSelection(reference){return this._checked(()=>{
      if(reference!==null) {
        if(!this.model||!reference||!['face','edge'].includes(reference.kind)||!this.model.geometries[reference.kind].has(reference.entity_id)||Object.entries(this.model.identity).some(([key,value])=>reference[key]!==value))fail('Selection belongs to another evaluation or unknown entity.');
        if(this.selection?.kind===reference.kind&&this.selection.entity_id===reference.entity_id)return;
        this.selection={...this.model.identity,kind:reference.kind,entity_id:reference.entity_id};
      }else {if(this.selection===null)return;this.selection=null;}
      this._schedule();
    });}
    capture(){return this._checked(()=>{if(!this.gl||this.lost||!this.resources)fail('WebGL is unavailable for capture.');this._draw();return this.canvas.toDataURL('image/png');});}
    destroy(){if(this.destroyed)return;this.destroyed=true;this.ready=false;if(this.pending!==null)cancelAnimationFrame(this.pending);this.pending=null;this.observer?.disconnect();for(const remove of this.listeners)remove();this.listeners=[];this.drag=null;this._deleteBuffers();if(this.gl&&this.resources)this.gl.deleteProgram(this.resources.program);this.resources=null;this.model=null;this.fullModel=null;this.selection=null;this.gl=null;}
  }
  CadRenderer.math=Object.freeze({validate,camera,basis,project,screenRay,prepare,visibleModel,trace,pick,nearestSegment,gpuData});
  globalThis.CadRenderer=CadRenderer;
})();
