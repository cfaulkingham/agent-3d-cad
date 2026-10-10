// Exercises the actual export event handler with delayed desktop/job responses.
// No DOM renderer, native geometry, filesystem export, or browser claim.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import vm from 'node:vm';
const file=process.argv[2]||new URL('../web/app.js',import.meta.url);
const source=fs.readFileSync(file,'utf8'),start=source.indexOf("  $('export-model').onclick = async () => {"),end=source.indexOf("  $('fit').onclick",start);
assert.ok(start>=0&&end>start,'Production export event handler must be present');
const handler=source.slice(start,end);let checks=0;
const check=(condition,label)=>{assert.ok(condition,label);checks++;};
const turn=()=>new Promise(resolve=>setImmediate(resolve));
function deferred(){let resolve,reject;const promise=new Promise((ok,bad)=>{resolve=ok;reject=bad;});return{promise,resolve,reject};}
function fixture(mode='job',format='step'){
 const response=deferred(),nodes=new Map(),calls=[],messages=[],updates=[],timers=[];
 const state={epoch:1,value:{view_id:'main',status:'ready',read_only:false,payload:{document_id:'Part',revision:3,evaluation_id:'eval_3',draft:false,read_only:false},camera:{yaw:0,pitch:0,zoom:1,pan:[0,0]}}};
 const node=id=>{if(!nodes.has(id))nodes.set(id,{value:'',hidden:true,open:false});return nodes.get(id);};node('export-format').value=format;
 let nextJob=null;
 const bridge=mode==='desktop'?{desktop:{async export(args){calls.push({kind:'desktop',args});return response.promise;}}}:{async tool(name,args){calls.push({name,args});return args.action==='submit'?response.promise:nextJob;}};
 const environment={state,bridge,$:node,status:message=>messages.push(message),update:value=>updates.push(value),crypto:{randomUUID:()=> 'test-export'},Date,setTimeout:callback=>{timers.push(callback);return timers.length;},control:null};
 vm.runInNewContext("let exporting=false,disposed=false;\n"+handler+"\ncontrol={click:()=>$('export-model').onclick(),dispose:()=>{disposed=true;},exporting:()=>exporting};",environment,{filename:'production-export-handler'});
 return{...environment,nodes,calls,messages,updates,timers,response,setNextJob:job=>{nextJob=job;},retarget(change){
  if(change==='generation')state.epoch++;
  else if(change==='view')state.value.view_id='other';
  else if(change==='document')state.value.payload.document_id='Other';
  else if(change==='revision')state.value.payload.revision++;
  else if(change==='evaluation')state.value.payload.evaluation_id='eval_new';
  else if(change==='read_only')state.value.read_only=true;
  else if(change==='artifact')state.value.payload={read_only:true,document_id:null,revision:null,evaluation_id:'artifact_hash',draft:false};
  else if(change==='disposed')environment.control.dispose();
  else throw Error('Unknown change');
  messages.push('Current view status');node('copy-text').value='Current view fallback';node('copy-fallback').hidden=true;node('copy-fallback').open=false;
 }};
}
for(const mode of ['desktop','job'])for(const flag of ['value','payload']){
 const f=fixture(mode);if(flag==='value')f.state.value.read_only=true;else f.state.value.payload.read_only=true;
 const pending=f.control.click();await turn();check(f.calls.length===0&&!f.control.exporting()&&!f.messages.length,'Read-only '+flag+' short-circuits '+mode+' before export admission');await pending;
}
for(const mode of ['desktop','job'])for(const change of ['generation','view','document','revision','evaluation','read_only','artifact','disposed'])for(const failed of [false,true]){
 const f=fixture(mode),pending=f.control.click();await turn();check(f.calls.length===1,'Export has one admitted '+mode+' request');f.retarget(change);
 if(failed)f.response.reject(Error('Old source export failed'));else f.response.resolve(mode==='desktop'?{cancelled:false,paths:['old.step']}:{state:'succeeded',result:{path:'old.step'}});
 await pending;check(f.messages.at(-1)==='Current view status'&&f.nodes.get('copy-text').value==='Current view fallback'&&f.nodes.get('copy-fallback').hidden&&!f.nodes.get('copy-fallback').open,'Delayed '+(failed?'error':'success')+' cannot overwrite '+change+' '+mode+' view');
 check(!f.control.exporting(),'Old export releases busy state without replay');
}
for(const mode of ['desktop','job']){
 const f=fixture(mode),pending=f.control.click();await turn();f.state.value.camera.zoom=2;
 f.response.resolve(mode==='desktop'?{cancelled:false,paths:['current.step']}:{state:'succeeded',result:{path:'current.step'}});await pending;
 check(f.messages.at(-1).startsWith(mode==='desktop'?'Saved:':'Export saved'),'Harmless camera motion retains valid '+mode+' export result');
 check(f.calls[0].args.document_id==='Part'&&f.calls[0].args.revision===3||f.calls[0].args.arguments?.document_id==='Part'&&f.calls[0].args.arguments.revision===3,'Export request retains captured native source');
}
{
 const f=fixture(),pending=f.control.click();f.response.resolve({state:'queued',job_id:'export_job'});await turn();check(f.timers.length===1,'Queued export uses bounded polling');f.retarget('artifact');f.timers.shift()();await pending;
 check(f.calls.length===1&&f.messages.at(-1)==='Current view status','Retarget during timer prevents later job polling and result publication');
}
{
 const f=fixture(),result=deferred(),pending=f.control.click();f.setNextJob(result.promise);f.response.resolve({state:'queued',job_id:'export_job'});await turn();f.timers.shift()();await turn();check(f.calls.length===2,'Native job read can be outstanding during a source change');f.retarget('artifact');result.resolve({state:'succeeded',result:{path:'old.step'}});await pending;
 check(f.messages.at(-1)==='Current view status'&&f.nodes.get('copy-text').value==='Current view fallback','A late outstanding job response cannot restore paths in an artifact review');
}
{
 const f=fixture('job','pdf'),pending=f.control.click();f.response.resolve({state:'succeeded',result:{artifacts:[{path:'view.pdf'},{path:'view.svg'}]}});await pending;
 check(f.calls[0].args.tool==='cad_drawing'&&f.nodes.get('copy-text').value==='view.pdf\nview.svg','Current qualified drawing export retains complete artifact paths');
}
{
 const f=fixture(),pending=f.control.click();f.response.resolve({state:'failed',job_id:'export_job',error:{message:'Native export failed'}});await pending;check(f.messages.at(-1)==='Native export failed','Current export failure remains visible');
}
{
 const f=fixture('job','3mf'),pending=f.control.click();f.response.resolve({state:'succeeded',result:{path:'plate-1.3mf'}});await pending;
 check(f.calls[0].args.tool==='cad_export'&&f.calls[0].args.arguments.format==='3mf'&&f.nodes.get('copy-text').value==='plate-1.3mf','3MF toolbar export uses the native exporter');
}
console.log('PASS '+checks+' export UI source-qualification checks');
