// Actual markup controller with deterministic pointer and canvas boundaries.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import vm from 'node:vm';
const environment={TextEncoder,console,devicePixelRatio:2};vm.runInNewContext(fs.readFileSync(new URL('../web/markup.js',import.meta.url),'utf8'),environment);
const Markup=environment.CadMarkup,plain=value=>JSON.parse(JSON.stringify(value));let checks=0;
const test=(name,fn)=>{fn();checks++;console.log('PASS '+name);};
const binding={source:{view_id:'view',document_id:'plate',revision:1,evaluation_id:'eval_1',feature_id:'plate',review_sha256:null,draft:false},frame:{camera:{yaw:0,pitch:0,zoom:1,pan:[0,0]},hidden_part_ids:[],presentation:null,appearance:null,render:null,artifact_pose:null,section:null,annotations:[]},viewport:[640,480]};
const mark=(kind='arrow',id='one')=>({id,kind,color:'#e63946',width:3,points:kind==='pen'?[[.1,.2]]:kind==='text'?[[.2,.3]]:[[.1,.2],[.4,.6]],...(kind==='text'?{text:'Change this\nØ 4',font_size:20}:{})});
const scene=(items=[mark()])=>({schema_version:1,binding:structuredClone(binding),items});
function fixture(){const listeners={},painted=[],errors=[],changes=[],modes=[],capture=new Set();const ctx=new Proxy({},{get:(_,name)=>typeof name==='string'?((...args)=>painted.push([name,...args])):undefined,set:(_,name,value)=>{painted.push([name,value]);return true;}});
 const canvas={width:640,height:480,style:{},getContext:()=>ctx,addEventListener:(type,fn)=>listeners[type]=fn,removeEventListener:type=>delete listeners[type],getBoundingClientRect:()=>({left:10,top:20,width:640,height:480}),setAttribute(){},setPointerCapture:id=>capture.add(id),hasPointerCapture:id=>capture.has(id),releasePointerCapture:id=>capture.delete(id),focus(){}};
 const controller=new Markup(canvas,{onChange:v=>changes.push(plain(v)),onError:e=>errors.push(e.message),onModeChange:v=>modes.push(v)});controller.setBinding(binding);
 const event=(type,x,y,extra={})=>listeners[type]?.({button:0,isPrimary:true,pointerId:1,clientX:x+10,clientY:y+20,preventDefault(){},stopPropagation(){},...extra});return {controller,canvas,ctx,event,painted,errors,changes,modes,listeners,capture};
}
test('closed scene validation preserves data and rejects invalid sources, points and styles',()=>{
 assert.deepEqual(plain(Markup.validateScene(scene())),scene());for(const change of [s=>s.bad=true,s=>s.binding.source.evaluation_id='bad id',s=>s.binding.source.revision=0,s=>s.binding.viewport[0]=0,s=>s.binding.frame.camera.zoom=NaN,s=>s.items[0].points[1][0]=1.01,s=>s.items[0].color='url(http://host)',s=>s.items[0].width=17,s=>s.items[0].script='code',s=>s.items.push({...s.items[0]})]){const s=scene();change(s);assert.throws(()=>Markup.validateScene(s));}
 const readOnly=scene();Object.assign(readOnly.binding.source,{document_id:null,revision:null,feature_id:null,review_sha256:'a'.repeat(64)});assert.doesNotThrow(()=>Markup.validateScene(readOnly));readOnly.binding.source.draft=true;assert.throws(()=>Markup.validateScene(readOnly));
});
test('UTF-8 text and total point limits reject atomically',()=>{
 for(const text of ['', ' ', '\u0000', 'a\n'.repeat(8), '€'.repeat(171)])assert.throws(()=>Markup.validateScene(scene([{...mark('text'),text}])));
 assert.doesNotThrow(()=>Markup.validateScene(scene([{...mark('text'),text:'€'.repeat(170)}])));
 assert.throws(()=>Markup.validateScene(scene(Array.from({length:257},(_,i)=>mark('arrow','m'+i)))));
 assert.throws(()=>Markup.validateScene(scene([{...mark('pen'),points:Array.from({length:2049},()=>[0,0])}])));
 assert.throws(()=>Markup.validateScene(scene(Array.from({length:5},(_,i)=>({...mark('pen','m'+i),points:Array.from({length:2048},()=>[0,0])})))));
 const f=fixture();f.controller.restore(scene());assert.throws(()=>f.controller.add({...mark('pen','large'),points:Array.from({length:2049},()=>[0,0])}));assert.equal(f.controller.snapshot().items.length,1);
});
test('pointer strokes use viewport coordinates, coalesce tiny moves and support undo/redo',()=>{
 const f=fixture();f.controller.setTool('pen');f.event('pointerdown',64,96);f.event('pointermove',64.1,96.1);f.event('pointermove',128,144);f.event('pointerup',192,192);assert.equal(f.capture.size,0);assert.deepEqual(plain(f.controller.snapshot().items[0].points),[[.1,.2],[.2,.3],[.3,.4]]);assert.equal(f.controller.context().item_count,1);
 assert.ok(f.controller.undo());assert.equal(f.controller.snapshot(),null);assert.ok(f.controller.redo());assert.equal(f.controller.snapshot().items.length,1);f.controller.undo();f.controller.add(mark('rectangle'));assert.equal(f.controller.redo(),false);assert.equal(f.controller.snapshot().items[0].kind,'rectangle');
});
test('source, camera, display or aspect changes retire marks without rebinding history',()=>{
 const f=fixture();f.controller.restore(scene());const original=f.controller.snapshot();for(const change of [v=>v.source.evaluation_id='eval_2',v=>v.frame.camera.yaw=.1,v=>v.frame.hidden_part_ids=['part'],v=>v.frame.presentation={clipping:{enabled:true}},v=>v.viewport[0]=700]){const value=structuredClone(binding);change(value);f.controller.setBinding(value);assert.equal(f.controller.context().status,'retired');assert.throws(()=>f.controller.setTool('pen'));f.painted.length=0;f.controller.paint(f.ctx,640,480);assert.equal(f.painted.length,0);assert.deepEqual(plain(f.controller.snapshot()),plain(original));f.controller.setBinding(binding);assert.equal(f.controller.context().status,'current');}
 const scaled=structuredClone(binding);scaled.viewport=[1280,960];f.controller.setBinding(scaled);assert.equal(f.controller.context().status,'current');f.controller.clear();assert.equal(f.controller.snapshot(),null);f.controller.undo();assert.equal(f.controller.context().status,'current');
});
test('pointer cancellation, loss, source transition and secondary touches cannot save partial marks',()=>{
 const f=fixture();f.controller.setTool('arrow');f.event('pointerdown',20,30,{isPrimary:false});assert.equal(f.controller.gesture,null);f.event('pointerdown',20,30);f.event('pointermove',200,200);f.event('pointercancel',200,200);assert.equal(f.controller.snapshot(),null);f.event('pointerdown',20,30);f.event('lostpointercapture',200,200);assert.equal(f.controller.snapshot(),null);f.event('pointerdown',20,30);const next=structuredClone(binding);next.source.evaluation_id='eval_2';f.controller.setBinding(next);f.event('pointerup',200,200);assert.equal(f.controller.snapshot(),null);assert.equal(f.capture.size,0);
});
test('all tools paint their visible primitives and text is plain canvas text',()=>{
 const f=fixture();f.controller.restore(scene(['pen','line','arrow','rectangle','ellipse','text'].map((kind,i)=>mark(kind,'mark_'+i))));f.painted.length=0;f.controller.paint(f.ctx,640,480);for(const name of ['arc','lineTo','strokeRect','ellipse','fillText'])assert.ok(f.painted.some(operation=>operation[0]===name),name);assert.ok(f.painted.some(operation=>operation[0]==='fillText'&&operation[1]==='Ø 4'));
 f.controller.setTool('text');f.controller.setText('<img src=x onerror=alert(1)>');f.event('pointerdown',100,100);f.event('pointerup',100,100);assert.ok(f.painted.some(operation=>operation[0]==='fillText'&&operation[1].startsWith('<img')));assert.equal(f.errors.length,0);
});
test('capture refuses unfinished gestures, bounded history stays bounded and destroy detaches input',()=>{
 const f=fixture();f.controller.setTool('pen');f.event('pointerdown',20,20);assert.throws(()=>f.controller.paint(f.ctx,640,480));f.controller.cancel();for(let i=0;i<40;i++)f.controller.add(mark('arrow','m'+i));assert.equal(f.controller.past.length,32);f.controller.destroy();assert.equal(Object.keys(f.listeners).length,0);assert.equal(f.canvas.style.pointerEvents,'none');
});
test('current controller snapshots cannot be mutated through caller-owned data',()=>{
 const f=fixture(),source=scene();f.controller.restore(source);source.items[0].points[0][0]=1;const snapshot=f.controller.snapshot();snapshot.items.length=0;assert.equal(f.controller.snapshot().items.length,1);assert.equal(f.controller.snapshot().items[0].points[0][0],.1);
});
const sorted=value=>Array.isArray(value)?value.map(sorted):value&&typeof value==='object'?Object.fromEntries(Object.keys(value).sort().map(key=>[key,sorted(value[key])])):value;
test('same saved scene with native key ordering preserves history and active pointer gestures',()=>{
 const f=fixture();f.controller.add(mark('pen'));const snapshot=f.controller.snapshot(),native=sorted(snapshot);assert.notEqual(JSON.stringify(native),JSON.stringify(snapshot));assert.ok(Markup.sameScene(native,snapshot));
 f.controller.setTool('line');f.event('pointerdown',20,30);f.controller.restore(native);assert.ok(f.controller.gesture);assert.equal(f.controller.past.length,1);f.event('pointerup',100,100);assert.equal(f.controller.snapshot().items.length,2);f.controller.restore(sorted(f.controller.snapshot()));f.controller.undo();assert.equal(f.controller.snapshot().items.length,1);assert.ok(f.controller.redo());
});
test('large CSS viewports keep overlay allocation within four megapixels and 4096 dimensions',()=>{
 const f=fixture();for(const [width,height] of [[10000,10000],[10000,100],[100,10000]]){f.canvas.getBoundingClientRect=()=>({left:0,top:0,width,height});f.controller.resize();assert.ok(f.canvas.width<=4096&&f.canvas.height<=4096&&f.canvas.width*f.canvas.height<=4000000);}
});
for(const name of ['renderer.js','progressive.js','state.js'])vm.runInNewContext(fs.readFileSync(new URL('../web/'+name,import.meta.url),'utf8'),environment);
const asyncTest=async(name,fn)=>{await fn();checks++;console.log('PASS '+name);};
function stateFixture(){const calls=[],bridge={capabilities:{},async tool(name,args){calls.push({name,args:plain(args)});return {};}};
 const state=new environment.CadLiveState(bridge);Object.assign(state.value,{view_id:'view',status:'ready',payload:{document_id:'plate',revision:1,evaluation_id:'eval_1',feature_id:'plate',draft:false},camera:structuredClone(binding.frame.camera),viewport:[640,480]});
 const value=scene();value.binding=plain(Markup.bind(state.value,state.value.camera,640,480));return {state,bridge,calls,scene:value};
}
await asyncTest('state saves exact markup and viewport, updates context qualification and retires camera/aspect changes',async()=>{
 const f=stateFixture();await f.state.setMarkup(f.scene);assert.equal(f.state.value.markup_unsaved,false);assert.deepEqual(f.calls.at(-1).args.markup,f.scene);assert.deepEqual(f.calls.at(-1).args.viewport,[640,480]);assert.equal(f.state.snapshot().markup.status,'current');
 f.state.setCamera({...f.state.value.camera,yaw:.5});assert.equal(f.state.snapshot().markup.status,'retired');await f.state.publishContext();assert.deepEqual(f.calls.at(-1).args.markup,f.scene);
 f.state.setCamera(binding.frame.camera);await f.state.setMarkupViewport([800,480]);assert.equal(f.state.snapshot().markup.status,'retired');await f.state.setMarkupViewport([1280,960]);assert.equal(f.state.snapshot().markup.status,'current');
});
await asyncTest('uncertain write acknowledgment preserves unsaved scene and does not automatically replay mutation',async()=>{
 const f=stateFixture();f.bridge.tool=async()=>{throw Error('Lost acknowledgment');};await assert.rejects(f.state.setMarkup(f.scene),/Lost acknowledgment/);assert.equal(f.state.value.markup_unsaved,true);assert.equal(f.state.value.markup_error,'Lost acknowledgment');assert.deepEqual(plain(f.state.value.markup.scene),f.scene);
});
await asyncTest('native key ordering does not create markup writes or invalidate captured state',async()=>{
 const f=stateFixture();await f.state.setMarkup(f.scene);const snapshot=f.state.snapshot(),calls=f.calls.length,version=f.state.markupVersion;f.state.acceptMarkup(sorted(f.state.value.markup));await f.state.setMarkup(sorted(f.scene));assert.equal(f.state.markupVersion,version);assert.equal(f.calls.length,calls);assert.ok(f.state.matches(snapshot));
});
await asyncTest('markup edits during asynchronous texture settlement invalidate screenshot before image bytes',async()=>{
 const f=stateFixture();await f.state.setMarkup(f.scene);let resume,entered;const enteredWait=new Promise(resolve=>entered=resolve),textureWait=new Promise(resolve=>resume=resolve);let captures=0;
 const renderer={async whenTexturesReady(){entered();await textureWait;},capture(){captures++;return 'data:image/png;base64,AAAA';}};
 const coordinator=new environment.CadCapture(f.bridge,f.state,()=>renderer),capture=coordinator.capture({capture_id:'capture_one',source:{view_id:'view',evaluation_id:'eval_1'},max_bytes:2097152});await enteredWait;
 const changed=structuredClone(f.scene);changed.items[0].points[1]=[.8,.8];await f.state.setMarkup(changed);resume();await capture;assert.equal(captures,0);assert.ok(f.calls.some(call=>call.args.action==='capture'&&call.args.error.includes('changed')));assert.ok(!f.calls.some(call=>call.args.action==='capture'&&call.args.data));
});
await asyncTest('captured image delivery requires its tracked frame and rejects changes before and during context publication',async()=>{
 for(const change of [f=>f.state.setCamera({...f.state.value.camera,yaw:.5}),f=>{f.state.value.payload={...f.state.value.payload,evaluation_id:'eval_2'};},f=>{f.state.markupVersion++;},f=>{f.state.value.selection={reference:{entity_id:'face-1'}};},f=>{f.state.value.markup_drawing=true;}]){
  for(const during of [false,true]){
   const f=stateFixture(),messages=[];f.bridge.capabilities.message={image:{}};f.bridge.request=async(...args)=>messages.push(args);const snapshot=f.state.snapshot();
   let release,entered;const started=new Promise(resolve=>entered=resolve),gate=new Promise(resolve=>release=resolve);
   if(during)f.bridge.tool=async()=>{entered();await gate;return {};};else change(f);
   const sent=f.state.sendPrompt('Review these marks','data:image/png;base64,AAAA',snapshot);
   if(during){await started;change(f);release();}
   await assert.rejects(sent,/changed/);assert.equal(messages.length,0);
  }
 }
 const f=stateFixture(),messages=[];f.bridge.capabilities.message={image:{}};f.bridge.request=async(...args)=>messages.push(args);
 const snapshot=f.state.snapshot();await assert.rejects(f.state.sendPrompt('Review','data:image/png;base64,AAAA',structuredClone(snapshot)),/changed/);assert.equal(messages.length,0);
 await f.state.sendPrompt('Review this frame','data:image/png;base64,AAAA',snapshot);assert.equal(messages.length,1);assert.match(messages[0][1].content[0].text,/Review this frame/);assert.equal(messages[0][1].content[1].data,'AAAA');
 await f.state.sendPrompt('Text only remains supported');assert.equal(messages.length,2);assert.equal(messages[1][1].content.length,1);
});
await asyncTest('render-only animated camera changes during texture wait reject native PNG capture',async()=>{
 const f=stateFixture();let resume,entered,captures=0;const started=new Promise(resolve=>entered=resolve),gate=new Promise(resolve=>resume=resolve),camera=structuredClone(f.state.value.camera);
 const renderer={getCamera:()=>camera,async whenTexturesReady(){entered();await gate;},capture(){captures++;return 'data:image/png;base64,AAAA';}};
 const coordinator=new environment.CadCapture(f.bridge,f.state,()=>renderer),capture=coordinator.capture({capture_id:'capture_camera',source:{view_id:'view',evaluation_id:'eval_1'},max_bytes:2097152});await started;camera.yaw=.25;resume();await capture;assert.equal(captures,0);assert.ok(f.calls.some(call=>call.args.action==='capture'&&call.args.error.includes('changed')));assert.ok(!f.calls.some(call=>call.args.action==='capture'&&call.args.data));
});
console.log(`markup UI: ${checks} scenario groups passed`);
