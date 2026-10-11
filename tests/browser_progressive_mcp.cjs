// Optional developer evidence: real native MCP, full shipped viewer and Chromium.
// Requires Playwright and Chrome only for this test; neither is a product dependency.
const fs=require('node:fs'),path=require('node:path'),assert=require('node:assert/strict');
const {spawn}=require('node:child_process'),{createInterface}=require('node:readline'),{chromium}=require('playwright');
const {createHash}=require('node:crypto');
const [exe,chrome,out]=process.argv.slice(2),root=path.resolve(__dirname,'..');
if(!exe||!chrome||!out)throw Error('Usage: browser_progressive_mcp.cjs <native executable> <Chrome executable> <evidence directory>');
fs.mkdirSync(out,{recursive:true});let checks=0;
const check=(value,message)=>{assert.ok(value,message);checks++;};
const pause=ms=>new Promise(resolve=>setTimeout(resolve,ms));
(async()=>{
 const executableSha256=createHash('sha256').update(fs.readFileSync(exe)).digest('hex');
 const harness=spawn(process.execPath,[path.join(__dirname,'viewer_shell_harness/serve.mjs'),path.resolve(exe)],{stdio:['ignore','pipe','pipe'],env:{...process.env,AGENTCAD_HARNESS_VIEWER:'embedded'}});
 let stderr='',browser,page;harness.stderr.on('data',data=>stderr+=data);
 const lines=createInterface({input:harness.stdout});
 try{
  const info=await new Promise((resolve,reject)=>{const timer=setTimeout(()=>reject(Error('Harness startup: '+stderr)),60000);lines.once('line',line=>{clearTimeout(timer);resolve(JSON.parse(line));});harness.once('exit',code=>{clearTimeout(timer);reject(Error('Harness exited '+code+': '+stderr));});});
  const calls=[],faults=[],rpc=async(name,args)=>{
   const response=await fetch(info.url+'rpc',{method:'POST',body:JSON.stringify({method:'tools/call',params:{name,arguments:args}})}),reply=await response.json();
   if(reply.error)throw Error(reply.error.message);if(reply.result.isError)throw Object.assign(Error(reply.result.structuredContent.error.message),reply.result.structuredContent.error);return reply.result.structuredContent;
  };
  const inputBytes=fs.readFileSync(path.join(root,'examples/articulated-gripper/gripper.create.json'));
  const input=JSON.parse(inputBytes),inputSha256=createHash('sha256').update(inputBytes).digest('hex');
  await rpc('cad_create',input);const before=await rpc('cad_read',{document_id:input.document_id});
  browser=await chromium.launch({headless:true,executablePath:chrome,args:['--use-gl=angle','--use-angle=swiftshader','--enable-unsafe-swiftshader']});
  page=await browser.newPage({viewport:{width:1320,height:960},deviceScaleFactor:1});page.on('pageerror',error=>faults.push(error.message));
  page.on('console',message=>{if(message.type()==='error')faults.push(message.text());});
  // Observability only: preserve the actual constructors, arguments and callbacks.
  await page.route('**/viewer',async route=>{
   const response=await route.fetch(),html=await response.text();
   const marker="(() => {\n  'use strict';\n  const $ = id => document.getElementById(id), bridge = new CadBridge();";
   const observe=`globalThis.progressiveEvidence={frames:[],cameras:[]};
const NativeState=CadLiveState;globalThis.CadLiveState=class extends NativeState{constructor(bridge,change){super(bridge,value=>{if(value.payload?.progressive){const p=value.payload;progressiveEvidence.frames.push({time:performance.now(),revision:p.revision,evaluation:p.evaluation_id,...p.progressive,levels:value.progressive?.levels,camera:value.camera});}change(value);});globalThis.workflowState=this;}};
const NativeRenderer=CadRenderer;globalThis.CadRenderer=class extends NativeRenderer{constructor(...args){super(...args);globalThis.workflowRenderer=this;}};
`;
   assert.ok(html.includes(marker),'Actual full app entry point exists');await route.fulfill({response,body:html.replace(marker,observe+marker)});
  });
  await page.route('**/rpc',async route=>{const request=route.request().postDataJSON();if(request.method==='tools/call'&&request.params.name==='cad_viewer'&&request.params.arguments.action==='component'){const args=request.params.arguments,record={...args,time:Date.now()};calls.push(record);if(args.offset===0)await pause(35);const response=await route.fetch(),body=await response.json();record.error=body.error||body.result?.structuredContent?.error||null;await route.fulfill({response});}else await route.continue();});
  await page.goto(info.url+'?doc='+input.document_id+'&view=progressive_gripper&width=1280&height=880');
  const frame=page.frames().find(item=>item.url().includes('/viewer'));assert.ok(frame);
  await frame.waitForFunction(()=>globalThis.workflowState?.value.status==='ready',null,{timeout:60000});
  let readinessError=null;
  try{await frame.evaluate(async()=>{await workflowState.whenGeometryReady();await workflowState.contextQueue;await new Promise(requestAnimationFrame);});}catch(error){readinessError=error;}
  const initial=await frame.evaluate(()=>({frames:progressiveEvidence.frames,p:workflowState.value.payload,status:workflowState.value.progressive,camera:workflowRenderer.getCamera(),fault:document.getElementById('view-error').hidden?null:document.getElementById('view-error').textContent}));
  fs.writeFileSync(path.join(out,'initial-state.json'),JSON.stringify(initial,null,2));
  fs.writeFileSync(path.join(out,'component-requests.json'),JSON.stringify(calls,null,2));
  await page.screenshot({path:path.join(out,'gripper-initial.png')});
  if(readinessError)throw readinessError;
  check(initial.p.components.occurrences.length===48,'Substantial gripper has all 48 leaf occurrences');
  check(initial.frames[0].loaded_occurrences>0&&initial.frames[0].loaded_occurrences<48,'First displayed geometry precedes the complete assembly');
  check(initial.p.progressive.loaded_occurrences===48,'All gripper occurrences eventually render');
  check(!initial.fault&&!initial.status.error&&!initial.status.omitted_occurrences,'Completed gripper display has no hidden load error or omitted geometry');
  check(initial.p.components.geometries.length<48,'Shared part sources are deduplicated');
  const manifests=await rpc('cad_query',{document_id:input.document_id,revision:1,kind:'component_manifest'});
  check(!manifests.mesh&&manifests.components.geometries.length===initial.p.components.geometries.length,'Native manifest is independently delivered without complete tessellation');
  const requests=new Set(calls.filter(c=>!c.cancel).map(c=>c.request_id));
  const unique=new Set(calls.filter(c=>!c.cancel).map(c=>c.geometry_id+':'+c.level));
  check(requests.size===unique.size,'Polling/chunks reuse one admitted request per source/detail level');
  await page.screenshot({path:path.join(out,'gripper-full-view.png')});
  const baseCamera=initial.camera,evaluation=initial.p.evaluation_id;
  // Use a flat source face, then verify its qualified selection across detail swaps.
  const picked=await frame.evaluate(()=>{const r=workflowRenderer,rect=r.canvas.getBoundingClientRect();for(let y=100;y<rect.height-100;y+=13)for(let x=360;x<rect.width-80;x+=13){const pick=CadRenderer.math.pick(r.model,r.camera,rect.width,rect.height,x,y,'face');if(pick?.id){const face=r.model.geometries.face.get(pick.id);if(face?.surface_kind==='plane')return{x,y,id:pick.id,part:face.part_id};}}return null;});
  if(picked){const bounds=await frame.locator('#viewport').boundingBox();await page.mouse.click(bounds.x+picked.x,bounds.y+picked.y);await frame.evaluate(()=>workflowState.contextQueue);}
  const selection=await frame.evaluate(()=>workflowState.value.selection?.reference??null);
  check(!!selection,'A real pointer selects qualified model geometry');
  const levelsBefore=initial.status.levels;
  await frame.evaluate(async camera=>{workflowRenderer.setCamera({...camera,zoom:camera.zoom*12});await workflowState.whenGeometryReady();await new Promise(requestAnimationFrame);},baseCamera);
  const fine=await frame.evaluate(()=>({p:workflowState.value.payload,status:workflowState.value.progressive,selection:workflowState.value.selection?.reference,camera:workflowRenderer.getCamera()}));
  check(fine.p.evaluation_id===evaluation,'Adaptive detail retains exact evaluation identity');
  check(Object.entries(fine.status.levels).some(([id,level])=>level>(levelsBefore[id]??0)),'Zooming requests a finer real native component mesh');
  check(JSON.stringify(fine.selection)===JSON.stringify(selection),'Qualified selection survives adaptive mesh replacement');
  await frame.evaluate(async camera=>{workflowRenderer.setCamera({...camera,zoom:camera.zoom*.08});await workflowState.whenGeometryReady();await new Promise(requestAnimationFrame);},baseCamera);
  const coarse=await frame.evaluate(()=>({status:workflowState.value.progressive,evaluation:workflowState.value.payload.evaluation_id}));
  check(coarse.evaluation===evaluation&&Object.values(coarse.status.levels).every(level=>level===0),'Zooming out returns to coarse component detail');
  check(coarse.status.cache_bytes<=coarse.status.budget_bytes,'Resident component cache stays inside its declared budget');
  await frame.evaluate(async camera=>{workflowRenderer.setCamera(camera);await workflowState.whenGeometryReady();await new Promise(requestAnimationFrame);},baseCamera);
  const restored=await frame.evaluate(()=>workflowRenderer.getCamera());
  check(JSON.stringify(restored)===JSON.stringify(baseCamera),'Detail refinement leaves user camera unchanged');
  await frame.locator('#dock-toggle').click();await frame.locator('[data-tool="markup-panel"]').click();
  await frame.locator('[data-markup-tool="arrow"]').click();
  const overlay=await frame.locator('#markup-overlay').boundingBox();
  await page.mouse.move(overlay.x+overlay.width*.7,overlay.y+overlay.height*.2);await page.mouse.down();
  await page.mouse.move(overlay.x+overlay.width*.58,overlay.y+overlay.height*.35,{steps:5});await page.mouse.up();
  await frame.waitForFunction(()=>workflowState.value.markup?.item_count===1&&!workflowState.value.markup_unsaved);
  await frame.evaluate(()=>workflowState.contextQueue);
  const markup=await rpc('cad_context',{view_id:'progressive_gripper'});
  check(markup.markup.status==='current'&&markup.markup.scene.binding.source.evaluation_id===evaluation,'Real pointer markup on the gripper retains the progressive source evaluation');
  const captureReply=await(await fetch(info.url+'rpc',{method:'POST',body:JSON.stringify({method:'tools/call',params:{name:'cad_screenshot',arguments:{view_id:'progressive_gripper',timeout_ms:15000}}})})).json();
  const captureImage=captureReply.result?.content?.find(item=>item.type==='image');
  check(!captureReply.error&&!captureReply.result?.isError&&captureImage?.mimeType==='image/png','Native screenshot captures the loaded gripper with its qualified viewport markup');
  fs.writeFileSync(path.join(out,'gripper-markup.png'),Buffer.from(captureImage.data,'base64'));
  const original=await rpc('cad_read',{document_id:input.document_id});
  check(JSON.stringify(original)===JSON.stringify(before),'All viewing, picking and LOD work preserves the committed document');
  await frame.evaluate(async()=>{await workflowState.publishContext();});
  check(faults.length===0,'Actual full viewer has no browser exceptions or console errors');
  fs.writeFileSync(path.join(out,'progressive-browser.json'),JSON.stringify({checks,viewer:'native_mcp_resource',executable_sha256:executableSha256,input_sha256:inputSha256,first_frame:initial.frames[0],complete:initial.status,fine:fine.status,coarse:coarse.status,selection,markup:markup.markup,requests:calls,browser_errors:faults},null,2));
  console.log(`${checks} real browser/native progressive checks passed`);
 }finally{await browser?.close();lines.close();harness.kill('SIGTERM');await pause(50);}
})().catch(error=>{console.error(error);process.exitCode=1;});
