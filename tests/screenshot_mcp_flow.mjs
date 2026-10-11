// Real native stdio + app capture coordinator. GPU pixels have a separate
// optional real-browser test; this fixture independently decodes returned PNGs.
import assert from 'node:assert/strict';
import {spawn} from 'node:child_process';
import {mkdtempSync,readFileSync,rmSync} from 'node:fs';
import {tmpdir} from 'node:os';
import {join,resolve} from 'node:path';
import {createInterface} from 'node:readline';
import {inflateSync} from 'node:zlib';
import vm from 'node:vm';
import {fileURLToPath} from 'node:url';
const root=fileURLToPath(new URL('../',import.meta.url)),workspace=mkdtempSync(join(tmpdir(),'cad-capture-flow-'));
for(const name of ['renderer.js','state.js'])vm.runInThisContext(readFileSync(join(root,'web',name),'utf8'),{filename:name});
const child=spawn(resolve(process.argv[2]),['serve','--workspace',workspace],{stdio:['pipe','pipe','pipe']});
const pending=new Map();let next=1,stderr='',checks=0;
const check=(v,m)=>{assert.ok(v,m);checks++;};
child.stderr.on('data',s=>stderr+=s);
const lines=createInterface({input:child.stdout});lines.on('line',line=>{const m=JSON.parse(line),p=pending.get(m.id);if(p){pending.delete(m.id);clearTimeout(p.timer);m.error?p.reject(Error(m.error.message)):p.resolve(m.result);}});
child.on('exit',code=>{for(const p of pending.values()){clearTimeout(p.timer);p.reject(Error(`Native exit ${code}: ${stderr}`));}pending.clear();});
function rpc(method,params={}){return new Promise((resolve,reject)=>{const id=next++,timer=setTimeout(()=>{pending.delete(id);reject(Error(`Timeout ${method}: ${stderr}`));},10000);pending.set(id,{resolve,reject,timer});child.stdin.write(JSON.stringify({jsonrpc:'2.0',id,method,params})+'\n');});}
const toolRaw=(name,args={})=>rpc('tools/call',{name,arguments:args});
async function tool(name,args={}){for(let attempt=0;;attempt++){const r=await toolRaw(name,args);if(!r.isError)return r.structuredContent;const e=Error(r.structuredContent.error.message);e.code=r.structuredContent.error.code;if(e.code!=='workspace_busy'||attempt===40)throw e;await new Promise(resolve=>setTimeout(resolve,25));}}
const pause=ms=>new Promise(r=>setTimeout(r,ms));
async function screenshotRaw(args){for(let attempt=0;;attempt++){const result=await toolRaw('cad_screenshot',args);if(!result.isError||result.structuredContent?.error?.code!=='workspace_busy'||attempt===40)return result;await pause(25);}}
async function sync(){for(let n=0;n<1000;n++){try{return await tool('cad_viewer',{action:'sync',view_id:'image'});}catch(e){if(e.code!=='workspace_busy')throw e;}await pause(5);}throw Error('Sync retry exhausted');}
async function request(){for(let n=0;n<1000;n++){const s=await sync();if(s.capture_requests?.length)return s.capture_requests[0];await pause(5);}throw Error('No image request');}
let state;
try{
 await rpc('initialize',{protocolVersion:'2025-11-25',capabilities:{},clientInfo:{name:'capture-flow',version:'1'}});child.stdin.write(JSON.stringify({jsonrpc:'2.0',method:'notifications/initialized'})+'\n');
 const list=await rpc('tools/list');check(list.tools.some(t=>t.name==='cad_screenshot'),'screenshot in actual MCP discovery');
 await tool('cad_create',{document_id:'plate',model:{schema_version:1,units:'mm',parameters:{},features:[{id:'base',type:'box',size:[20,10,6]}],output:'base'}});
 await tool('cad_open',{document_id:'plate',view_id:'image'});let shown;for(let n=0;n<1000;n++){shown=await sync();if(shown.state==='ready')break;await pause(5);}check(shown.state==='ready','native source ready');
 const bytes=readFileSync(join(root,'tests/image_fixtures/type-6.png')),data=bytes.toString('base64');
 const capture=screenshotRaw({view_id:'image',timeout_ms:3000});const req=await request();
 const pingStart=Date.now();await rpc('ping');check(Date.now()-pingStart<500,'MCP ping responds while screenshot waits');
 await tool('cad_viewer',{action:'capture',view_id:'image',evaluation_id:shown.evaluation_id,capture_id:req.capture_id,offset:0,data:data.slice(0,32),final:false});
 await tool('cad_viewer',{action:'capture',view_id:'image',evaluation_id:shown.evaluation_id,capture_id:req.capture_id,offset:32,data:data.slice(32),final:true});
 const result=await capture,image=result.content.find(c=>c.type==='image');check(!result.isError&&image.mimeType==='image/png','native MCP emits image content');
 check(!result.content[0].text.includes(data.slice(64,128)),'text content does not duplicate image bytes');
 const decoded=Buffer.from(image.data,'base64');assert.deepEqual(decoded,bytes);checks++;assert.deepEqual(readFileSync(result.structuredContent.path),bytes);checks++;
 let offset=8,compressed=[];while(offset<decoded.length){const n=decoded.readUInt32BE(offset),type=decoded.toString('ascii',offset+4,offset+8);if(type==='IDAT')compressed.push(decoded.subarray(offset+8,offset+8+n));offset+=12+n;}
 check(inflateSync(Buffer.concat(compressed)).length==65*(137*4+1),'independent zlib verifies real PNG stream');
 // Run the actual app controller/coordinator against native requests.
 const bridge={tool,updateContext:async()=>{}},controller=new CadLiveState(bridge);state=controller;controller.attach('image');for(let n=0;n<1000;n++){await controller.pollOnce();if(controller.value.status==='ready')break;await pause(5);}await controller.contextQueue;
 let rendered=0,textures=0,visible=true;const renderer={async whenTexturesReady(){textures++;},capture(){rendered++;return 'data:image/png;base64,'+data;}};
 const coordinator=new CadCapture(bridge,controller,()=>renderer,()=>visible);
 const second=screenshotRaw({view_id:'image',timeout_ms:3000});const req2=await request();coordinator.accept([req2,req2]);check(!(await second).isError&&rendered===1&&textures===1,'app settles textures, captures once and transfers native PNG');
 visible=false;const hidden=screenshotRaw({view_id:'image',timeout_ms:3000});coordinator.accept([await request()]);const failure=await hidden;check(failure.isError&&failure.structuredContent.error.code==='screenshot_unavailable'&&rendered===1,'hidden app never supplies a stale frame');
 visible=true;const stale=screenshotRaw({view_id:'image',timeout_ms:3000});const req3=await request();await tool('cad_viewer',{action:'context',view_id:'image',evaluation_id:shown.evaluation_id,selection:null,camera:{yaw:.1,pitch:.2,zoom:1,pan:[0,0]}});check((await stale).structuredContent.error.code==='stale_capture','persisted camera change rejects pinned frame');
 const timed=await screenshotRaw({view_id:'image',timeout_ms:30});check(timed.isError&&timed.structuredContent.error.code==='screenshot_timeout','disconnected app capture is bounded');
 check((await sync()).capture_requests.length===0,'failed/expired capture requests cleaned');
 console.log(`screenshot MCP/app flow: ${checks} checks passed`);
}finally{state?.dispose();child.stdin.end();await new Promise(r=>{if(child.exitCode!==null)return r();child.once('exit',r);});lines.close();rmSync(workspace,{recursive:true,force:true});}
