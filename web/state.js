/* View coordination is separate from DOM/WebGL so revision races can be tested. */
(() => {
  'use strict';
  const copy = value => value == null ? value : JSON.parse(JSON.stringify(value));
  const playbackNow=()=>globalThis.performance?.now?.()??Date.now();
  const sourceKey = value => value?.read_only===true ? 'artifact:'+value.artifact?.review_sha256 : 'document:'+value?.document_id;
  const retryablePoll = code => ['workspace_busy', 'queue_full', 'stale_selection'].includes(code);
  const defaultPresentation=()=>({clip:null,explode:{distance_mm:0,directions:[]}});
  const presentationFor=(value,payload,prune=false)=>CadRenderer.math.presentation(value||defaultPresentation(),
    {partBounds:new Map((payload?.summary?.assembly?.parts||[]).map(part=>[part.id,true]))},{prune});
  const sectionGeometry=value=>CadRenderer.math.sectionGeometry(value);
  const sameSectionGeometry=(a,b)=>JSON.stringify(sectionGeometry(a))===JSON.stringify(sectionGeometry(b));
  const sectionQueryGeometry=query=>sectionGeometry({clip:{...query.plane,keep:'positive'},explode:query.explode||defaultPresentation().explode});
  const appearanceFor=(value,payload,prune=false)=>CadRenderer.math.appearance(value||CadRenderer.math.defaultAppearance(),
    {partBounds:new Map((payload?.summary?.assembly?.parts||[]).map(part=>[part.id,true]))},{prune});
  const presetName=name=>{
    if(typeof name!=='string'||!name.trim()||new TextEncoder().encode(name).length>64||/[\u0000-\u001f\u007f]/.test(name))throw Error('Use a view name of 1–64 UTF-8 bytes without control characters.');
    return name;
  };
  const sequenceSourceMatches=(source,p)=>source?.document_id===p?.document_id&&source?.revision===p?.revision&&source?.feature_id===p?.feature_id;
  function sequenceSample(sequence,time){
    const frames=sequence.frames,duration=frames.at(-1).time_s;if(!Number.isFinite(time)||time<0||time>duration)throw Error('Seek time is outside this sequence.');
    let upper=1;while(upper+1<frames.length&&frames[upper].time_s<time)upper++;
    const a=frames[upper-1],b=frames[upper],t=(time-a.time_s)/(b.time_s-a.time_s),sample=copy(a);sample.time_s=time;
    sample.presentation.explode.distance_mm=a.presentation.explode.distance_mm*(1-t)+b.presentation.explode.distance_mm*t;
    if(sample.presentation.clip)sample.presentation.clip.offset_mm=a.presentation.clip.offset_mm*(1-t)+b.presentation.clip.offset_mm*t;
    const ends=new Map(b.joints.flatMap(scope=>scope.values.map(v=>[scope.assembly_id+'/'+v.mate_id+'/'+v.coordinate,v.value])));
    for(const scope of sample.joints)for(const v of scope.values)v.value=v.value*(1-t)+ends.get(scope.assembly_id+'/'+v.mate_id+'/'+v.coordinate)*t;
    return sample;
  }
  class CadLiveState {
    constructor(bridge, changed = () => {}) {
      this.bridge = bridge; this.changed = changed; this.epoch = 0; this.busy = false; this.closed = false;
      this.contextQueue = Promise.resolve(); this.deliveryQueue = Promise.resolve(); this.contextVersion = 0; this.timer = null;
      this.visibilityVersion = 0; this.visibilitySaved = 0; this.snapshotVersions = new WeakMap(); this.motionBusy = false;
      this.presentationVersion=0;this.presentationSaved=0;
      this.measurementVersion=0;this.measureBusy=false;
      this.pointMeasurementSource=null;
      this.sectionVersion=0;this.sectionBusy=false;
      this.appearanceVersion=0;this.appearanceSaved=0;this.cameraVersion=0;this.cameraSaved=0;this.presetVersion=0;this.presetBusy=false;this.annotationVersion=0;this.annotationBusy=false;this.sequenceVersion=0;this.sequenceBusy=false;this.playbackTimer=null;this.playToken=0;
      this.value = { read_only:false,artifact:null,view_id: null, status: 'connecting', payload: null, selection: null, camera: null, model: null, error: null, hidden_part_ids: [], visibility_unsaved: false, presentation:defaultPresentation(),presentation_unsaved:false,saving: false, motion_error: null, preview_operations: null,measurement:null,measurement_status:null,measurement_targets:{a:null,b:null},measurement_error:null,measuring:false,section:null,section_status:null,section_error:null,sectioning:false,appearance:CadRenderer.math.defaultAppearance(),appearance_unsaved:false,presets:[],preset_error:null,preset_busy:false,preset_message:null,annotations:[],annotation_error:null,annotating:false,sequences:[],playback:null,playing:false,sequence_busy:false,sequence_error:null };
      this.clearPointMeasurement();
    }
    requireNative() { if(this.value.payload?.read_only===true||this.value.read_only)throw Error('This is a read-only artifact review. Open the editable native source for this action.'); }
    update(values) {
      Object.assign(this.value, values);
      if(this.pointMeasurementSource&&this.pointMeasurementSource!==this.pointMeasurementKey())this.clearPointMeasurement();
      this.changed(this.value);
    }
    pointMeasurementKey() {
      const p=this.value.payload;
      return this.value.status==='ready'&&p&&!p.draft?JSON.stringify([this.value.view_id,p.read_only?p.artifact.review_sha256:[p.document_id,p.revision,p.evaluation_id,p.feature_id],this.value.presentation,this.value.hidden_part_ids]):null;
    }
    clearPointMeasurement() { this.pointMeasurementSource=null;this.value.point_measurement={a:null,b:null,picking:null,active:false,error:null}; }
    requirePointMeasurement() {
      if(this.closed||!this.pointMeasurementKey()||this.value.preset_busy)throw Error('Wait for the current saved geometry before defining measurement points.');
    }
    beginPointMeasurement(slot='a') {
      this.requirePointMeasurement();
      if(!['a','b'].includes(slot))throw Error('Choose point A or B.');
      this.pointMeasurementSource=this.pointMeasurementKey();
      this.update({selection:null,point_measurement:{...this.value.point_measurement,picking:slot,active:true,error:null}});
    }
    stopPointMeasurement() {
      if(this.value.point_measurement?.active)this.update({point_measurement:{...this.value.point_measurement,picking:null,active:false,error:null}});
    }
    setMeasurementPoint(slot,point,input='coordinates') {
      this.requirePointMeasurement();
      if(!['a','b'].includes(slot)||!['coordinates','surface','edge'].includes(input)||!Array.isArray(point)||point.length!==3||!point.every(v=>Number.isFinite(v)&&Math.abs(v)<=1e9))throw Error('Define point A or B with three finite coordinates between −1,000,000,000 and 1,000,000,000 mm.');
      this.pointMeasurementSource=this.pointMeasurementKey();
      const m=this.value.point_measurement||{a:null,b:null};
      this.update({point_measurement:{...m,[slot]:{point_mm:[...point],input},picking:slot==='a'&&!m.b?'b':null,error:null}});
    }
    acceptMeasurementPoint(point,input='surface') {
      const slot=this.value.point_measurement?.picking;
      if(!slot)return;
      if(!point){this.update({point_measurement:{...this.value.point_measurement,error:'Click a visible model surface to define this point.'}});return;}
      this.setMeasurementPoint(slot,point,input);
    }
    attach(view_id) {
      if (typeof view_id !== 'string' || !/^[A-Za-z][A-Za-z0-9_-]{0,63}$/.test(view_id)) throw Error('Invalid CAD view identity.');
      if (this.value.view_id === view_id) return;
      this.clearSequences();this.epoch++; this.clearVisibility();this.clearPresentation();this.clearMeasurement();this.clearSection();this.clearAppearance();this.clearPresets();this.clearAnnotations();this.cameraSaved=++this.cameraVersion; this.update({ read_only:false,artifact:null,view_id, status: 'loading', payload: null, selection: null, camera: null, model: null, error: null, hidden_part_ids: [], saving: false, motion_error: null, preview_operations: null });
    }
    clearVisibility() { this.visibilitySaved = ++this.visibilityVersion; this.value.visibility_unsaved = false; }
    clearPresentation(){this.presentationSaved=++this.presentationVersion;this.value.presentation=defaultPresentation();this.value.presentation_unsaved=false;}
    clearMeasurement(){this.clearPointMeasurement();++this.measurementVersion;Object.assign(this.value,{measurement:null,measurement_status:null,measurement_targets:{a:null,b:null},measurement_error:null,measuring:false});}
    clearSection(){++this.sectionVersion;Object.assign(this.value,{section:null,section_status:null,section_error:null,sectioning:this.sectionBusy});}
    clearAppearance(){this.appearanceSaved=++this.appearanceVersion;this.value.appearance=CadRenderer.math.defaultAppearance();this.value.appearance_unsaved=false;}
    clearPresets(){++this.presetVersion;Object.assign(this.value,{presets:[],preset_error:null,preset_message:null,preset_busy:this.presetBusy});}
    clearAnnotations(){++this.annotationVersion;Object.assign(this.value,{annotations:[],annotation_error:null,annotating:this.annotationBusy});}
    clearSequences(){this.pausePlayback();++this.sequenceVersion;Object.assign(this.value,{sequences:[],playback:null,sequence_error:null,sequence_busy:this.sequenceBusy});}
    annotationsFor(notes,payload=this.value.payload){return CadRenderer.math.annotations(notes||[],notes?.some(note=>note.status==='current')?CadRenderer.math.prepare(payload):null,{draft:!!payload?.draft});}
    acceptAnnotations(notes,payload=this.value.payload){if(JSON.stringify(notes||[])===JSON.stringify(this.value.annotations))return false;const value=this.annotationsFor(notes,payload);if(JSON.stringify(value)!==JSON.stringify(this.value.annotations)){++this.annotationVersion;this.update({annotations:value,annotation_error:null});return true;}return false;}
    async annotation(operation,args={}) {
      const p=this.value.payload;if(p?.read_only===true||this.value.read_only===true)throw Error('Review pins require a saved native document. External artifact sessions are read only.');if(this.value.status!=='ready'||!p)throw Error('Wait for the current revision to finish loading.');
      if(!['list','add','update','delete','clear'].includes(operation))throw Error('Choose a review note operation.');
      if(operation==='add'&&p.draft)throw Error('Save or reset this pose before adding a review pin.');
      if(this.annotationBusy)throw Error('Wait for the current review note request.');
      const fields=operation==='add'?['text','anchor']:operation==='update'?['annotation_id','text']:operation==='delete'?['annotation_id']:[];
      if(!args||Object.keys(args).length!==fields.length||fields.some(key=>!Object.hasOwn(args,key)))throw Error('Invalid review note request.');
      if(fields.includes('text')&&(typeof args.text!=='string'||!args.text.trim()||new TextEncoder().encode(args.text).length>512||/[\u0000-\u0008\u000b-\u001f\u007f]/.test(args.text)))throw Error('Use 1–512 UTF-8 bytes of plain review text.');
      if(fields.includes('annotation_id')&&(typeof args.annotation_id!=='string'||!/^[A-Za-z][A-Za-z0-9_-]{0,63}$/.test(args.annotation_id)))throw Error('Invalid review note identity.');
      this.annotationBusy=true;++this.annotationVersion;this.update({annotating:true,annotation_error:null});
      const snapshot=this.snapshot(),epoch=this.epoch,current=()=>!this.closed&&epoch===this.epoch&&this.matches(snapshot);
      try {
        const action=this.contextQueue.then(async()=>{
          if(!current())throw Error('The view changed. Review it before updating notes.');
          const response=await this.bridge.tool('cad_viewer',{action:'annotation',view_id:snapshot.view_id,evaluation_id:snapshot.evaluation_id,operation,...copy(args)});
          if(!current())return;
          if(response.view_id!==snapshot.view_id||response.document_id!==p.document_id||response.revision!==p.revision||response.evaluation_id!==p.evaluation_id||response.feature_id!==p.feature_id||response.stale!==false||response.head_revision!==p.revision)throw Error('Review note response belongs to another source evaluation.');
          this.acceptAnnotations(response.annotations);this.update({annotation_error:null});this.publishSectionContext();return response;
        });this.contextQueue=action.catch(()=>{});return await action;
      }catch(error){if(current())this.update({annotation_error:error.message});throw error;}
      finally{this.annotationBusy=false;if(!this.closed)this.update({annotating:false});}
    }
    setCamera(value){
      const settings=CadRenderer.math.camera(value);
      if(JSON.stringify(settings)!==JSON.stringify(this.value.camera)){++this.cameraVersion;this.value.camera=settings;}
      return settings;
    }
    setAppearance(value){
      if(this.value.status!=='ready'||!this.value.payload)throw Error('Wait for the current revision to finish loading.');
      const settings=appearanceFor(value,this.value.payload);
      if(JSON.stringify(settings)===JSON.stringify(this.value.appearance)&&this.appearanceSaved===this.appearanceVersion)return Promise.resolve();
      ++this.appearanceVersion;
      this.update({appearance:settings,appearance_unsaved:true,context_error:null});
      const active=this.snapshot(),epoch=this.epoch;
      return this.publishContext().catch(error=>{if(epoch!==this.epoch||!this.matches(active))return;this.update({context_error:error.message});throw error;});
    }
    presetsFor(presets,payload=this.value.payload,prune=false){
      if(!Array.isArray(presets)||presets.length>16)throw Error('A view may contain up to 16 saved views.');
      const names=new Set(),parts=this.partIds(payload);
      return presets.map(preset=>{
        if(!preset||Object.keys(preset).length!==5||!['name','camera','presentation','hidden_part_ids','appearance'].every(key=>Object.hasOwn(preset,key)))throw Error('Invalid saved view settings.');
        const name=presetName(preset.name);if(names.has(name))throw Error('Saved view names must be unique.');names.add(name);
        const ids=preset.hidden_part_ids;
        if(!Array.isArray(ids)||ids.length>1024||new Set(ids).size!==ids.length||(!prune&&ids.some(id=>!parts.includes(id))))throw Error('Saved view visibility names an unknown leaf.');
        return{name,camera:CadRenderer.math.camera(preset.camera),presentation:presentationFor(preset.presentation,payload,prune),hidden_part_ids:parts.filter(id=>ids.includes(id)),appearance:appearanceFor(preset.appearance,payload,prune)};
      });
    }
    acceptPresets(presets,payload=this.value.payload,prune=false){
      const value=this.presetsFor(presets,payload,prune);
      if(JSON.stringify(value)!==JSON.stringify(this.value.presets)){++this.presetVersion;this.update({presets:value});}
    }
    async preset(operation,name){
      this.requireNative();
      const p=this.value.payload;
      if(this.value.status!=='ready'||!p)throw Error('Wait for the current revision to finish loading.');
      if(operation==='apply')this.pausePlayback();
      if(!['list','save','apply','delete'].includes(operation))throw Error('Choose a saved view operation.');
      if(operation!=='list')name=presetName(name);
      if(['save','apply'].includes(operation)&&p.draft)throw Error('Save or reset this pose before saving or applying a view.');
      if(this.presetBusy)throw Error('Wait for the current saved view request.');
      this.presetBusy=true;this.update({preset_busy:true,preset_error:null,preset_message:null});
      const snapshot=this.snapshot(),epoch=this.epoch;
      const current=()=>!this.closed&&epoch===this.epoch&&this.matches(snapshot);
      try{
        if(['save','apply'].includes(operation))await this.saveContext(snapshot);
        // Keep native preset mutations in the same order as context writes.
        // A later local view change invalidates this snapshot before admission
        // or leaves its queued context write to restore that newer choice.
        const action=this.contextQueue.then(async()=>{
          if(!current())throw Error('The view changed. Review it before using a saved view.');
          const response=await this.bridge.tool('cad_viewer',{action:'preset',view_id:snapshot.view_id,evaluation_id:snapshot.evaluation_id,operation,...(operation==='list'?{}:{name})});
          if(!current())return;
          if(response.view_id!==snapshot.view_id||response.document_id!==p.document_id||response.revision!==p.revision||response.evaluation_id!==p.evaluation_id||(response.feature_id!==undefined&&response.feature_id!==p.feature_id)||response.stale!==false||(response.head_revision!==undefined&&response.head_revision!==p.revision))throw Error('Saved view response belongs to another source evaluation.');
          const presets=this.presetsFor(response.presets);
          if(operation==='apply'){
            const presentation=presentationFor(response.presentation,p),appearance=appearanceFor(response.appearance,p),camera=CadRenderer.math.camera(response.camera),parts=this.partIds(),ids=response.hidden_part_ids;
            if(!Array.isArray(ids)||ids.length>1024||new Set(ids).size!==ids.length||ids.some(id=>!parts.includes(id)))throw Error('Saved view contains invalid visibility.');
            if(!sameSectionGeometry(presentation,this.value.presentation))this.clearSection();
            if(JSON.stringify(presentation)!==JSON.stringify(this.value.presentation)&&this.value.playback){++this.sequenceVersion;this.value.playback=null;}
            this.presentationSaved=++this.presentationVersion;this.appearanceSaved=++this.appearanceVersion;this.visibilitySaved=++this.visibilityVersion;this.cameraSaved=++this.cameraVersion;
            ++this.presetVersion;
            this.update({presentation,appearance,camera,hidden_part_ids:parts.filter(id=>ids.includes(id)),selection:null,presentation_unsaved:false,appearance_unsaved:false,visibility_unsaved:false,presets,preset_error:null,preset_message:`Applied “${name}”.`});
            await this.pollSection(response.section||null);
            this.publishSectionContext();
          }else{
            this.acceptPresets(presets);
            this.update({preset_error:null,preset_message:operation==='save'?`Saved “${name}”.`:operation==='delete'?`Deleted “${name}”.`:null});
          }
          return response;
        });
        this.contextQueue=action.catch(()=>{});return await action;
      }catch(error){if(!this.closed&&epoch===this.epoch&&this.value.payload?.evaluation_id===p.evaluation_id&&this.value.payload?.document_id===p.document_id)this.update({preset_error:error.message});throw error;}
      finally{this.presetBusy=false;if(!this.closed)this.update({preset_busy:false});}
    }
    sequencesFor(values){
      if(!Array.isArray(values)||values.length>16)throw Error('A view stores up to sixteen sequences.');
      const names=new Set(),closed=(v,keys)=>v&&typeof v==='object'&&!Array.isArray(v)&&Object.keys(v).length===keys.length&&keys.every(k=>Object.hasOwn(v,k)),id=v=>typeof v==='string'&&/^[A-Za-z][A-Za-z0-9_-]{0,63}$/.test(v);
      return values.map(sequence=>{
        if(!closed(sequence,['name','source','frames']))throw Error('Invalid sequence definition.');presetName(sequence.name);if(names.has(sequence.name))throw Error('Duplicate sequence name.');names.add(sequence.name);
        const source=sequence.source;
        if(!closed(source,['document_id','revision','evaluation_id','feature_id','model_sha256'])||!['document_id','evaluation_id','feature_id'].every(k=>id(source[k]))||!Number.isSafeInteger(source.revision)||source.revision<1||typeof source.model_sha256!=='string'||!/^[a-f0-9]{64}$/.test(source.model_sha256))throw Error('Invalid sequence source identity.');
        if(!Array.isArray(sequence.frames)||sequence.frames.length<2||sequence.frames.length>64||new TextEncoder().encode(JSON.stringify(sequence)).length>256*1024+1024)throw Error('Sequence exceeds its keyframe or byte limit.');
        let previous=-1,shape=null;
        for(const frame of sequence.frames){
          if(!closed(frame,['time_s','presentation','joints'])||!Number.isFinite(frame.time_s)||frame.time_s<0||frame.time_s>3600||frame.time_s<=previous||(previous<0&&frame.time_s!==0)||!Array.isArray(frame.joints)||frame.joints.length>16)throw Error('Invalid sequence keyframe.');previous=frame.time_s;
          const directions=frame.presentation?.explode?.directions;if(!Array.isArray(directions)||directions.some(v=>typeof v?.part_id!=='string'||!v.part_id.split('/').every(id)))throw Error('Invalid sequence occurrence path.');
          const owners=new Map(directions.map(v=>[v.part_id,true]));owners.set('historical_default',true);CadRenderer.math.presentation(frame.presentation,{partBounds:owners});
          let total=0;const scopes=new Map();for(const scope of frame.joints){if(!closed(scope,['assembly_id','values'])||!id(scope.assembly_id)||scopes.has(scope.assembly_id)||!Array.isArray(scope.values)||!scope.values.length||scope.values.length>126)throw Error('Invalid sequence mechanism.');const keys=new Set();
            for(const value of scope.values){const key=value?.mate_id+'/'+value?.coordinate;if(!closed(value,['mate_id','coordinate','value'])||!id(value.mate_id)||!['angle_deg','travel_mm'].includes(value.coordinate)||!Number.isFinite(value.value)||Math.abs(value.value)>1e6||keys.has(key))throw Error('Invalid sequence coordinate.');keys.add(key);}total+=scope.values.length;scopes.set(scope.assembly_id,[...keys].sort());}
          if(total>126)throw Error('Sequence exceeds 126 independent coordinates.');
          const presentation=copy(frame.presentation);presentation.explode.distance_mm=1;const geometry=sectionGeometry(presentation);const next=JSON.stringify({clip:frame.presentation.clip?{normal:frame.presentation.clip.normal,keep:frame.presentation.clip.keep}:null,explode:geometry?.explode||{distance_mm:1,directions:[...presentation.explode.directions].sort((a,b)=>a.part_id.localeCompare(b.part_id))},joints:[...scopes].sort((a,b)=>a[0].localeCompare(b[0]))});
          if(shape!==null&&shape!==next)throw Error('Sequence frames change their coordinate, plane or explode directions.');shape=next;
        }
        return copy(sequence);
      });
    }
    acceptSequences(values,playback=null){
      const sequences=this.sequencesFor(values),p=this.value.payload;
      if(playback!==null){
        const sequence=sequences.find(v=>v.name===playback.name);
        if(!sequence||Object.keys(playback).length!==6||!['name','time_s','speed','loop','source','state'].every(k=>Object.hasOwn(playback,k))||JSON.stringify(playback.source)!==JSON.stringify(sequence.source)||!Number.isFinite(playback.time_s)||playback.time_s<0||playback.time_s>sequence.frames.at(-1).time_s||!Number.isFinite(playback.speed)||playback.speed<.1||playback.speed>4||typeof playback.loop!=='boolean'||!['unapplied','pending','displayed'].includes(playback.state))throw Error('Invalid playback position.');
        if(!sequenceSourceMatches(playback.source,p))playback=null;
      }
      if(this.value.playing&&(!playback||!sequenceSourceMatches(playback.source,p)))this.pausePlayback();
      if(JSON.stringify(sequences)!==JSON.stringify(this.value.sequences)||JSON.stringify(playback)!==JSON.stringify(this.value.playback))++this.sequenceVersion;
      this.update({sequences,playback:copy(playback)});
    }
    captureSequenceFrame(time_s){
      const p=this.value.payload;if(p?.read_only||this.value.read_only)throw Error('Playback needs an editable native document.');
      if(this.value.status!=='ready'||!p||!Number.isFinite(time_s)||time_s<0||time_s>3600)throw Error('Wait for the current pose and choose a time from 0 to 3600 seconds.');
      const scopes=p.summary.assembly?.mechanisms||(p.summary.assembly?.motion?.dofs?.length?[{assembly_id:p.feature_id,motion:p.summary.assembly.motion}]:[]);
      const joints=scopes.map(scope=>({assembly_id:scope.assembly_id,values:scope.motion.dofs.filter(v=>!v.driven).map(v=>({mate_id:v.mate_id,coordinate:v.coordinate,value:v.value}))})).filter(scope=>scope.values.length);
      return{time_s,presentation:copy(this.value.presentation),joints};
    }
    async sequence(operation,fields={},{clock=false}={}){
      const p=this.value.payload;if(p?.read_only||this.value.read_only)throw Error('Playback needs an editable native document.');if(this.sequenceBusy||this.value.status!=='ready'||!p)throw Error('Wait for the current sequence sample.');
      if(!['list','save','delete','seek','options'].includes(operation))throw Error('Choose a sequence operation.');
      if(operation==='save'&&p.draft)throw Error('Reset this pose before saving the sequence.');
      if(!clock)this.pausePlayback();
      const snapshot=this.snapshot(),epoch=this.epoch;this.sequenceBusy=true;this.update({sequence_busy:true,sequence_error:null});let admittedEpoch=epoch;
      try{
        if(['save','seek','options'].includes(operation))await this.saveContext(snapshot);
        if(!this.matches(snapshot)||epoch!==this.epoch)throw Error('The view changed before the sequence request.');
        if(operation==='seek'){
          const definition=this.value.sequences.find(v=>v.name===fields.name),sample=definition?sequenceSample(definition,fields.time_s):null;
          ++this.epoch;admittedEpoch=this.epoch;++this.contextVersion;
          if(!sample||sample.joints.length||!sameSectionGeometry(sample.presentation,this.value.presentation))this.clearSection();
          this.clearMeasurement();this.update({status:'loading',selection:null});
        }
        const response=await this.bridge.tool('cad_viewer',{action:'sequence',view_id:snapshot.view_id,evaluation_id:snapshot.evaluation_id,operation,...copy(fields)});
        if(!this.closed&&this.epoch===admittedEpoch&&operation!=='seek'){
          if(!this.matches(snapshot))throw Error('The view changed during the sequence request.');
          if(response.view_id!==snapshot.view_id||response.document_id!==p.document_id||response.revision!==p.revision||response.head_revision!==p.revision||response.evaluation_id!==p.evaluation_id||response.feature_id!==p.feature_id||response.stale!==false)throw Error('Sequence response belongs to another source evaluation.');
          this.acceptSequences(response.sequences||[],response.playback||null);
        }
        return response;
      }catch(error){this.pausePlayback();if(!this.closed&&this.epoch===admittedEpoch)this.update({sequence_error:error.message});throw error;}
      finally{this.sequenceBusy=false;if(!this.closed)this.update({sequence_busy:false});if(operation==='seek'&&!this.closed&&this.epoch===admittedEpoch)await this.pollOnce();}
    }
    pausePlayback(){++this.playToken;clearTimeout(this.playbackTimer);this.playbackTimer=null;if(this.value?.playing)this.update({playing:false});}
    async playSequence(name){
      if(this.value.payload?.read_only||this.value.read_only)throw Error('Playback needs an editable native document.');
      if(this.value.status!=='ready'||this.sequenceBusy)throw Error('Wait for the current pose.');
      const sequence=this.value.sequences.find(v=>v.name===name),p=this.value.payload;
      if(!sequence||!sequenceSourceMatches(sequence.source,p))throw Error('This sequence belongs to an older source revision.');
      this.pausePlayback();const token=this.playToken,position=this.value.playback?.name===name?this.value.playback:null,duration=sequence.frames.at(-1).time_s;
      const start=position?.time_s===duration?0:position?.time_s||0,speed=position?.speed||1,loop=position?.loop||false,anchor=playbackNow();this.update({playing:true,sequence_error:null});
      const tick=async()=>{
        if(this.closed||token!==this.playToken||!this.value.playing)return;
        try{
          if(this.value.status==='ready'&&!this.sequenceBusy){
            const elapsed=start+Math.max(0,playbackNow()-anchor)/1000*speed,time=loop?elapsed%duration:Math.min(duration,elapsed);
            await this.sequence('seek',{name,time_s:time},{clock:true});
            if(!loop&&elapsed>=duration){this.pausePlayback();return;}
          } else if(this.value.status==='loading'&&!this.motionBusy&&!this.sequenceBusy)await this.pollOnce();
          if(this.value.status==='error')throw Error(this.value.error||'Sequence evaluation failed.');
          if(token===this.playToken&&this.value.playing)this.playbackTimer=setTimeout(tick,100);
        }catch(error){this.pausePlayback();if(!this.closed)this.update({sequence_error:error.message});}
      };
      await tick();
    }
    validateSectionMeta(meta){
      if(!meta||meta.evaluation_id!==this.value.payload?.evaluation_id||typeof meta.job_id!=='string'||!/^[A-Za-z][A-Za-z0-9_-]{0,63}$/.test(meta.job_id)||meta.query?.action!=='section')throw Error('Invalid section identity.');
      const geometry=sectionGeometry(this.value.presentation);
      if(!geometry||JSON.stringify(sectionQueryGeometry(meta.query))!==JSON.stringify(geometry))throw Error('Section plane or exploded placement changed.');
      return meta;
    }
    publishSectionContext(){
      if(!this.bridge.capabilities.updateModelContext||this.value.status!=='ready')return;
      const snapshot=this.snapshot(),version=++this.contextVersion,prior=this.deliveryQueue;
      // The native section action already persisted its metadata. Optional host
      // delivery must not add a writer lock or hold up geometry/job polling.
      const delivery=prior.then(async()=>{
        if(this.matches(snapshot)&&version===this.contextVersion)await this.bridge.request('ui/update-model-context',{content:[{type:'text',text:CadLiveState.promptText(snapshot)}],structuredContent:snapshot});
      });
      this.deliveryQueue=delivery.catch(error=>{if(this.matches(snapshot)&&version===this.contextVersion)this.update({context_error:error.message});});
    }
    acceptSection(response){
      const p=this.value.payload;
      if(response.view_id!==this.value.view_id||response.evaluation_id!==p?.evaluation_id||p.draft)throw Error('Section does not belong to this committed view.');
      if(!['empty','queued','running','cancelling','succeeded','failed','cancelled','interrupted'].includes(response.state))throw Error('Invalid section state.');
      const meta=response.state==='empty'?null:this.validateSectionMeta({evaluation_id:response.evaluation_id,job_id:response.job_id,query:response.query});
      if(response.state==='succeeded'){
        CadRenderer.math.validateSection(response.result,CadRenderer.math.prepare(p),this.value.presentation);
        const report=response.result.report,parts=meta.query.part_ids;
        if(parts&&(report.coverage!=='explicit_leaf_subset'||JSON.stringify(report.sections.map(scope=>scope.part_id).sort())!==JSON.stringify([...parts].sort())))throw Error('Section coverage does not match its query.');
        if(!parts&&report.coverage!==(this.partIds().length?'all_assembly_leaves':'feature_solids'))throw Error('Section coverage does not match its query.');
      }
      const changed=JSON.stringify(meta)!==JSON.stringify(this.value.section),stateChanged=response.state!==this.value.section_status?.state;
      if(changed)++this.sectionVersion;
      this.update({section:copy(meta),section_status:response,section_error:null});
      if(changed||stateChanged)this.publishSectionContext();
    }
    async section(query){
      this.requireNative();
      const p=this.value.payload;
      if(this.value.status!=='ready'||!p||p.draft)throw Error('Wait for committed geometry before calculating a section.');
      if(this.sectionBusy)throw Error('Wait for the current section request.');
      query=copy(query);
      if(query!==null){
        if(query?.action!=='section')throw Error('Choose an exact section query.');
        const geometry=sectionGeometry(this.value.presentation);
        if(!geometry||JSON.stringify(sectionQueryGeometry(query))!==JSON.stringify(geometry))throw Error('Section query must match the current clipping plane and exploded placement.');
      }
      const epoch=this.epoch,evaluation=p.evaluation_id,view=this.value.view_id;
      let version=this.sectionVersion;
      const current=()=>!this.closed&&epoch===this.epoch&&version===this.sectionVersion&&this.value.status==='ready'&&this.value.payload?.evaluation_id===evaluation;
      this.sectionBusy=true;this.update({sectioning:true,section_error:null});
      try{
        if(query!==null){
          // A queued presentation save may still be in flight. Persist this
          // snapshot before changing its section version, and do not await the
          // optional host acknowledgment handled by deliveryQueue.
          await this.saveContext(this.snapshot());
          if(!current())throw Error('The section view changed. Review its current plane before calculating.');
        }
        version=++this.sectionVersion;
        this.update({section:null,section_status:null,section_error:null});
        const response=await this.bridge.tool('cad_viewer',{action:'section',view_id:view,evaluation_id:evaluation,query:copy(query)});
        if(current())this.acceptSection(response);
      }catch(error){if(current())this.update({section_error:error.message});throw error;}
      finally{this.sectionBusy=false;if(!this.closed)this.update({sectioning:false});}
    }
    async pollSection(meta,observedVersion=this.sectionVersion){
      if(this.value.read_only||observedVersion!==this.sectionVersion||this.sectionBusy||this.value.status!=='ready'||this.value.payload?.draft)return;
      // Empty sync cannot dismiss an admission failure that has no job.
      if(!meta){if(this.value.section||(this.value.section_status&&this.value.section_status.state!=='empty')){this.clearSection();this.update({});this.publishSectionContext();}return;}
      const p=this.value.payload,epoch=this.epoch;
      let version=this.sectionVersion;
      const current=()=>!this.closed&&epoch===this.epoch&&version===this.sectionVersion&&this.value.status==='ready'&&p.evaluation_id===this.value.payload?.evaluation_id&&!this.sectionBusy;
      try{
        this.validateSectionMeta(meta);
        if(JSON.stringify(meta)!==JSON.stringify(this.value.section)){
          version=++this.sectionVersion;this.update({section:copy(meta),section_status:null,section_error:null});
        }else if(this.value.section_status?.state==='succeeded')return; // Completed geometry is immutable; sync still checks its current metadata.
        const response=await this.bridge.tool('cad_viewer',{action:'section',view_id:this.value.view_id,evaluation_id:p.evaluation_id});
        if(current())this.acceptSection(response);
      }catch(error){if(current()&&!retryablePoll(error.code))this.update({section_status:null,section_error:error.message});}
    }
    measurementTarget(target){
      this.requireNative();
      const p=this.value.payload;if(!p||p.draft)throw Error('Save or reset the pose before measuring its source geometry.');
      if(target?.kind==='part'&&this.partIds().includes(target.part_id))return copy(target);
      if(['face','edge'].includes(target?.kind)&&p.topology[target.kind==='face'?'faces':'edges'].some(item=>item.id===target.entity_id))return copy(target);
      throw Error('Choose a current face, edge or leaf part.');
    }
    setMeasurementTarget(slot,target){if(!['a','b'].includes(slot))throw Error('Choose measurement target A or B.');this.update({measurement_targets:{...this.value.measurement_targets,[slot]:target?this.measurementTarget(target):null}});}
    acceptMeasurement(response){
      const p=this.value.payload;
      if(response.view_id!==this.value.view_id||response.evaluation_id!==p?.evaluation_id)throw Error('Measurement does not belong to this view.');
      if(!['empty','queued','running','cancelling','succeeded','failed','cancelled','interrupted'].includes(response.state))throw Error('Invalid measurement state.');
      const meta=response.state==='empty'?null:{evaluation_id:response.evaluation_id,job_id:response.job_id,query:response.query};
      if(meta&&(!/^[A-Za-z][A-Za-z0-9_-]{0,63}$/.test(meta.job_id)||!['pair','clearance'].includes(meta.query?.action)))throw Error('Invalid measurement identity.');
      if(response.state==='succeeded'){
        const r=response.result;for(const key of ['document_id','revision','evaluation_id','feature_id'])if(r?.[key]!==p[key])throw Error('Measurement source identity changed.');
        const report=r.report;if(report?.method!=='exact_BRep_minimum_distance'||report.coordinate_space!=='committed_source_pose'||report.units!=='mm'||!Number.isFinite(report.minimum_distance_mm)||report.minimum_distance_mm<0||!Array.isArray(report.pairs)||!report.pairs.length||report.pairs.length>253)throw Error('Invalid exact measurement result.');
        for(const pair of report.pairs){if(!Number.isFinite(pair.distance_mm)||pair.distance_mm<0||!Array.isArray(pair.targets)||pair.targets.length!==2||!Array.isArray(pair.witnesses)||!pair.witnesses.length||pair.witnesses.length>16)throw Error('Invalid measurement witnesses.');
          for(const target of pair.targets)this.measurementTarget(target);
          for(const w of pair.witnesses){for(const point of [w.a_mm,w.b_mm])if(!Array.isArray(point)||point.length!==3||!point.every(Number.isFinite))throw Error('Invalid measurement point.');
            if(Math.abs(Math.hypot(...w.a_mm.map((x,i)=>x-w.b_mm[i]))-pair.distance_mm)>1e-7)throw Error('Measurement witness does not match its distance.');}}
        if(Math.abs(Math.min(...report.pairs.map(pair=>pair.distance_mm))-report.minimum_distance_mm)>1e-7)throw Error('Measurement minimum does not match its pairs.');
        if(report.action!==meta.query.action||(report.action==='pair'&&JSON.stringify(report.pairs[0].targets)!==JSON.stringify(meta.query.targets)))throw Error('Measurement result does not match its query.');
      }
      const changed=JSON.stringify(meta)!==JSON.stringify(this.value.measurement);if(changed)++this.measurementVersion;
      const targets=changed&&meta?.query.action==='pair'?{a:this.measurementTarget(meta.query.targets[0]),b:this.measurementTarget(meta.query.targets[1])}:this.value.measurement_targets;
      this.update({measurement:copy(meta),measurement_status:copy(response),measurement_targets:targets,measurement_error:null});
    }
    async measure(query){
      this.requireNative();
      const p=this.value.payload;if(this.value.status!=='ready'||!p||p.draft)throw Error('Wait for committed geometry before measuring.');
      if(this.measureBusy)throw Error('Wait for the current measurement request.');
      const version=++this.measurementVersion,epoch=this.epoch,evaluation=p.evaluation_id;this.measureBusy=true;this.update({measuring:true,measurement_error:null});
      const current=()=>!this.closed&&epoch===this.epoch&&version===this.measurementVersion&&this.value.payload?.evaluation_id===evaluation;
      try{const response=await this.bridge.tool('cad_viewer',{action:'measure',view_id:this.value.view_id,evaluation_id:evaluation,query:copy(query)});if(current())this.acceptMeasurement(response);}
      catch(error){if(current())this.update({measurement_error:error.message});throw error;}
      finally{this.measureBusy=false;if(!this.closed&&epoch===this.epoch)this.update({measuring:false});}
    }
    async pollMeasurement(meta,observedVersion=this.measurementVersion){
      if(observedVersion!==this.measurementVersion||this.measureBusy||this.value.payload?.draft)return;
      if(!meta){if(this.value.measurement){++this.measurementVersion;this.update({measurement:null,measurement_status:null,measurement_error:null});}return;}
      const p=this.value.payload,version=this.measurementVersion,epoch=this.epoch;
      try{const response=await this.bridge.tool('cad_viewer',{action:'measure',view_id:this.value.view_id,evaluation_id:p.evaluation_id});
        if(!this.closed&&epoch===this.epoch&&version===this.measurementVersion&&p.evaluation_id===this.value.payload?.evaluation_id&&!this.measureBusy)this.acceptMeasurement(response);}
      catch(error){if(!this.closed&&epoch===this.epoch&&version===this.measurementVersion&&!retryablePoll(error.code))this.update({measurement_error:error.message});}
    }
    setPresentation(value) {
      this.pausePlayback();
      if(this.value.status!=='ready'||!this.value.payload)throw Error('Wait for the current revision to finish loading.');
      const settings=presentationFor(value,this.value.payload);
      if(JSON.stringify(settings)===JSON.stringify(this.value.presentation)&&this.presentationSaved===this.presentationVersion)return Promise.resolve();
      if(!sameSectionGeometry(settings,this.value.presentation))this.clearSection();
      if(this.value.playback){++this.sequenceVersion;this.value.playback=null;}
      ++this.presentationVersion;
      this.update({presentation:settings,presentation_unsaved:true,selection:null,context_error:null});
      const active=this.snapshot(),epoch=this.epoch;
      return this.publishContext().catch(error=>{if(epoch!==this.epoch||!this.matches(active))return;this.update({context_error:error.message});throw error;});
    }
    partIds(payload = this.value.payload) { return (payload?.summary?.assembly?.parts || []).map(part => part.id); }
    occurrenceLeaves(id) {
      if (typeof id !== 'string') throw Error('Choose a current assembly part or subassembly.');
      const leaves = this.partIds().filter(part => part === id || part.startsWith(id + '/'));
      if (!leaves.length) throw Error('Choose a current assembly part or subassembly.');
      return leaves;
    }
    visibleSelection(selection, hidden) { return selection && hidden.includes(selection.geometry?.part_id) ? null : selection; }
    setHiddenParts(ids) {
      if (this.value.status !== 'ready' || !this.value.payload) throw Error('Wait for the current revision to finish loading.');
      const parts = this.partIds();
      if (!Array.isArray(ids) || ids.length > 1024 || new Set(ids).size !== ids.length || ids.some(id => !parts.includes(id))) throw Error('Visibility must name unique current assembly parts.');
      const hidden = parts.filter(id => ids.includes(id));
      if (JSON.stringify(hidden) === JSON.stringify(this.value.hidden_part_ids) && this.visibilitySaved === this.visibilityVersion) return Promise.resolve();
      ++this.visibilityVersion;
      this.update({ hidden_part_ids: hidden, visibility_unsaved: true, selection: this.visibleSelection(this.value.selection, hidden), context_error: null });
      const active=this.snapshot(),epoch=this.epoch;
      return this.publishContext().catch(error => {
        if (epoch!==this.epoch||!this.matches(active)) return;
        this.update({ context_error: error.message }); throw error;
      });
    }
    setPartVisible(id, visible) {
      if (typeof visible !== 'boolean') throw Error('Choose whether to show or hide this occurrence.');
      const leaves = new Set(this.occurrenceLeaves(id));
      return this.setHiddenParts(visible ? this.value.hidden_part_ids.filter(part => !leaves.has(part)) : [...new Set([...this.value.hidden_part_ids, ...leaves])]);
    }
    isolatePart(id) {
      const leaves = new Set(this.occurrenceLeaves(id));
      return this.setHiddenParts(this.partIds().filter(part => !leaves.has(part)));
    }
    showAll() { return this.setHiddenParts([]); }
    async editParameter(name, value) {
      this.requireNative();
      const p = this.value.payload;
      if (this.closed || this.value.status !== 'ready' || !p || p.draft || this.parameterBusy || this.motionBusy || this.presetBusy || this.sequenceBusy)
        throw Error('Wait for saved geometry before changing a parameter.');
      if (!Object.hasOwn(this.value.model?.parameters || {}, name) || !Number.isFinite(value) || Math.abs(value) > 1000000)
        throw Error('Choose a finite parameter value between −1,000,000 and 1,000,000.');
      if (this.value.model.parameters[name] === value) return;
      this.pausePlayback();
      const epoch = this.epoch, view = this.value.view_id, document_id = p.document_id, expected_revision = p.revision;
      const current = () => !this.closed && epoch === this.epoch && this.value.view_id === view && this.value.payload?.document_id === document_id;
      this.parameterBusy = true; this.update({ parameter_busy: true, parameter_source: document_id, parameter_error: null, parameter_message: `Updating ${name}…` });
      try {
        // One durable, revision-checked mutation per release. Never resubmit after
        // an uncertain acknowledgment: sync reconciles the committed source.
        let job = await this.bridge.tool('cad_job', { action: 'submit', request_id: `parameter_${crypto.randomUUID()}`, tool: 'cad_apply',
          arguments: { document_id, expected_revision, operations: [{ op: 'set_parameter', name, value }] }, budget: { timeout_ms: 30000, memory_mb: 2048 } });
        const deadline = Date.now() + 40000;
        while (['queued', 'running', 'cancelling'].includes(job.state)) {
          if (Date.now() > deadline) throw Error(`The edit is still pending. Read the current revision before retrying. Job: ${job.job_id}`);
          if (!current()) return;
          await new Promise(resolve => setTimeout(resolve, 200));
          job = await this.bridge.tool('cad_job', { action: 'get', job_id: job.job_id });
        }
        if (!current()) return;
        if (job.state !== 'succeeded') throw Error(job.error?.message || `Parameter edit ${job.state}.`);
        if (job.result?.document_id !== document_id || job.result?.revision !== expected_revision + 1) throw Error('The parameter result belongs to another revision. Read the saved model.');
        this.update({ selection: null, parameter_message: `Saved revision ${job.result.revision}` });
      } catch (error) {
        if (current()) this.update({ parameter_error: error.message, parameter_message: null });
        throw error;
      } finally {
        this.parameterBusy = false;
        if (!this.closed) this.update({ parameter_busy: false });
        if (current()) await this.pollOnce();
      }
    }
    async motion(action, fields = {}) {
      this.requireNative();
      this.pausePlayback();
      const v = this.value;
      if (this.motionBusy || !v.payload || (action !== 'motion_reset' && v.status !== 'ready')) throw Error('Wait for the current pose to finish loading.');
      if (v.saving && v.status !== 'error') throw Error('Wait for the pose save to finish.');
      const args = { action, view_id: v.view_id, evaluation_id: v.payload.evaluation_id, ...copy(fields) };
      this.motionBusy = true; this.epoch++; this.contextVersion++;this.clearSection();this.clearAnnotations();
      const epoch = this.epoch;
      this.update({ status: 'loading', selection: null, error: null, motion_error: null, saving: action === 'motion_save' });
      try {
        // Never retry a mutation after an uncertain acknowledgment. Sync reads
        // persisted view state and reconciles a request that did succeed.
        await this.bridge.tool('cad_viewer', args);
      } catch (error) {
        if (epoch === this.epoch && !this.closed) this.update({ motion_error: error.message });
        throw error;
      } finally {
        this.motionBusy = false;
        if (epoch === this.epoch && !this.closed) await this.pollOnce();
      }
    }
    previewValues(values, assembly_id) { return this.motion('motion_preview', { values, ...(assembly_id ? { assembly_id } : {}) }); }
    previewPose(pose_id, assembly_id) { return this.motion('motion_preview', { pose_id, ...(assembly_id ? { assembly_id } : {}) }); }
    resetMotion() { return this.motion('motion_reset'); }
    saveMotion(pose_id = '', assembly_id) { return this.motion('motion_save', { ...(pose_id ? { pose_id } : {}), ...(assembly_id ? { assembly_id } : {}) }); }
    invalidate() { this.pausePlayback();this.epoch++; this.contextVersion++;this.clearSection();this.clearAnnotations(); this.update({ status: 'loading', selection: null, error: null }); }
    async pollOnce() {
      if (this.busy || this.motionBusy || this.closed || !this.value.view_id) return;
      this.busy = true;
      const epoch = this.epoch, view = this.value.view_id, visibilityVersion = this.visibilityVersion,presentationVersion=this.presentationVersion,measurementVersion=this.measurementVersion,sectionVersion=this.sectionVersion,appearanceVersion=this.appearanceVersion,cameraVersion=this.cameraVersion,presetVersion=this.presetVersion,annotationVersion=this.annotationVersion;
      const pendingVisibility = this.visibilitySaved !== visibilityVersion;
      const current = () => !this.closed && epoch === this.epoch;
      const mayUseSavedVisibility = () => !this.presetBusy&&!pendingVisibility && visibilityVersion === this.visibilityVersion && this.visibilitySaved === this.visibilityVersion;
      const pendingPresentation=this.presentationSaved!==presentationVersion;
      const mayUseSavedPresentation=()=>!this.presetBusy&&!pendingPresentation&&presentationVersion===this.presentationVersion&&this.presentationSaved===this.presentationVersion;
      const pendingAppearance=this.appearanceSaved!==appearanceVersion,pendingCamera=this.cameraSaved!==cameraVersion;
      const mayUseSavedAppearance=()=>!this.presetBusy&&!pendingAppearance&&appearanceVersion===this.appearanceVersion&&this.appearanceSaved===this.appearanceVersion;
      const mayUseSavedCamera=()=>!this.presetBusy&&!pendingCamera&&cameraVersion===this.cameraVersion&&this.cameraSaved===this.cameraVersion;
      try {
        const state = await this.bridge.tool('cad_viewer', { action: 'sync', view_id: view,
          ...(this.value.payload ? { known_evaluation_id: this.value.payload.evaluation_id } : {}) });
        if (!current()) return;
        if (state.state !== 'ready') {
          if(state.state==='empty'||state.state==='error'||(this.value.payload&&(sourceKey(state)!==sourceKey(this.value.payload)||state.revision!==this.value.payload.revision)))this.pausePlayback();
          this.clearSection();this.clearAnnotations();
          const retarget = state.state === 'empty' || (this.value.payload && sourceKey(this.value.payload) !== sourceKey(state));
          if (retarget){this.clearSequences();this.clearVisibility();this.clearPresentation();this.clearMeasurement();this.clearAppearance();this.clearPresets();this.cameraSaved=++this.cameraVersion;}
          // Keep the last solid visible while building, but disable interaction with its old references.
          this.update({ status: state.state, selection: null, error: state.state === 'loading' && retryablePoll(state.error?.code) ? null : state.error?.message || null,
            document_id: state.document_id, revision: state.revision, saving: !!state.saving,
            ...(retarget ? { read_only:false,artifact:null,payload: null, model: null, camera: null, hidden_part_ids: [], preview_operations: null, motion_error: null } : {}) });
          return;
        }
        if (state.evaluation_id === this.value.payload?.evaluation_id && sourceKey(state)===sourceKey(this.value.payload)) {
          if(!this.sequenceBusy)this.acceptSequences(state.sequences||[],state.playback||null);
          const hidden = mayUseSavedVisibility() ? this.partIds().filter(id => (state.hidden_part_ids || []).includes(id)) : this.value.hidden_part_ids;
          const presentation=presentationFor(mayUseSavedPresentation()?(state.presentation||this.value.presentation):this.value.presentation,this.value.payload,true);
          const presentationChanged=JSON.stringify(presentation)!==JSON.stringify(this.value.presentation);
          const appearance=appearanceFor(mayUseSavedAppearance()?(state.appearance||this.value.appearance):this.value.appearance,this.value.payload,true);
          if(JSON.stringify(appearance)!==JSON.stringify(this.value.appearance)&&mayUseSavedAppearance()){this.appearanceSaved=++this.appearanceVersion;this.value.appearance_unsaved=false;}
          let camera=this.value.camera;
          if(state.camera&&mayUseSavedCamera()){camera=CadRenderer.math.camera(state.camera);if(JSON.stringify(camera)!==JSON.stringify(this.value.camera))this.cameraSaved=++this.cameraVersion;}
          if(presetVersion===this.presetVersion&&!this.presetBusy)this.acceptPresets(state.presets||this.value.presets,this.value.payload,true);
          if(!sameSectionGeometry(presentation,this.value.presentation)||(this.value.payload.draft&&(this.value.section||this.value.section_status||this.sectionBusy)))this.clearSection();
          if(presentationChanged&&mayUseSavedPresentation()){this.presentationSaved=++this.presentationVersion;this.value.presentation_unsaved=false;}
          if (JSON.stringify(hidden) !== JSON.stringify(this.value.hidden_part_ids)) this.clearVisibility();
          if(annotationVersion===this.annotationVersion&&!this.annotationBusy)this.acceptAnnotations(state.annotations||[]);
          this.update({ status: 'ready', error: null, saving: false, preview_operations: state.preview_operations || null,
            hidden_part_ids: hidden,presentation,appearance,camera,selection: this.value.payload.draft||presentationChanged ? null : this.visibleSelection(this.value.selection, hidden) });if(!this.value.read_only){await this.pollMeasurement(state.measurement||null,measurementVersion);await this.pollSection(state.section||null,sectionVersion);} return;
        }
        const retarget = this.value.payload && sourceKey(this.value.payload) !== sourceKey(state);
        this.clearMeasurement();this.clearSection();this.clearAnnotations();if (retarget){this.clearSequences();this.clearVisibility();this.clearPresentation();this.clearAppearance();this.clearPresets();this.cameraSaved=++this.cameraVersion;}
        this.update({ status: 'loading', selection: null, error: null, document_id: state.document_id, revision: state.revision,
          ...(retarget ? { read_only:false,artifact:null,payload: null, model: null, camera: null, hidden_part_ids: [] } : {}) });
        let offset = 0, total = null; const chunks = [];
        while (true) {
          const part = await this.bridge.tool('cad_viewer', { action: 'mesh', view_id: view, evaluation_id: state.evaluation_id, offset });
          if (!current()) return;
          if (typeof part.data !== 'string' || part.offset !== offset || !Number.isSafeInteger(part.total_bytes) ||
              part.total_bytes < 1 || part.total_bytes > 64 * 1024 * 1024 || (total !== null && total !== part.total_bytes) ||
              part.data.length < 1 || part.data.length > 128 * 1024 || /[^\x00-\x7f]/.test(part.data)) throw Error('Invalid CAD mesh transfer.');
          total = part.total_bytes; offset += part.data.length; chunks.push(part.data);
          if (offset > total) throw Error('CAD mesh transfer exceeded its declared size.');
          if (part.next_offset === null) { if (offset !== total) throw Error('Incomplete CAD mesh transfer.'); break; }
          if (part.next_offset !== offset || offset >= total) throw Error('Invalid CAD mesh continuation.');
        }
        const payload = JSON.parse(chunks.join(''));
        if (payload.evaluation_id !== state.evaluation_id || payload.document_id !== state.document_id ||
            payload.revision !== state.revision || !!payload.draft !== !!state.draft) throw Error('CAD mesh identity does not match the displayed revision.');
        // A revision may commit during transfer. Do not briefly expose those outdated picks.
        const confirmed = await this.bridge.tool('cad_viewer', { action: 'sync', view_id: view, known_evaluation_id: state.evaluation_id });
        if (!current()) return;
        if (confirmed.state !== 'ready' || confirmed.evaluation_id !== payload.evaluation_id) return;
        if(payload.read_only===true) {
          CadRenderer.math.validateArtifact(payload);
          if(confirmed.read_only!==true||confirmed.artifact?.review_sha256!==payload.artifact.review_sha256)throw Error('Artifact review changed during transfer.');
          const saved=await this.bridge.tool('cad_context',{view_id:view});if(!current())return;
          if(saved.read_only!==true||saved.evaluation_id!==payload.evaluation_id||saved.artifact?.review_sha256!==payload.artifact.review_sha256)return;
          const same=sourceKey(this.value.payload)===sourceKey(payload),ref=saved.selection;
          let selection=null;
          if(ref&&ref.review_sha256===payload.artifact.review_sha256&&['mesh_group','curve'].includes(ref.kind)){
            const geometry=payload.artifact_geometry[ref.kind==='mesh_group'?'groups':'polylines'].find(item=>item.id===ref.entity_id);
            if(geometry)selection={reference:ref,geometry};
          }
          const presentation=presentationFor(same&&!mayUseSavedPresentation()?this.value.presentation:(saved.presentation||confirmed.presentation),payload,true);
          const appearance=appearanceFor(same&&!mayUseSavedAppearance()?this.value.appearance:(saved.appearance||confirmed.appearance),payload,true);
          const camera=same&&!mayUseSavedCamera()?this.value.camera:(saved.camera?CadRenderer.math.camera(saved.camera):null);
          this.clearSequences();this.clearAnnotations();this.clearMeasurement();this.clearSection();this.clearPresets();
          this.update({status:'ready',read_only:true,artifact:payload.artifact,payload,model:null,selection,camera,presentation,appearance,hidden_part_ids:[],error:null,context_error:null,saving:false,preview_operations:null,document_id:null,revision:null});
          const version=this.contextVersion+1;this.publishContext().catch(error=>{if(current()&&version===this.contextVersion)this.update({context_error:error.message});});return;
        }
        const sameDocument = this.value.payload?.document_id === payload.document_id;
        let saved = null, savedHidden = null, savedCamera = null,savedPresentation=null,savedAppearance=null,savedPresets=null,selection = null;
        if (!sameDocument) {
          saved = await this.bridge.tool('cad_context', { view_id: view });
          if (!current()) return;
          if (saved.document_id !== payload.document_id || (saved.head_revision !== undefined && saved.head_revision !== payload.revision)) return;
          // This read is newer than the confirming sync. Native visibility
          // describes the current display even when the saved pick is stale.
          savedHidden = saved.hidden_part_ids || null;
          // The camera belongs to this document's view, not a topology pick.
          // Reopening after a build/revision change must still restore it.
          savedCamera = saved.camera || null;
          savedPresentation=saved.presentation||null;
          savedAppearance=saved.appearance||null;savedPresets=saved.presets||null;
          if (saved.stale || saved.document_id !== payload.document_id || saved.evaluation_id !== payload.evaluation_id) saved = null;
          if (saved?.selection && !payload.draft) {
            const ref = saved.selection, geometry = payload.topology[ref.kind === 'face' ? 'faces' : 'edges']?.find(item => item.id === ref.entity_id);
            if (geometry && ref.document_id === payload.document_id && ref.revision === payload.revision && ref.evaluation_id === payload.evaluation_id && ref.feature_id === payload.feature_id)
              selection = { reference: ref, geometry };
          }
        }
        const hidden = this.partIds(payload).filter(id => (sameDocument && !mayUseSavedVisibility() ? this.value.hidden_part_ids : (savedHidden || confirmed.hidden_part_ids || [])).includes(id));
        const presentation=presentationFor(sameDocument&&!mayUseSavedPresentation()?this.value.presentation:(savedPresentation||confirmed.presentation||state.presentation||this.value.presentation),payload,true);
        const appearance=appearanceFor(sameDocument&&!mayUseSavedAppearance()?this.value.appearance:(savedAppearance||confirmed.appearance||state.appearance||this.value.appearance),payload,true);
        const camera=sameDocument?(mayUseSavedCamera()&&confirmed.camera?CadRenderer.math.camera(confirmed.camera):this.value.camera):savedCamera;
        if(JSON.stringify(appearance)!==JSON.stringify(this.value.appearance)){this.appearanceSaved=++this.appearanceVersion;this.value.appearance_unsaved=false;}
        if(JSON.stringify(camera)!==JSON.stringify(this.value.camera))this.cameraSaved=++this.cameraVersion;
        const presets=this.presetsFor(savedPresets||confirmed.presets||state.presets||this.value.presets,payload,true);
        if(JSON.stringify(presets)!==JSON.stringify(this.value.presets))++this.presetVersion;
        const restoredSectionVersion=this.sectionVersion;
        const annotations=this.annotationsFor(confirmed.annotations||[],payload);++this.annotationVersion;
        this.update({ read_only:false,artifact:null,status: 'ready', payload,annotations,annotation_error:null, selection: this.visibleSelection(selection, hidden), model: state.model, error: null, context_error: null, hidden_part_ids: hidden,
          presentation,appearance,presets,saving: false, preview_operations: confirmed.preview_operations || state.preview_operations || null,
          document_id: payload.document_id, revision: payload.revision,
          camera });
        if(!this.sequenceBusy)this.acceptSequences(confirmed.sequences||[],confirmed.playback||null);
        await this.pollMeasurement(confirmed.measurement||saved?.measurement||null);
        await this.pollSection(saved?.section||confirmed.section||null,restoredSectionVersion);
        // Host context is optional and may acknowledge slowly. It must not hold up HEAD polling.
        const publicationVersion = this.contextVersion + 1;
        this.publishContext().catch(error => { if (current() && publicationVersion === this.contextVersion) this.update({ context_error: error.message }); });
      } catch (error) {
        // Polling is read-only: a brief lock conflict or an evaluation superseded
        // during transfer can be retried by the next bounded poll. Old picks
        // stay disabled while the last successfully rendered solid is retained.
        // This does not retry context publication, mutations, or ui/message.
        if (current()){this.clearSection();this.clearAnnotations();this.update({ status: retryablePoll(error.code) ? 'loading' : 'error', selection: null,
          error: retryablePoll(error.code) ? null : error.message });}
      } finally { this.busy = false; }
    }
    async start() {
      if (this.closed) return;
      await this.pollOnce();
      if (!this.closed) this.timer = setTimeout(() => this.start(), 1000);
    }
    snapshot(prompt = '') {
      const v = this.value, p = v.payload;
      if (v.status !== 'ready' || !p) throw Error('Wait for the current revision to finish loading.');
      const snapshot = copy({ view_id: v.view_id, document_id: p.document_id, revision: p.revision,
        evaluation_id: p.evaluation_id, feature_id: p.feature_id,...(p.read_only?{read_only:true,artifact:p.artifact}:{}),selection: p.draft ? null : v.selection?.reference || null,
        ...(p.draft ? { draft: true, preview_operations: v.preview_operations } : {}),
        hidden_part_ids: v.hidden_part_ids,presentation:v.presentation,appearance:v.appearance,annotations:v.annotations,...(v.playback?{playback:v.playback}:{}),...(v.measurement?{measurement:v.measurement}:{}),...(v.section?{section:v.section}:{}),...(v.camera ? { camera: v.camera } : {}), prompt });
      this.snapshotVersions.set(snapshot,[this.visibilityVersion,this.presentationVersion,this.measurementVersion,this.sectionVersion,this.appearanceVersion,this.cameraVersion,this.annotationVersion,this.sequenceVersion]); return snapshot;
    }
    matches(snapshot) { const versions=this.snapshotVersions.get(snapshot);return !this.closed && this.value.status === 'ready' && this.value.view_id === snapshot.view_id && this.value.payload?.evaluation_id === snapshot.evaluation_id && (!versions||(versions[0]===this.visibilityVersion&&versions[1]===this.presentationVersion&&versions[2]===this.measurementVersion&&versions[3]===this.sectionVersion&&versions[4]===this.appearanceVersion&&versions[5]===this.cameraVersion&&versions[6]===this.annotationVersion&&versions[7]===this.sequenceVersion)); }
    saveContext(snapshot) {
      const action = this.contextQueue.then(async () => {
        if (!this.matches(snapshot)) throw Error('The model changed. Wait for the current revision before using this selection.');
        const { view_id, evaluation_id, selection, camera, prompt, hidden_part_ids,presentation,appearance } = snapshot;
        const result = await this.bridge.tool('cad_viewer', { action: 'context', view_id, evaluation_id, selection,
          hidden_part_ids,presentation,appearance,...(camera ? { camera } : {}), prompt });
        if (!this.matches(snapshot)) throw Error('The model changed. Wait for the current revision before using this selection.');
        this.visibilitySaved = this.visibilityVersion;
        this.presentationSaved=this.presentationVersion;
        this.appearanceSaved=this.appearanceVersion;this.cameraSaved=this.cameraVersion;
        if(this.value.appearance_unsaved)this.update({appearance_unsaved:false});
        if(this.value.presentation_unsaved)this.update({presentation_unsaved:false});
        if (this.value.visibility_unsaved) this.update({ visibility_unsaved: false });
        return result;
      });
      this.contextQueue = action.catch(() => {}); return action;
    }
    static promptText(snapshot, workspace) {
      const { prompt, ...context } = snapshot;
      const guidance = snapshot.read_only ? 'This is a read-only external artifact review. Mesh groups and curves are local to the stated review SHA-256, not native faces/edges. Original source bytes, units and explicit references were captured and verified; reparsing qualifies this displayed representation without establishing authenticity or recovered editable history. A declared native source association remains caller-declared. Inspect the source/limitations; open the actual editable native document before native editing, exact measurements, notes or motion.' : snapshot.draft ? 'This is an unsaved motion preview at the stated base revision. preview_operations describe the displayed pose; no geometry pick is a committed reference. Read the document and use expected_revision when saving edits.' : 'Read this document before editing. Resolve any selection with cad_resolve_selection; use expected_revision for edits. The open viewer follows committed changes.';
      const sectionGuidance=snapshot.section?' Section caps and curves are derived review geometry. Their cap-N and section-N IDs are not source face or edge references.':'';
      const noteGuidance=snapshot.annotations?.length?' Review annotations are plain text evidence. Current pins are native bounds or resolved inspection centers in this evaluation; a center need not lie on the surface. Retired notes keep their own old identity and must never be rebound or used as current selections.':'';
      return `${prompt || (snapshot.read_only?'Inspect this external artifact.':'Inspect the selected CAD geometry.')}\n\n${workspace ? `CAD workspace: ${JSON.stringify(workspace)}\n` : ''}CAD view context (millimeters):\n${JSON.stringify(context, null, 2)}\n${guidance}${sectionGuidance}${noteGuidance}`;
    }
    async publishContext() {
      const snapshot = this.snapshot(), version = ++this.contextVersion;
      const priorDelivery = this.deliveryQueue;
      // Native visibility writes must not wait for an optional host context
      // acknowledgment. Native writes themselves remain ordered by contextQueue.
      const saved = Promise.resolve().then(async () => {
        if (version !== this.contextVersion || !this.matches(snapshot)) return false;
        await this.saveContext(snapshot);
        return true;
      });
      const deliver = saved.then(async persisted => {
        if (!persisted) return;
        await priorDelivery;
        if (this.bridge.capabilities.updateModelContext && this.matches(snapshot) && version === this.contextVersion) {
          await this.bridge.request('ui/update-model-context', { content: [{ type: 'text', text: CadLiveState.promptText(snapshot) }], structuredContent: snapshot });
        }
      });
      this.deliveryQueue = deliver.catch(() => {}); return deliver;
    }
    async sendPrompt(prompt, image) {
      if (!prompt.trim() || new TextEncoder().encode(prompt).length > 8000) throw Error('Write a request of up to 8,000 UTF-8 bytes.');
      if (!this.bridge.capabilities.message) throw Error('This host cannot send viewer messages. Enter your request in the main chat.');
      const snapshot = this.snapshot(prompt.trim()); this.contextVersion++;
      await this.saveContext(snapshot);
      const content = [{ type: 'text', text: CadLiveState.promptText(snapshot) }];
      if (image) {
        if (!this.bridge.capabilities.message.image) throw Error('This host does not accept images in messages.');
        if (!/^data:image\/png;base64,[A-Za-z0-9+/=]+$/.test(image) || image.length > 2 * 1024 * 1024) throw Error('The viewport image exceeds the 2 MiB limit.');
        content.push({ type: 'image', mimeType: 'image/png', data: image.slice(image.indexOf(',') + 1) });
      }
      // Deliberately never retry: a lost acknowledgment can still mean delivery succeeded.
      await this.bridge.request('ui/message', { role: 'user', content }, 30000);
    }
    dispose() { this.pausePlayback();this.closed = true; this.epoch++; clearTimeout(this.timer); }
  }
  CadLiveState.sequenceSample=sequenceSample;
  globalThis.CadLiveState = CadLiveState;
})();
