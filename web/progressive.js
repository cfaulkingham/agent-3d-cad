/* Progressive B-rep component display. Native workers tessellate only requested
   source geometry. Occurrences share local meshes; publication stays source-bound. */
(() => {
  'use strict';
  const MAX_BYTES=32*1024*1024,MAX_TRIANGLES=200000,MAX_VERTICES=200000,MAX_POINTS=200000;
  const point=p=>Array.isArray(p)&&p.length===3&&p.every(x=>Number.isFinite(x)&&Math.abs(x)<=1e12);
  const id=s=>typeof s==='string'&&/^[A-Za-z][A-Za-z0-9_-]{0,63}$/.test(s);
  const fail=s=>{throw Error(s);};
  const transform=(m,p)=>[0,1,2].map(i=>m[i*4]*p[0]+m[i*4+1]*p[1]+m[i*4+2]*p[2]+m[i*4+3]);
  function validateManifest(p) {
    const c=p?.components,b=p?.summary?.bounds_mm,t=p?.topology;
    if(!c||c.schema_version!==1||c.coordinate_space!=='source_local_mm'||!id(p.document_id)||!id(p.evaluation_id)||!id(p.feature_id)||!Number.isSafeInteger(p.revision)||p.revision<1||p.draft!==false||!b||!point(b.min)||!point(b.max)||b.min.some((v,i)=>v>b.max[i])||Math.max(...b.max.map((v,i)=>v-b.min[i]))<=0)fail('Invalid progressive source manifest.');
    if(!t||t.feature_id!==p.feature_id||!Array.isArray(t.faces)||!Array.isArray(t.edges)||t.faces.length+t.edges.length>100000)fail('Progressive topology exceeds its bounded manifest.');
    if(!Array.isArray(c.geometries)||!c.geometries.length||c.geometries.length>256||!Array.isArray(c.occurrences)||!c.occurrences.length||c.occurrences.length>1024)fail('Invalid component inventory.');
    const geometries=new Map(),owners=new Map((p.summary.assembly?.parts||[]).map(x=>[x.id,x]));
    for(const g of c.geometries){if(!g||!id(g.id)||geometries.has(g.id)||!g.bounds_mm||!point(g.bounds_mm.min)||!point(g.bounds_mm.max)||g.bounds_mm.min.some((v,i)=>v>g.bounds_mm.max[i])||!/^[a-f0-9]{64}$/.test(g.topology_sha256)||!Array.isArray(g.levels)||g.levels.length!==3||g.levels.some((l,i)=>l.level!==i||!Number.isFinite(l.linear_deflection_mm)||l.linear_deflection_mm<=0||l.linear_deflection_mm>1e6||!Number.isFinite(l.angular_deflection_rad)||l.angular_deflection_rad<=0||l.angular_deflection_rad>1||i>0&&l.linear_deflection_mm>g.levels[i-1].linear_deflection_mm))fail('Invalid component detail inventory.');geometries.set(g.id,g);}
    const faces=new Map(t.faces.map(v=>[v.id,v])),edges=new Map(t.edges.map(v=>[v.id,v])),seenFaces=new Set(),seenEdges=new Set(),parts=new Set(),used=new Set();
    if(faces.size!==t.faces.length||edges.size!==t.edges.length)fail('Duplicate source topology.');
    for(const o of c.occurrences){const m=o.transform;if(!geometries.has(o.geometry_id)||parts.has(o.id)||o.id!==null&&!owners.has(o.id)||o.id===null&&(owners.size||c.occurrences.length!==1)||!Array.isArray(m)||m.length!==16||!m.every(Number.isFinite)||[12,13,14].some(i=>Math.abs(m[i])>1e-12)||Math.abs(m[15]-1)>1e-12)fail('Invalid occurrence transform or ownership.');
      for(let a=0;a<3;a++)for(let b=0;b<3;b++)if(Math.abs([0,1,2].reduce((n,i)=>n+m[a*4+i]*m[b*4+i],0)-(a===b?1:0))>1e-7)fail('Occurrence transform must be rigid.');
      if(m[0]*(m[5]*m[10]-m[6]*m[9])-m[1]*(m[4]*m[10]-m[6]*m[8])+m[2]*(m[4]*m[9]-m[5]*m[8])<.999999)fail('Occurrence must preserve face orientation.');
      for(const [key,entities,seen,prefix] of [['faces',faces,seenFaces,'face'],['edges',edges,seenEdges,'edge']]){if(!Array.isArray(o[key])||o[key].length>10000)fail('Invalid component topology mapping.');for(const entity of o[key]){if(!new RegExp('^'+prefix+'-[1-9][0-9]*$').test(entity)||!entities.has(entity)||seen.has(entity)||(entities.get(entity).part_id??null)!==o.id)fail('Occurrence correspondence does not match its topology.');seen.add(entity);}}
      parts.add(o.id);used.add(o.geometry_id);
    }
    if(seenFaces.size!==faces.size||seenEdges.size!==edges.size||used.size!==geometries.size||owners.size&&parts.size!==owners.size)fail('Component manifest must cover all source topology.');
    return p;
  }
  function validateComponent(response,manifest,g,level) {
    for(const key of ['document_id','revision','evaluation_id','feature_id'])if(response?.[key]!==manifest[key])fail('Component source changed during transfer.');
    const c=response.component_mesh,m=c?.mesh,l=g.levels[level];
    if(!c||c.geometry_id!==g.id||c.level!==level||c.topology_sha256!==g.topology_sha256||!m||m.schema_version!==1||m.feature_id!==g.id||m.selection_lifetime!=='evaluation'||m.units!=='mm'||m.linear_deflection_mm!==l.linear_deflection_mm||m.angular_deflection_rad!==l.angular_deflection_rad)fail('Component tessellation does not match its qualified source.');
    const occurrence=manifest.components.occurrences.find(o=>o.geometry_id===g.id),faceCount=occurrence.faces.length,edgeCount=occurrence.edges.length;
    const inside=p=>point(p)&&p.every((v,i)=>v>=g.bounds_mm.min[i]-1e-5&&v<=g.bounds_mm.max[i]+1e-5);
    if(!Array.isArray(m.positions)||m.positions.length>MAX_VERTICES||!m.positions.every(inside)||!Array.isArray(m.triangles)||m.triangles.length>MAX_TRIANGLES||!m.triangles.every(t=>Array.isArray(t)&&t.length===3&&t.every(i=>Number.isSafeInteger(i)&&i>=0&&i<m.positions.length))||!Array.isArray(m.triangle_faces)||m.triangle_faces.length!==m.triangles.length||!m.triangle_faces.every(f=>/^face-[1-9][0-9]*$/.test(f)&&Number(f.slice(5))<=faceCount)||new Set(m.triangle_faces).size!==faceCount)fail('Invalid bounded component triangles.');
    let points=0;const seen=new Set();if(!Array.isArray(m.edges)||m.edges.length!==edgeCount)fail('Incomplete component edge mapping.');for(const e of m.edges){if(!/^edge-[1-9][0-9]*$/.test(e.id)||Number(e.id.slice(5))>edgeCount||seen.has(e.id)||!Array.isArray(e.points)||!e.points.every(inside))fail('Invalid component edge mapping.');seen.add(e.id);points+=e.points.length;}if(points>MAX_POINTS)fail('Component edges exceed their budget.');
    // Conservatively charge parsed JSON, JS arrays, CPU/BVH staging and GPU buffers.
    const bytes=JSON.stringify(response).length*4+m.positions.length*24+m.triangles.length*128+points*48;
    return {mesh:m,bytes,points};
  }
  class CadProgressiveSession {
    constructor(manifest,{request,onPayload=()=>{},onStatus=()=>{},onError=()=>{},memoryBytes=MAX_BYTES,camera=null,width=1000,height=700,hiddenPartIds=[],presentation=CadRenderer.math.defaultPresentation(),render=CadRenderer.math.renderSettings()}={}) {
      this.manifest=validateManifest(manifest);this.request=request;this.onPayload=onPayload;this.onStatus=onStatus;this.onError=onError;this.memoryBytes=Math.min(MAX_BYTES,Math.max(1024,memoryBytes));this.camera=camera||{yaw:-.65,pitch:.6,zoom:1,pan:[0,0]};this.width=width;this.height=height;this.hidden=new Set(hiddenPartIds);this.presentation=presentation;this.render=CadRenderer.math.renderSettings(render);this.pool=new Map();this.resident=new Map();this.failed=new Map();this.pressureFailures=new Set();this.pressureCaps=new Map();this.pressureTried=new Set();this.closed=false;this.running=null;this.timer=null;this.waiters=[];this.generation=0;this.serial=0;this.active=null;this.error=null;this.omitted=0;this.onScreen=new Set();this.stamp=0;
      this.geometries=new Map(manifest.components.geometries.map(g=>[g.id,g]));const b=manifest.summary.bounds_mm;this.span=Math.max(...b.max.map((v,i)=>v-b.min[i]));this.center=b.min.map((v,i)=>(v+b.max[i])/2);this.partBounds=new Map((manifest.summary.assembly?.parts||[]).map(p=>[p.id,p.bounds_mm]));
    }
    priorities(){
      const result=new Map();this.onScreen.clear();
      const normalize=b=>({min:b.min.map((v,i)=>(v-this.center[i])/this.span),max:b.max.map((v,i)=>(v-this.center[i])/this.span)});
      const partBounds=new Map([...this.partBounds].map(([id,b])=>[id,normalize(b)])),offsets=CadRenderer.math.presentationOffsets({span:this.span,partBounds},this.presentation);
      // Use the renderer's exact full-occurrence bounds, including hidden parts
      // and explosion, so partial mesh residency cannot change perspective framing.
      let bounds=normalize(this.manifest.summary.bounds_mm);
      if(this.hidden.size||this.presentation.explode.distance_mm>0){bounds=null;for(const [id,b] of partBounds){if(this.hidden.has(id))continue;const shift=offsets.get(id)||[0,0,0];if(!bounds)bounds={min:[Infinity,Infinity,Infinity],max:[-Infinity,-Infinity,-Infinity]};for(let i=0;i<3;i++){bounds.min[i]=Math.min(bounds.min[i],b.min[i]+shift[i]);bounds.max[i]=Math.max(bounds.max[i],b.max[i]+shift[i]);}}}
      const extent=CadRenderer.math.depthExtent({bounds}),camera={...this.camera,projection:this.render.projection,distance:Math.max(extent+.25,Math.max(3,extent+1)*(this.render.lens_mm/50))};
      for(const o of this.manifest.components.occurrences){if(this.hidden.has(o.id))continue;const b=this.partBounds.get(o.id)||this.manifest.summary.bounds_mm;const corners=[];for(let x=0;x<2;x++)for(let y=0;y<2;y++)for(let z=0;z<2;z++)corners.push(CadRenderer.math.project([x?b.max[0]:b.min[0],y?b.max[1]:b.min[1],z?b.max[2]:b.min[2]].map((v,i)=>(v-this.center[i])/this.span+(offsets.get(o.id)?.[i]||0)),camera,this.width,this.height));
        const min=[0,1].map(i=>Math.min(...corners.map(p=>p[i]))),max=[0,1].map(i=>Math.max(...corners.map(p=>p[i]))),visible=max[0]>=0&&max[1]>=0&&min[0]<=this.width&&min[1]<=this.height;
        if(visible)this.onScreen.add(o.id);
        const size=Math.hypot(max[0]-min[0],max[1]-min[1]),world=Math.hypot(...b.max.map((v,i)=>v-b.min[i])),scale=visible?size/Math.max(world,1e-9):0;
        const g=this.geometries.get(o.geometry_id),current=this.resident.get(g.id)??0;let level=current;
        while(level<2&&g.levels[level].linear_deflection_mm*scale>2)level++;
        while(level>0&&g.levels[level-1].linear_deflection_mm*scale<.8)level--;
        level=Math.min(level,this.pressureCaps.get(g.id)??2);
        const prior=result.get(g.id);if(!prior||scale>prior.scale)result.set(g.id,{id:g.id,scale,level,score:visible?size:0});
      }
      return [...result.values()].sort((a,b)=>b.score-a.score||a.id.localeCompare(b.id));
    }
    failure(){const wanted=this.priorities().filter(p=>p.score>0);const failures=wanted.flatMap(p=>{const key=p.id+':'+(this.resident.has(p.id)?p.level:0);return this.failed.has(key)?[p.id+': '+this.failed.get(key)]:[];});if(this.omittedVisible)failures.push(this.omittedVisible+' visible occurrences exceed the display geometry budget.');return failures.length?failures.join('; '):null;}
    status(){const wanted=this.priorities();return {loading:!!this.active||!!this.running,loaded:this.resident.size,total:this.geometries.size,generation:this.generation,cache_bytes:[...this.pool.values()].reduce((n,v)=>n+v.bytes,0),budget_bytes:this.memoryBytes,omitted_occurrences:this.omitted,omitted_visible_occurrences:this.omittedVisible||0,pressure_limited_geometries:this.pressureCaps.size,error:this.failure(),failed_requests:this.failed.size,missing_visible_geometries:wanted.filter(p=>p.score>0&&!this.resident.has(p.id)).length,levels:Object.fromEntries(this.resident),visible_geometries:wanted.filter(p=>p.score>0).length};}
    next(){const desired=this.priorities();for(const p of desired)if(p.score>0&&!this.resident.has(p.id)&&!this.failed.has(p.id+':0'))return {id:p.id,level:0};for(const p of desired)if(this.resident.has(p.id)&&this.resident.get(p.id)!==p.level&&!this.failed.has(p.id+':'+p.level))return {id:p.id,level:p.level};return null;}
    async fetch(id,level){
      const key=id+':'+level;if(this.pool.has(key)){const entry=this.pool.get(key);entry.used=++this.stamp;return entry;}
      const token='request_'+(++this.serial)+'_'+Math.random().toString(36).slice(2,14);const args={geometry_id:id,level,request_id:token};this.active=args;this.onStatus(this.status());let offset=0,total=null,chunks=[],busyRetries=0;
      while(!this.closed){let response;try{response=await this.request({...args,offset});}catch(error){
          // These are explicit native admission/lock refusals. The same job
          // request ID and offset are replayed; no mutation or unknown error is retried.
          if(!this.closed&&['workspace_busy','queue_full'].includes(error.code)&&busyRetries<20){await new Promise(resolve=>setTimeout(resolve,Math.min(100,10*(++busyRetries))));continue;}throw error;
        }if(this.closed)return null;
        if(response.evaluation_id!==this.manifest.evaluation_id||response.geometry_id!==id||response.level!==level)fail('Component transfer identity changed.');
        if(response.component_state==='loading'){await new Promise(resolve=>setTimeout(resolve,40));continue;}
        if(response.component_state==='error'){const e=Error(response.error?.message||'Component load failed.');e.code=response.error?.code;throw e;}
        if(response.component_state!=='ready'||typeof response.data!=='string'||response.offset!==offset||!Number.isSafeInteger(response.total_bytes)||response.total_bytes<1||response.total_bytes>64*1024*1024||total!==null&&response.total_bytes!==total||response.data.length<1||response.data.length>128*1024||/[^\x00-\x7f]/.test(response.data))fail('Invalid component transfer.');
        total=response.total_bytes;offset+=response.data.length;if(offset>total)fail('Component transfer exceeds its declared size.');chunks.push(response.data);
        if(response.next_offset===null){if(offset!==total)fail('Incomplete component transfer.');break;}if(response.next_offset!==offset||offset>=total)fail('Invalid component continuation.');
      }
      if(this.closed)return null;const entry=validateComponent(JSON.parse(chunks.join('')),this.manifest,this.geometries.get(id),level);entry.used=++this.stamp;
      if(entry.bytes>this.memoryBytes){const error=Error('Component exceeds the display memory budget.');error.code='display_memory_budget';throw error;}
      // Never evict currently displayed geometry before a replacement is validated.
      let bytes=[...this.pool.values()].reduce((n,v)=>n+v.bytes,0);
      for(const [k,v] of [...this.pool].sort((a,b)=>a[1].used-b[1].used)){if(bytes+entry.bytes<=this.memoryBytes)break;const [source,lod]=k.split(':');if(this.resident.get(source)===Number(lod))continue;this.pool.delete(k);bytes-=v.bytes;}
      if(bytes+entry.bytes>this.memoryBytes){const oldKey=id+':'+this.resident.get(id),old=this.pool.get(oldKey);if(old&&bytes-old.bytes+entry.bytes<=this.memoryBytes){this.pool.delete(oldKey);bytes-=old.bytes;}}
      if(bytes+entry.bytes>this.memoryBytes){const visible=new Set(this.priorities().filter(p=>p.score>0).map(p=>p.id));for(const [source,lod] of [...this.resident]){if(bytes+entry.bytes<=this.memoryBytes)break;if(source===id||visible.has(source))continue;const k=source+':'+lod,v=this.pool.get(k);if(v){this.pool.delete(k);bytes-=v.bytes;}this.resident.delete(source);}}
      if(bytes+entry.bytes>this.memoryBytes){const error=Error('Display memory budget reached; retained the previous geometry.');error.code='display_memory_budget';throw error;}this.pool.set(key,entry);return entry;
    }
    compose(){
      this.priorities();
      const p=this.manifest,m={schema_version:1,units:'mm',feature_id:p.feature_id,selection_lifetime:'evaluation',linear_deflection_mm:0,positions:[],triangles:[],triangle_faces:[],edges:[]};let points=0,omitted=0,omittedVisible=0,loaded=0;
      // On-screen occurrences win residency; the summary bounds always frame the whole model.
      const occurrences=[...p.components.occurrences].sort((a,b)=>Number(this.onScreen.has(b.id))-Number(this.onScreen.has(a.id)));
      for(const o of occurrences){if(this.hidden.has(o.id))continue;const level=this.resident.get(o.geometry_id),entry=this.pool.get(o.geometry_id+':'+level);if(!entry)continue;const source=entry.mesh;
        if((m.positions.length+source.positions.length)*80+(m.triangles.length+source.triangles.length)*256+(points+entry.points)*80>64*1024*1024||m.positions.length+source.positions.length>MAX_VERTICES||m.triangles.length+source.triangles.length>MAX_TRIANGLES||points+entry.points>MAX_POINTS){omitted++;if(this.onScreen.has(o.id))omittedVisible++;continue;}
        const offset=m.positions.length;for(const v of source.positions)m.positions.push(transform(o.transform,v));for(let i=0;i<source.triangles.length;i++){m.triangles.push(source.triangles[i].map(n=>n+offset));m.triangle_faces.push(o.faces[Number(source.triangle_faces[i].slice(5))-1]);}
        for(const e of source.edges)m.edges.push({id:o.edges[Number(e.id.slice(5))-1],...(o.id!==null?{part_id:o.id}:{}),points:e.points.map(v=>transform(o.transform,v))});points+=entry.points;m.linear_deflection_mm=Math.max(m.linear_deflection_mm,source.linear_deflection_mm);loaded++;
      }
      this.omitted=omitted;this.omittedVisible=omittedVisible;return {...p,mesh:m,progressive:{generation:++this.generation,loaded_occurrences:loaded,total_occurrences:p.components.occurrences.length,omitted_occurrences:omitted,omitted_visible_occurrences:omittedVisible}};
    }
    async loadFirst(){const task=this.next()||{id:this.manifest.components.geometries[0].id,level:0};try{await this.fetch(task.id,task.level);if(this.closed)return null;this.resident.set(task.id,task.level);return this.compose();}finally{this.active=null;}}
    update({camera=this.camera,width=this.width,height=this.height,hiddenPartIds=[...this.hidden],presentation=this.presentation,render=this.render}={}){if(this.closed)return;const settings=CadRenderer.math.renderSettings(render),key=JSON.stringify([camera,width,height,hiddenPartIds,presentation,settings]);if(this.viewKey===key)return;this.viewKey=key;for(const failed of this.pressureFailures)this.failed.delete(failed);this.pressureFailures.clear();this.pressureCaps.clear();this.pressureTried.clear();this.camera=camera;this.width=Math.max(1,width);this.height=Math.max(1,height);this.needsCompose=JSON.stringify(hiddenPartIds)!==JSON.stringify([...this.hidden])||this.omitted>0;this.hidden=new Set(hiddenPartIds);this.presentation=presentation;this.render=settings;clearTimeout(this.timer);this.timer=setTimeout(()=>this.pump(),80);}
    async relievePressure(task){
      // Refuse extra detail before sacrificing geometry coverage. A cap lasts
      // until the camera/visibility changes, preventing immediate re-refinement.
      if(this.resident.has(task.id)){const current=this.resident.get(task.id);if(task.level<=current)return false;this.pressureCaps.set(task.id,current);return true;}
      const priorities=new Map(this.priorities().map(p=>[p.id,p.score]));
      const candidates=[...this.resident].filter(([id,level])=>level>0&&!this.pressureTried.has(id)).sort((a,b)=>(priorities.get(a[0])||0)-(priorities.get(b[0])||0));
      for(const [id,level] of candidates){
        this.pressureTried.add(id);this.pressureCaps.set(id,0);
        try{await this.fetch(id,0);if(this.closed)return false;this.resident.set(id,0);this.onPayload(this.compose());return true;}
        catch(error){this.pressureCaps.set(id,level);if(this.closed)return false;}
      }
      return false;
    }
    async pump(){
      if(this.closed||this.running)return this.running;clearTimeout(this.timer);this.timer=null;
      this.running=(async()=>{if(this.needsCompose){this.needsCompose=false;const p=this.compose();if(p)this.onPayload(p);}let task;while(!this.closed&&(task=this.next())){try{await this.fetch(task.id,task.level);if(this.closed)break;this.resident.set(task.id,task.level);const p=this.compose();if(p)this.onPayload(p);this.failed.delete(task.id+':'+task.level);this.pressureFailures.delete(task.id+':'+task.level);}catch(e){if(e.code==='display_memory_budget'&&await this.relievePressure(task))continue;this.failed.set(task.id+':'+task.level,e.message);if(e.code==='display_memory_budget')this.pressureFailures.add(task.id+':'+task.level);this.error=e.message;this.onError(e);}finally{this.active=null;this.onStatus(this.status());}}})();
      try{await this.running;}finally{this.running=null;this.onStatus(this.status());const waiters=this.waiters.splice(0);for(const w of waiters){const error=this.failure();error?w.reject(Error(error)):w.resolve();}}
    }
    whenReady(){if(this.closed)return Promise.reject(Error('Progressive source retired.'));if(!this.running&&!this.needsCompose&&!this.next())return this.failure()?Promise.reject(Error(this.failure())):Promise.resolve();const promise=new Promise((resolve,reject)=>this.waiters.push({resolve,reject}));this.pump();return promise;}
    dispose(){if(this.closed)return;this.closed=true;clearTimeout(this.timer);if(this.active)this.request({...this.active,cancel:true}).catch(()=>{});for(const w of this.waiters.splice(0))w.reject(Error('Progressive source retired.'));this.pool.clear();this.resident.clear();}
  }
  CadProgressiveSession.validateManifest=validateManifest;CadProgressiveSession.validateComponent=validateComponent;globalThis.CadProgressiveSession=CadProgressiveSession;
})();
