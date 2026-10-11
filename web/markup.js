/* Source- and frame-qualified viewport review markup. Plain data, never CAD geometry. */
(() => {
  'use strict';
  const LIMITS=Object.freeze({items:256,points:8192,strokePoints:2048,history:32,textBytes:512,sceneBytes:512*1024,frameBytes:64*1024});
  const TOOLS=Object.freeze(['navigate','pen','line','arrow','rectangle','ellipse','text']);
  const clone=value=>JSON.parse(JSON.stringify(value));
  const fail=message=>{throw Error(message);};
  const object=value=>!!value&&typeof value==='object'&&!Array.isArray(value);
  const keys=(value,required)=>{if(!object(value)||Object.keys(value).length!==required.length||required.some(key=>!Object.hasOwn(value,key)))fail('Invalid markup fields.');};
  const finite=(value,min,max)=>typeof value==='number'&&Number.isFinite(value)&&value>=min&&value<=max;
  const bytes=value=>new TextEncoder().encode(value).length;
  const id=value=>typeof value==='string'&&/^[A-Za-z][A-Za-z0-9_-]{0,63}$/.test(value);
  const point=value=>Array.isArray(value)&&value.length===2&&value.every(v=>finite(v,0,1));
  const canonical=value=>Array.isArray(value)?value.map(canonical):object(value)?Object.fromEntries(Object.keys(value).sort().map(key=>[key,canonical(value[key])])):value;
  const equal=(a,b)=>JSON.stringify(canonical(a))===JSON.stringify(canonical(b));
  function plain(value,depth=0){
    if(depth>16)fail('Markup frame nesting exceeds 16 levels.');
    if(value===null||typeof value==='boolean')return;
    if(typeof value==='number'){if(!Number.isFinite(value))fail('Markup frame has a non-finite value.');return;}
    if(typeof value==='string'){if(value.length>65536)fail('Markup frame text is too large.');return;}
    if(Array.isArray(value)){if(value.length>8192)fail('Markup frame array is too large.');for(const entry of value)plain(entry,depth+1);return;}
    if(object(value)){const entries=Object.entries(value);if(entries.length>128)fail('Markup frame has too many fields.');for(const [key,entry] of entries){if(key==='__proto__'||key==='constructor'||key==='prototype')fail('Invalid markup frame field.');plain(entry,depth+1);}return;}
    fail('Markup frame must contain plain JSON.');
  }
  function binding(value){
    keys(value,['source','frame','viewport']);const source=value.source;
    keys(source,['view_id','document_id','revision','evaluation_id','feature_id','review_sha256','draft']);
    if(!id(source.view_id)||!id(source.evaluation_id)||typeof source.draft!=='boolean')fail('Invalid markup source identity.');
    if(source.review_sha256===null){if(!id(source.document_id)||!id(source.feature_id)||!Number.isSafeInteger(source.revision)||source.revision<1)fail('Markup requires a qualified native source.');}
    else if(typeof source.review_sha256!=='string'||!/^[a-f0-9]{64}$/.test(source.review_sha256)||source.document_id!==null||source.revision!==null||source.feature_id!==null||source.draft)fail('Markup requires a qualified artifact source.');
    if(!Array.isArray(value.viewport)||value.viewport.length!==2||!value.viewport.every(v=>finite(v,1,10000)))fail('Invalid markup viewport.');
    keys(value.frame,['camera','hidden_part_ids','presentation','appearance','render','artifact_pose','section','annotations']);
    const camera=value.frame.camera;keys(camera,['yaw','pitch','zoom','pan']);
    if(!finite(camera.yaw,-1e9,1e9)||!finite(camera.pitch,-Math.PI/2,Math.PI/2)||!finite(camera.zoom,.000001,1e9)||!Array.isArray(camera.pan)||camera.pan.length!==2||!camera.pan.every(v=>finite(v,-1e9,1e9)))fail('Invalid markup camera.');
    if(!Array.isArray(value.frame.hidden_part_ids)||value.frame.hidden_part_ids.length>1024||value.frame.hidden_part_ids.some(v=>typeof v!=='string'||v.length>512)||new Set(value.frame.hidden_part_ids).size!==value.frame.hidden_part_ids.length)fail('Invalid markup visibility.');
    for(const key of ['presentation','appearance','render','artifact_pose','section'])if(value.frame[key]!==null&&!object(value.frame[key]))fail('Invalid markup frame settings.');
    if(!Array.isArray(value.frame.annotations)||value.frame.annotations.length>32||value.frame.annotations.some(note=>!object(note)))fail('Invalid markup frame notes.');
    plain(value.frame);if(bytes(JSON.stringify(value.frame))>LIMITS.frameBytes)fail('Markup frame exceeds 64 KiB.');return clone(value);
  }
  function item(value){
    if(!object(value)||!TOOLS.includes(value.kind)||value.kind==='navigate')fail('Unknown markup tool.');
    keys(value,value.kind==='text'?['id','kind','color','width','points','text','font_size']:['id','kind','color','width','points']);
    if(!id(value.id)||typeof value.color!=='string'||!/^#[a-fA-F0-9]{6}$/.test(value.color)||!finite(value.width,1,16))fail('Invalid markup style.');
    const count=value.kind==='pen'?LIMITS.strokePoints:value.kind==='text'?1:2;
    if(!Array.isArray(value.points)||value.points.length<1||value.points.length>count||value.points.some(p=>!point(p))||(value.kind!=='pen'&&value.points.length!==count))fail('Invalid markup coordinates.');
    if(value.kind==='text'&&(typeof value.text!=='string'||!value.text.trim()||bytes(value.text)>LIMITS.textBytes||/[\x00-\x08\x0b\x0c\x0e-\x1f\x7f]/.test(value.text)||value.text.split('\n').length>8||!finite(value.font_size,10,48)))fail('Markup text must contain 1–512 UTF-8 bytes and at most eight lines.');
    return clone(value);
  }
  function scene(value){
    keys(value,['schema_version','binding','items']);if(value.schema_version!==1)fail('Unsupported markup version.');binding(value.binding);
    if(!Array.isArray(value.items)||value.items.length>LIMITS.items)fail('Markup exceeds 256 items.');const ids=new Set();let count=0;
    for(const entry of value.items){item(entry);if(ids.has(entry.id))fail('Duplicate markup item.');ids.add(entry.id);count+=entry.points.length;}
    if(count>LIMITS.points||bytes(JSON.stringify(value))>LIMITS.sceneBytes)fail('Markup scene exceeds its point or byte budget.');return clone(value);
  }
  function matches(a,b){return !!a&&!!b&&equal(a.source,b.source)&&equal(a.frame,b.frame)&&Math.abs(a.viewport[0]/a.viewport[1]-b.viewport[0]/b.viewport[1])<1e-6;}
  function paint(ctx,value,width,height){
    if(!value)return;scene(value);if(!finite(width,1,10000)||!finite(height,1,10000))fail('Invalid markup paint dimensions.');
    const scale=Math.min(width,height)/Math.min(...value.binding.viewport);ctx.save();ctx.beginPath();ctx.rect(0,0,width,height);ctx.clip();ctx.lineCap='round';ctx.lineJoin='round';
    for(const mark of value.items){
      const points=mark.points.map(p=>[p[0]*width,p[1]*height]),a=points[0],b=points[1];ctx.strokeStyle=mark.color;ctx.fillStyle=mark.color;ctx.lineWidth=mark.width*scale;ctx.beginPath();
      if(mark.kind==='text'){ctx.font=`${mark.font_size*scale}px sans-serif`;ctx.textAlign='left';ctx.textBaseline='top';mark.text.split('\n').forEach((line,i)=>ctx.fillText(line,a[0],a[1]+i*mark.font_size*1.25*scale));}
      else if(mark.kind==='rectangle')ctx.strokeRect(Math.min(a[0],b[0]),Math.min(a[1],b[1]),Math.abs(a[0]-b[0]),Math.abs(a[1]-b[1]));
      else if(mark.kind==='ellipse'){ctx.ellipse((a[0]+b[0])/2,(a[1]+b[1])/2,Math.abs(a[0]-b[0])/2,Math.abs(a[1]-b[1])/2,0,0,Math.PI*2);ctx.stroke();}
      else if(mark.kind==='pen'&&points.length===1){ctx.arc(a[0],a[1],ctx.lineWidth/2,0,Math.PI*2);ctx.fill();}
      else{ctx.moveTo(...a);for(const p of points.slice(1))ctx.lineTo(...p);ctx.stroke();if(mark.kind==='arrow'){
        const angle=Math.atan2(b[1]-a[1],b[0]-a[0]),length=Math.min(Math.hypot(b[0]-a[0],b[1]-a[1])*.45,Math.max(12,mark.width*4)*scale);
        ctx.beginPath();ctx.moveTo(b[0]-length*Math.cos(angle-.45),b[1]-length*Math.sin(angle-.45));ctx.lineTo(...b);ctx.lineTo(b[0]-length*Math.cos(angle+.45),b[1]-length*Math.sin(angle+.45));ctx.stroke();
      }}
    }ctx.restore();
  }
  class CadMarkup {
    constructor(canvas,{onChange=()=>{},onError=()=>{},onModeChange=()=>{},onGestureChange=()=>{}}={}){
      if(!canvas?.getContext)fail('Markup requires a canvas.');this.canvas=canvas;this.ctx=canvas.getContext('2d');if(!this.ctx)fail('Markup canvas is unavailable.');
      this.callbacks={onChange,onError,onModeChange,onGestureChange};this.current=null;this.scene=null;this.past=[];this.future=[];this.tool='navigate';this.color='#e63946';this.width=3;this.fontSize=20;this.text='';this.sequence=0;this.gesture=null;this.listeners=[];this.destroyed=false;
      this.listen('pointerdown',event=>this.start(event));this.listen('pointermove',event=>this.move(event));this.listen('pointerup',event=>this.end(event));
      this.listen('pointercancel',()=>this.cancel());this.listen('lostpointercapture',()=>this.cancel());
      this.listen('keydown',event=>{if(event.key==='Escape'){event.preventDefault();this.cancel();this.setTool('navigate');}else if((event.ctrlKey||event.metaKey)&&event.key.toLowerCase()==='z'){event.preventDefault();event.shiftKey?this.redo():this.undo();}});
      this.listen('wheel',event=>{if(this.tool!=='navigate'){event.preventDefault();event.stopPropagation();}},{passive:false});this.mode();
    }
    listen(type,callback,options){const listener=event=>{try{callback(event);}catch(error){this.cancel();this.callbacks.onError(error);}};this.canvas.addEventListener(type,listener,options);this.listeners.push(()=>this.canvas.removeEventListener(type,listener,options));}
    get stale(){return !!this.scene?.items.length&&!matches(this.scene.binding,this.current);}
    get active(){return !this.stale&&!!this.scene?.items.length;}
    context(){return {status:this.stale?'retired':this.active?'current':'empty',coordinate_space:'viewport_normalized',anchor_lifetime:'source_and_frame',item_count:this.scene?.items.length||0,can_undo:!!this.past.length,can_redo:!!this.future.length,drawing:this.tool!=='navigate',scene:this.snapshot()};}
    snapshot(){return this.scene?clone(this.scene):null;}
    setBinding(value){const next=value===null?null:binding(value),changed=!equal(this.current,next);this.current=next;if(changed){this.cancel();if(this.stale||!next)this.setTool('navigate');this.draw();this.callbacks.onChange(this.context());}return !this.stale;}
    restore(value){const next=value===null?null:scene(value);if(equal(this.scene,next))return;this.cancel();this.scene=next;this.past=[];this.future=[];this.sequence=Math.max(0,...(next?.items||[]).map(mark=>Number(mark.id.replace(/^mark_/,''))||0));if(this.stale)this.setTool('navigate');this.changed();}
    setTool(value){if(!TOOLS.includes(value))fail('Unknown markup tool.');if(value!=='navigate'&&(!this.current||this.stale))fail(this.stale?'This sketch belongs to a different view. Restore that view or clear the sketch before drawing.':'Wait for a rendered model before drawing.');this.cancel();this.tool=value;this.mode();this.callbacks.onModeChange(value);return value;}
    mode(){this.canvas.style.pointerEvents=this.tool==='navigate'?'none':'auto';this.canvas.style.touchAction=this.tool==='navigate'?'auto':'none';this.canvas.style.cursor=this.tool==='navigate'?'default':'crosshair';this.canvas.setAttribute('aria-label',this.tool==='navigate'?'Viewport markup':'Draw viewport '+this.tool);this.canvas.tabIndex=this.tool==='navigate'?-1:0;}
    setStyle({color=this.color,width=this.width,font_size=this.fontSize}={}){if(typeof color!=='string'||!/^#[a-fA-F0-9]{6}$/.test(color)||!finite(width,1,16)||!finite(font_size,10,48))fail('Invalid markup style.');this.color=color;this.width=width;this.fontSize=font_size;}
    setText(value){if(typeof value!=='string'||bytes(value)>LIMITS.textBytes||/[\x00-\x08\x0b\x0c\x0e-\x1f\x7f]/.test(value)||value.split('\n').length>8)fail('Markup text exceeds its limit.');this.text=value;}
    position(event){const rect=this.canvas.getBoundingClientRect();if(rect.width<1||rect.height<1)fail('Markup viewport is hidden.');return [Math.max(0,Math.min(1,(event.clientX-rect.left)/rect.width)),Math.max(0,Math.min(1,(event.clientY-rect.top)/rect.height))];}
    start(event){if(this.tool==='navigate'||event.button!==0||event.isPrimary===false||this.gesture)return;event.preventDefault();event.stopPropagation();if(!this.current||this.stale)fail('Markup frame changed before drawing.');if(this.tool==='text'&&!this.text.trim())fail('Enter a text label before placing it.');
      const p=this.position(event),mark={id:'mark_'+(++this.sequence),kind:this.tool,color:this.color,width:this.width,points:this.tool==='pen'||this.tool==='text'?[p]:[p,p]};if(this.tool==='text'){mark.text=this.text;mark.font_size=this.fontSize;}
      this.gesture={pointer:event.pointerId,mark,binding:clone(this.current)};this.canvas.setPointerCapture?.(event.pointerId);this.canvas.focus?.({preventScroll:true});this.callbacks.onGestureChange(true);this.draw();
    }
    move(event){if(!this.gesture||event.pointerId!==this.gesture.pointer)return;event.preventDefault();event.stopPropagation();const p=this.position(event),mark=this.gesture.mark;if(mark.kind==='text')return;
      if(mark.kind==='pen'){const last=mark.points.at(-1),rect=this.canvas.getBoundingClientRect();if(Math.hypot((p[0]-last[0])*rect.width,(p[1]-last[1])*rect.height)<1)return;if(mark.points.length>=LIMITS.strokePoints)fail('A freehand stroke is limited to 2048 points.');mark.points.push(p);}else mark.points[1]=p;this.draw();
    }
    end(event){if(!this.gesture||event.pointerId!==this.gesture.pointer)return;this.move(event);const gesture=this.gesture;this.gesture=null;this.canvas.releasePointerCapture?.(event.pointerId);this.callbacks.onGestureChange(false);if(!matches(gesture.binding,this.current))fail('Markup frame changed during drawing.');this.add(gesture.mark);}
    cancel(){if(!this.gesture)return;const pointer=this.gesture.pointer;this.gesture=null;if(this.canvas.hasPointerCapture?.(pointer))this.canvas.releasePointerCapture(pointer);this.callbacks.onGestureChange(false);this.draw();}
    add(value){if(!this.current||this.stale)fail('Cannot add markup to a changed frame.');const mark=item(value),next={schema_version:1,binding:this.scene?.items.length?this.scene.binding:clone(this.current),items:[...(this.scene?.items||[]),mark]};this.commit(scene(next));}
    commit(next){this.past.push(this.snapshot());if(this.past.length>LIMITS.history)this.past.shift();this.future=[];this.scene=next;this.changed();}
    clear(){this.cancel();if(this.scene)this.commit(null);}
    undo(){this.cancel();if(!this.past.length)return false;this.future.push(this.snapshot());this.scene=this.past.pop();if(this.stale)this.setTool('navigate');this.changed();return true;}
    redo(){this.cancel();if(!this.future.length)return false;this.past.push(this.snapshot());this.scene=this.future.pop();if(this.stale)this.setTool('navigate');this.changed();return true;}
    changed(){this.draw();this.callbacks.onChange(this.context());}
    resize(){const rect=this.canvas.getBoundingClientRect(),w=Math.max(1,rect.width),h=Math.max(1,rect.height),ratio=Math.min(2,globalThis.devicePixelRatio||1,4096/w,4096/h,Math.sqrt(4000000/(w*h))),width=Math.max(1,Math.floor(w*ratio)),height=Math.max(1,Math.floor(h*ratio));if(this.canvas.width!==width)this.canvas.width=width;if(this.canvas.height!==height)this.canvas.height=height;this.draw();}
    draw(){if(this.destroyed)return;const ctx=this.ctx,c=this.canvas;ctx.clearRect(0,0,c.width,c.height);if(this.active)paint(ctx,this.scene,c.width,c.height);if(this.gesture&&matches(this.gesture.binding,this.current))paint(ctx,{schema_version:1,binding:this.gesture.binding,items:[this.gesture.mark]},c.width,c.height);}
    paint(ctx,width,height){if(this.gesture)fail('Finish the current stroke before capturing.');if(this.active)paint(ctx,this.scene,width,height);}
    destroy(){if(this.destroyed)return;this.cancel();this.destroyed=true;for(const remove of this.listeners)remove();this.listeners=[];this.ctx.clearRect(0,0,this.canvas.width,this.canvas.height);this.canvas.style.pointerEvents='none';}
    static bind(value,camera,width,height){const p=value?.payload;if(!p?.evaluation_id||!value.view_id)return null;return binding({source:{view_id:value.view_id,document_id:p.read_only?null:p.document_id,revision:p.read_only?null:p.revision,evaluation_id:p.evaluation_id,feature_id:p.read_only?null:p.feature_id,review_sha256:p.read_only?p.artifact.review_sha256:null,draft:!!p.draft},frame:{camera:clone(camera),hidden_part_ids:[...(value.hidden_part_ids||[])].sort(),presentation:value.presentation??null,appearance:value.appearance??null,render:value.render??null,artifact_pose:p.read_only?(value.artifact_pose||{}):null,section:value.section??null,annotations:value.annotations||[]},viewport:[width,height]});}
  }
  Object.assign(CadMarkup,{LIMITS,TOOLS,validateScene:scene,validateBinding:binding,validateItem:item,matches,sameScene:equal,paintScene:paint});globalThis.CadMarkup=CadMarkup;
})();
