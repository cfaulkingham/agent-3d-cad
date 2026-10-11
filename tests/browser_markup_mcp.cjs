// Optional real Chromium + complete shipped app + native MCP evidence.
// NODE_PATH=<developer Playwright modules> node tests/browser_markup_mcp.cjs <service> <Chrome> [output]
const assert=require('node:assert/strict'),fs=require('node:fs'),path=require('node:path'),os=require('node:os'),crypto=require('node:crypto');
const {spawn}=require('node:child_process'),{createInterface}=require('node:readline'),{chromium}=require('playwright');
const executable=process.argv[2],chrome=process.argv[3],output=path.resolve(process.argv[4]||fs.mkdtempSync(path.join(os.tmpdir(),'cad-markup-browser-')));
if(!executable||!chrome)throw Error('Usage: browser_markup_mcp.cjs <native executable> <Chrome executable> [output]');
fs.mkdirSync(output,{recursive:true});const root=path.resolve(__dirname,'..');let checks=0;const check=(value,message)=>{assert.ok(value,message);checks++;console.log('PASS '+message);};
const evidence={checks:0,browser_errors:[],messages:[],viewer_origin:process.env.AGENTCAD_HARNESS_VIEWER==='embedded'?'compiled_mcp_resource':'workspace_source',executable_sha256:crypto.createHash('sha256').update(fs.readFileSync(path.resolve(executable))).digest('hex')};
const pause=ms=>new Promise(resolve=>setTimeout(resolve,ms));
(async()=>{
 const harness=spawn(process.execPath,[path.join(root,'tests/viewer_shell_harness/serve.mjs'),path.resolve(executable)],{stdio:['ignore','pipe','pipe']});let stderr='',browser,page;
 harness.stderr.on('data',text=>stderr+=text);
 const ready=new Promise((resolve,reject)=>{const timer=setTimeout(()=>reject(Error('Harness startup timed out: '+stderr)),60000);createInterface({input:harness.stdout}).on('line',line=>{try{const value=JSON.parse(line);if(value.url){clearTimeout(timer);resolve(value);}}catch{}});harness.once('exit',code=>{clearTimeout(timer);reject(Error('Harness exited '+code+': '+stderr));});});
 try{
  const {url,workspace}=await ready;
  const viewerHtml=await(await fetch(url+'viewer')).text();evidence.viewer_sha256=crypto.createHash('sha256').update(viewerHtml).digest('hex');
  const raw=async(name,args={})=>{const response=await(await fetch(url+'rpc',{method:'POST',body:JSON.stringify({method:'tools/call',params:{name,arguments:args}})})).json();if(response.error)throw Error(response.error.message);return response.result;};
  const tool=async(name,args={})=>{for(let attempt=0;;attempt++){const result=await raw(name,args);if(!result.isError)return result.structuredContent;if(result.structuredContent?.error?.code!=='workspace_busy'||attempt>=50)throw Error(JSON.stringify(result));await pause(20);}};
  await tool('cad_create',{document_id:'markup_box',model:{schema_version:1,units:'mm',parameters:{},features:[{id:'body',type:'box',size:[20,12,6]}],output:'body'}});
  const originalHead=fs.readFileSync(path.join(workspace,'documents/markup_box/HEAD.json'),'utf8');
  browser=await chromium.launch({headless:true,executablePath:chrome,args:['--use-gl=angle','--use-angle=swiftshader','--enable-unsafe-swiftshader']});
  page=await browser.newPage({viewport:{width:1300,height:950},deviceScaleFactor:1});page.on('pageerror',error=>evidence.browser_errors.push(error.message));
  // Instrument constructor references only: every input, state transition and
  // render still runs through the bundled app's real implementation.
  await page.addInitScript(()=>{
    globalThis.fixture={messages:[]};
    for(const name of ['CadMarkup','CadLiveState','CadRenderer']){
      let value;
      Object.defineProperty(globalThis,name,{configurable:true,get(){return value;},set(ctor){
        value=new Proxy(ctor,{construct(target,args,newTarget){const instance=Reflect.construct(target,args,newTarget);fixture[name]=instance;if(name==='CadMarkup'){fixture.markupTrace=[];for(const method of ['cancel','restore','setBinding']){const original=instance[method];instance[method]=function(...args){if(this.gesture)fixture.markupTrace.push({method,binding:this.current,next:args[0],stack:new Error().stack});return original.apply(this,args);};}}return instance;}});
      }});
    }
    addEventListener('message',event=>{if(event.data?.method==='ui/message')fixture.messages.push(event.data.params);});
  });
  // Grant the documented image capability only in this isolated fake host.
  // ui/message is recorded and acknowledged locally; no real chat is contacted.
  await page.route(url+'?**',async route=>{const response=await route.fetch(),html=(await response.text()).replace('hostCapabilities: { message: {},','hostCapabilities: { message: { image: {} },');await route.fulfill({response,body:html});});
  await page.goto(url+'?doc=markup_box&width=1100&height=780&view=markup_browser');const frame=()=>page.frames().find(f=>f.url().endsWith('/viewer'));
  await page.waitForFunction(()=>!!document.querySelector('iframe')?.contentWindow?.fixture?.CadRenderer?.ready,{},{timeout:30000});const app=frame();
  await app.waitForFunction(()=>fixture.CadLiveState.value.status==='ready'&&!fixture.CadLiveState.value.markup_unsaved);
  await app.evaluate(async()=>{await fixture.CadLiveState.contextQueue;await fixture.CadLiveState.whenGeometryReady();});
  check((await tool('cad_context',{view_id:'markup_browser'})).markup.status==='empty','Native fresh view starts with explicit empty markup');
  await app.locator('#dock-toggle').click();await app.locator('[data-tool="markup-panel"]').click();
  const bounds=await app.locator('#markup-overlay').boundingBox();check(bounds.width>500&&bounds.height>400,'Actual overlay occupies the model viewport');
  const pointer=async(kind,points,valid=true)=>{await app.locator(`[data-markup-tool="${kind}"]`).click();const before=await app.evaluate(()=>fixture.CadMarkup.context().item_count),p=points.map(([x,y])=>[bounds.x+x*bounds.width,bounds.y+y*bounds.height]);await page.mouse.move(...p[0]);await page.mouse.down();if(kind==='line'){await app.waitForFunction(()=>!fixture.CadLiveState.busy);await app.evaluate(()=>fixture.CadLiveState.pollOnce());check(await app.evaluate(()=>!!fixture.CadMarkup.gesture),'Native polling preserves the active pointer despite sorted JSON keys');}for(const point of p.slice(1))await page.mouse.move(...point,{steps:5});await page.mouse.up();await app.waitForFunction(()=>!fixture.CadLiveState.value.markup_unsaved);if(valid)assert.equal(await app.evaluate(()=>fixture.CadMarkup.context().item_count),before+1,kind+' gesture commits one local item');};
  await pointer('pen',[[.33,.3],[.37,.25],[.4,.3],[.44,.25]]);
  await app.locator('#markup-color').fill('#0055ee');await app.locator('#markup-width').fill('6');await app.locator('#markup-width').press('Tab');
  await pointer('line',[[.33,.34],[.47,.34]]);
  check(await app.evaluate(()=>{const line=fixture.CadMarkup.snapshot().items.at(-1);return line.color==='#0055ee'&&line.width===6;}),'Color and width inputs apply to actual drawn markup');
  await app.locator('#markup-color').fill('#e63946');await app.locator('#markup-width').fill('3');await app.locator('#markup-width').press('Tab');
  await pointer('arrow',[[.33,.39],[.47,.43]]);
  await pointer('rectangle',[[.33,.49],[.47,.59]]);
  await pointer('ellipse',[[.33,.64],[.47,.74]]);
  await app.locator('#markup-text').fill('Change this\nØ 4');await pointer('text',[[.33,.78]]);
  let context=await tool('cad_context',{view_id:'markup_browser'});check(context.markup.status==='current'&&context.markup.item_count===6,'All six actual mouse tools persist through native MCP');check(context.markup.scene.items.map(x=>x.kind).join(',')==='pen,line,arrow,rectangle,ellipse,text','Native context retains every drawn primitive and text label');
  for(const invalid of ['€'.repeat(171),Array(9).fill('line').join('\n')]){await app.locator('#markup-text').fill(invalid);await pointer('text',[[.5,.8]],false);check((await tool('cad_context',{view_id:'markup_browser'})).markup.item_count===6,'Invalid UTF-8 byte/line text cannot place the previous accepted label');}
  await app.locator('#markup-text').fill('Change this\nØ 4');
  const source=JSON.stringify(context.markup.scene.binding.source),scene=JSON.stringify(context.markup.scene);
  await app.waitForFunction(()=>!fixture.CadLiveState.busy);await app.evaluate(()=>fixture.CadLiveState.pollOnce());check(await app.locator('#markup-undo').isEnabled(),'Native polling preserves actual Undo history');
  await app.locator('#markup-undo').click();await app.waitForFunction(()=>!fixture.CadLiveState.value.markup_unsaved);check((await tool('cad_context',{view_id:'markup_browser'})).markup.item_count===5,'Undo changes persisted native markup');await app.locator('#markup-redo').click();await app.waitForFunction(()=>!fixture.CadLiveState.value.markup_unsaved);check((await tool('cad_context',{view_id:'markup_browser'})).markup.item_count===6,'Redo restores persisted native markup');
  const captured=await raw('cad_screenshot',{view_id:'markup_browser',timeout_ms:15000});check(!captured.isError,'Actual native screenshot request completes through shipped app');const image=captured.content.find(x=>x.type==='image');check(image?.mimeType==='image/png','Native screenshot returns a PNG image block');fs.writeFileSync(path.join(output,'marked-up.png'),Buffer.from(image.data,'base64'));
  const pixelEvidence=await app.evaluate(async url=>{const bitmap=new Image();bitmap.src=url;await bitmap.decode();const canvas=document.createElement('canvas');canvas.width=bitmap.width;canvas.height=bitmap.height;const ctx=canvas.getContext('2d');ctx.drawImage(bitmap,0,0);const pixels=ctx.getImageData(0,0,canvas.width,canvas.height).data;let red=0;for(let i=0;i<pixels.length;i+=4)if(pixels[i]>180&&pixels[i+1]<100&&pixels[i+2]<130)red++;return {red,width:canvas.width,height:canvas.height};},'data:image/png;base64,'+image.data);check(pixelEvidence.red>1000,'Independently decoded screenshot has thousands of real red markup pixels');evidence.pixels=pixelEvidence;
  const download=page.waitForEvent('download');await app.locator('#tool-popover-close').click();await app.locator('#export-menu-button').click();await app.locator('#save-image').click();const savedImage=await download;await savedImage.saveAs(path.join(output,'saved-markup.png'));check(savedImage.suggestedFilename()==='markup_box-r1.png','Save PNG downloads the same qualified viewport with source filename');if(await app.locator('#dock-toggle').getAttribute('aria-expanded')==='false')await app.locator('#dock-toggle').click();await app.locator('[data-tool="markup-panel"]').click();
  await app.locator('#markup-attach').click();await page.waitForFunction(()=>fixture.messages.length===1);const messages=await page.evaluate(()=>fixture.messages);check(messages[0].content.some(c=>c.type==='image')&&messages[0].content.some(c=>c.type==='text'&&c.text.includes('source_and_frame')),'Image-capable fake host receives composed image plus source-qualified markup context');evidence.messages=messages.map(message=>({...message,content:message.content.map(c=>c.type==='image'?{type:c.type,mimeType:c.mimeType,base64_bytes:c.data.length}:c)}));
  await app.locator('[data-markup-tool="navigate"]').click();await app.evaluate(()=>fixture.CadRenderer.setCamera({...fixture.CadRenderer.getCamera(),yaw:.4}));await app.waitForFunction(()=>fixture.CadMarkup.stale&&fixture.CadLiveState.cameraSaved===fixture.CadLiveState.cameraVersion);context=await tool('cad_context',{view_id:'markup_browser'});check(context.markup.status==='retired'&&JSON.stringify(context.markup.scene)===scene,'Changed camera retires markup without altering historical coordinates');
  await app.evaluate(()=>fixture.CadRenderer.setCamera(fixture.CadMarkup.snapshot().binding.frame.camera));await app.waitForFunction(()=>!fixture.CadMarkup.stale&&fixture.CadLiveState.cameraSaved===fixture.CadLiveState.cameraVersion);
  await page.locator('#widths button[data-width="900"]').click();await app.waitForFunction(()=>fixture.CadMarkup.stale&&!fixture.CadLiveState.value.markup_unsaved);context=await tool('cad_context',{view_id:'markup_browser'});check(context.markup.status==='retired','Changed viewport aspect also retires native agent context');
  await page.evaluate(()=>{settings.width=1100;resize();});await app.waitForFunction(()=>!fixture.CadMarkup.stale&&!fixture.CadLiveState.value.markup_unsaved);check((await tool('cad_context',{view_id:'markup_browser'})).markup.status==='current','Returning exact frame restores qualified screen coordinates');
  await page.screenshot({path:path.join(output,'app-markup.png')});
  // Reload the full app in the same workspace/view and ensure persistence.
  await app.goto(url+'viewer');await app.waitForFunction(()=>fixture.CadRenderer?.ready&&fixture.CadLiveState?.value.markup?.item_count===6,{},{timeout:30000});check(JSON.stringify((await tool('cad_context',{view_id:'markup_browser'})).markup.scene.binding.source)===source,'Full app reopen retains original source identity');
  await app.locator('#dock-toggle').click();await app.locator('[data-tool="markup-panel"]').click();await app.locator('#markup-clear').click();await app.waitForFunction(()=>!fixture.CadLiveState.value.markup_unsaved);check((await tool('cad_context',{view_id:'markup_browser'})).markup.status==='empty','Clear removes persisted markup through actual UI');
  check(fs.readFileSync(path.join(workspace,'documents/markup_box/HEAD.json'),'utf8')===originalHead,'Drawing, native screenshots, image delivery, camera/resize/reopen and clear preserve source HEAD');check(evidence.browser_errors.length===0,'Whole app completes without browser exceptions');evidence.checks=checks;evidence.context=context;evidence.capture={...captured.structuredContent,image:undefined};for(const name of ['failure.json','failure.png'])fs.rmSync(path.join(output,name),{force:true});fs.writeFileSync(path.join(output,'evidence.json'),JSON.stringify(evidence,null,2));console.log(`browser markup MCP: ${checks} checks passed; evidence ${output}`);
 }catch(error){evidence.checks=checks;evidence.stderr=stderr;if(page){await page.screenshot({path:path.join(output,'failure.png')}).catch(()=>{});evidence.frames=await Promise.all(page.frames().map(async frame=>({url:frame.url(),state:await frame.evaluate(()=>({title:document.title,error:document.getElementById('view-error')?.textContent,markup:document.getElementById('markup-status')?.textContent,ready:fixture?.CadRenderer?.ready,status:fixture?.CadLiveState?.value.status,markup_trace:fixture?.markupTrace,markup_scene:fixture?.CadMarkup?.snapshot(),context_error:fixture?.CadLiveState?.value.context_error,state_error:fixture?.CadLiveState?.value.error})).catch(e=>({error:e.message}))})));}throw error;
 }finally{await browser?.close();harness.kill('SIGTERM');}
})().catch(error=>{fs.writeFileSync(path.join(output,'failure.json'),JSON.stringify({...evidence,error:error.stack},null,2));console.error(error);process.exitCode=1;});
