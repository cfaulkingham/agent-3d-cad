// Actual controller with a deterministic clock and bounded native-operation mock.
// Native solids and the real stdio flow are covered by separate fixtures.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import vm from 'node:vm';
let checks=0,now=0,nextTimer=1;const timers=new Map();
const environment={console,TextEncoder,performance:{now:()=>now},setTimeout:(callback,ms)=>{const id=nextTimer++;timers.set(id,{callback,due:now+ms});return id;},clearTimeout:id=>timers.delete(id)};
for(const file of ['renderer.js','state.js'])vm.runInNewContext(fs.readFileSync(new URL('../web/'+file,import.meta.url),'utf8'),environment,{filename:file});
const State=environment.CadLiveState,copy=v=>JSON.parse(JSON.stringify(v)),check=(value,message)=>{assert.ok(value,message);checks++;};
const presentation=(distance=0)=>({clip:{normal:[0,0,1],offset_mm:4,keep:'positive'},explode:{distance_mm:distance,directions:[{part_id:'lever',direction:[1,0,0]}]}});
const source={document_id:'part',revision:1,evaluation_id:'eval_original',feature_id:'mechanism',model_sha256:'a'.repeat(64)};
const frame=(time,angle,travel,distance=0)=>({time_s:time,presentation:presentation(distance),joints:[{assembly_id:'mechanism',values:[{mate_id:'hinge',coordinate:'angle_deg',value:angle},{mate_id:'spindle',coordinate:'travel_mm',value:travel}]}]});
const sequence={name:'Coordinated',source,frames:[frame(0,0,0),frame(2,90,18,20)]};sequence.frames[1].joints[0].values.reverse();
const position=(time_s=0,extra={})=>({name:sequence.name,time_s,speed:1,loop:false,source,state:'displayed',...extra});
const payload=(eid='eval_original')=>({schema_version:1,document_id:'part',revision:1,evaluation_id:eid,feature_id:'mechanism',draft:false,mesh:{},topology:{},summary:{assembly:{parts:[{id:'lever'}],motion:{dofs:[{mate_id:'hinge',coordinate:'angle_deg',value:30,driven:false},{mate_id:'rail',coordinate:'travel_mm',value:3,driven:true},{mate_id:'spindle',coordinate:'travel_mm',value:6,driven:false}]}}}});
async function advance(ms){now+=ms;for(const [id,timer] of [...timers])if(timer.due<=now){timers.delete(id);await timer.callback();}}
function mock(definition=sequence){
  const calls=[];let value=payload(),playback=position(),p=presentation(),saved=copy(definition),pending=false,gate=null,contextGate=null,lost=false;
  const summary=()=>({state:pending?'loading':'ready',document_id:value.document_id,revision:value.revision,evaluation_id:value.evaluation_id,feature_id:value.feature_id,draft:value.draft,model:{features:[],parameters:{}},presentation:p,appearance:environment.CadRenderer.math.defaultAppearance(),hidden_part_ids:[],sequences:[saved],playback,...(value.draft?{preview_operations:[]}:{} )});
  const context=()=>({...summary(),view_id:'review',selection:null,head_revision:value.revision,stale:false});
  const bridge={capabilities:{},async request(){return{};},async tool(name,args){
    calls.push(copy({name,args}));
    if(name==='cad_context')return context();
    if(args.action==='context'){if(contextGate)await contextGate;return context();}
    if(args.action==='sync')return summary();
    if(args.action==='mesh'){const data=JSON.stringify(value);return{data,offset:0,next_offset:null,total_bytes:data.length};}
    if(args.action==='sequence'){
      if(args.operation==='options')playback={...(playback||position()),speed:args.speed??playback.speed,loop:args.loop??playback.loop};
      if(args.operation==='save')saved={...copy(args.sequence),source:copy(source)};
      if(args.operation==='seek'){
        check(state.value.selection===null&&state.value.status==='loading','Old pick disabled before draft admission');
        playback=position(args.time_s,{speed:playback?.speed||1,loop:playback?.loop||false});p=State.sequenceSample(saved,args.time_s).presentation;
        if(gate)await gate;if(lost){lost=false;throw Error('Seek acknowledgement was lost');}
      }
      if(gate&&args.operation!=='seek')await gate;return context();
    }
    throw Error('Unexpected action '+args.action);
  }};
  const state=new State(bridge);state.value={...state.value,view_id:'review',status:'ready',payload:value,presentation:p};state.acceptSequences([saved],playback);
  state.pollSection=async()=>{};state.pollMeasurement=async()=>{};
  return{state,calls,block(){let release;gate=new Promise(resolve=>release=()=>{gate=null;resolve();});return release;},lost(){lost=true;},blockContext(){let release;contextGate=new Promise(resolve=>release=()=>{contextGate=null;resolve();});return release;},draft(){value.draft=true;},pending(value_=true){pending=value_;},revise(){value={...value,revision:2,evaluation_id:'eval_second'};playback=null;},position(){return playback;},dispose(){state.dispose();timers.clear();}};
}
{
  const sample=State.sequenceSample(sequence,1);check(sample.joints[0].values[0].value===45&&sample.joints[0].values[1].value===9,'Joint interpolation follows identities across reordered endpoint arrays');
  check(sample.presentation.explode.distance_mm===10&&sequence.frames[0].joints[0].values[0].value===0,'Exploded interpolation leaves saved source frames immutable');
  assert.throws(()=>State.sequenceSample(sequence,-1));checks++;assert.throws(()=>State.sequenceSample(sequence,Infinity));checks++;
}
{
  const m=mock(),captured=m.state.captureSequenceFrame(0);check(captured.joints[0].values.length===2&&captured.joints[0].values.every(v=>v.mate_id!=='rail'),'Captured frame omits driven coordinates');
  const snapshot=m.state.snapshot('Inspect this paused pose');check(snapshot.playback.time_s===0&&snapshot.playback.source.model_sha256===source.model_sha256,'Agent snapshot includes paused time and exact saved source identity');
  m.state.acceptSequences([sequence],position(1));check(!m.state.matches(snapshot),'Time change invalidates a pending agent snapshot even with the same evaluation');
  for(const modify of [s=>s.frames[0].time_s=.1,s=>s.frames[1].time_s=0,s=>s.frames[1].joints[0].values.pop(),s=>s.frames[1].joints[0].values.push(s.frames[1].joints[0].values[0]),s=>s.frames[1].presentation.clip.keep='negative',s=>s.frames[0].presentation.explode.directions[0].part_id='../bad',s=>s.source.model_sha256='BAD',s=>s.script='forbidden']){const bad=copy(sequence);modify(bad);assert.throws(()=>m.state.sequencesFor([bad]));checks++;}
  const historical=copy(sequence);historical.source.revision=2;check(m.state.sequencesFor([historical]).length===1,'Source-qualified historical definitions remain inspectable');await assert.rejects(m.state.playSequence('missing'));checks++;m.dispose();
}
{
  const m=mock();await m.state.sequence('options',{name:sequence.name,speed:2,loop:false});await m.state.playSequence(sequence.name);await advance(250);
  check(Math.abs(m.position().time_s-.5)<1e-12&&m.state.value.playing,'Clock speed advances native samples');
  await advance(1000);check(m.position().time_s===2&&!m.state.value.playing&&timers.size===0,'Non-looping clock stops exactly at the endpoint');
  m.state.acceptSequences([sequence],position(1.8,{speed:1,loop:true}));await m.state.playSequence(sequence.name);await advance(500);check(Math.abs(m.position().time_s-.3)<1e-10,'Loop wraps time into the bounded sequence interval');
  m.state.pausePlayback();const count=m.calls.filter(v=>v.args.operation==='seek').length;await advance(1000);check(count===m.calls.filter(v=>v.args.operation==='seek').length,'Pause cancels future sample admission');m.dispose();
}
{
  const m=mock(),release=m.block();m.state.value.selection={reference:{entity_id:'face-1'}};const playing=m.state.playSequence(sequence.name);await new Promise(resolve=>setImmediate(resolve));
  check(m.state.sequenceBusy&&m.calls.filter(v=>v.args.operation==='seek').length===1,'Only one native seek can be in flight');await advance(1000);check(m.calls.filter(v=>v.args.operation==='seek').length===1,'Elapsed time cannot queue extra native evaluations');
  m.state.pausePlayback();release();await playing;check(!m.state.value.playing&&timers.size===0,'Late native acknowledgement cannot restart a paused clock');m.dispose();
}
{
  const m=mock();m.lost();await assert.rejects(m.state.sequence('seek',{name:sequence.name,time_s:1}),/acknowledgement/);checks++;
  check(m.state.value.status==='ready'&&m.state.value.playback.time_s===1&&!m.state.value.playing,'Lost acknowledgement reconciles admitted native position through read-only sync');
  check(m.calls.filter(v=>v.args.operation==='seek').length===1,'Uncertain seek never replays the mutation');m.dispose();
}
{
  const visual=copy(sequence);for(const f of visual.frames){f.joints=[];f.presentation=presentation();}const m=mock(visual);m.state.value.section={evaluation_id:'eval_original',job_id:'section_job',query:{}};
  await m.state.sequence('seek',{name:visual.name,time_s:1});check(m.state.value.section?.job_id==='section_job','Matching pure presentation seek retains exact cap metadata');
  m.dispose();
}
{
  const m=mock();await m.state.playSequence(sequence.name);m.pending();await advance(100);const count=m.calls.filter(v=>v.args.operation==='seek').length;await advance(500);check(m.calls.filter(v=>v.args.operation==='seek').length===count,'Loading native sample is polled without admitting another evaluation');
  m.revise();await m.state.pollOnce();check(!m.state.value.playing,'Source revision change stops clock immediately');m.dispose();
}
{
  const m=mock(),release=m.block(),pending=m.state.sequence('seek',{name:sequence.name,time_s:1});await new Promise(resolve=>setImmediate(resolve));m.state.attach('other');release();await pending;check(m.state.value.view_id==='other'&&m.state.value.playback===null,'Late seek reply cannot relabel a retargeted view');m.dispose();
}
{
  const m=mock();await m.state.playSequence(sequence.name);m.state.setPresentation(presentation(5));check(!m.state.value.playing&&m.state.value.playback===null,'Manual presentation edits pause playback and retire its time claim');await m.state.contextQueue;m.dispose();
}
{const m=mock();m.state.value.payload.read_only=true;assert.throws(()=>m.state.captureSequenceFrame(0),/editable/);checks++;await assert.rejects(m.state.sequence('list'),/editable/);checks++;await assert.rejects(m.state.playSequence(sequence.name),/editable/);checks++;m.dispose();}
{const m=mock(),release=m.block(),pending=m.state.sequence('options',{name:sequence.name,speed:2});await new Promise(resolve=>setImmediate(resolve));await m.state.setPresentation(presentation(5));release();await assert.rejects(pending,/view changed/);checks++;check(m.state.value.playback===null&&m.state.value.presentation.explode.distance_mm===5,'Delayed sequence options cannot overwrite a newer manual presentation');m.dispose();}
{const m=mock();m.draft();const snapshot=m.state.snapshot(),sectionVersion=m.state.sectionVersion,release=m.blockContext(),pending=m.state.saveContext(snapshot);await new Promise(resolve=>setImmediate(resolve));await m.state.pollOnce();release();await pending;check(m.state.matches(snapshot)&&m.state.sectionVersion===sectionVersion,'Polling a draft with no cap cannot invalidate a pending clock context');m.dispose();}
{const m=mock();m.draft();m.state.value.section_status={state:'succeeded',result:{}};const sectionVersion=m.state.sectionVersion;await m.state.pollOnce();check(m.state.value.section_status===null&&m.state.sectionVersion>sectionVersion,'A draft poll still retires existing exact section geometry');m.dispose();}
console.log(`PASS ${checks} playback controller checks`);
