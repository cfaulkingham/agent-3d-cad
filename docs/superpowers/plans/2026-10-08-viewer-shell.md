# Viewer Shell Redesign Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Give the CAD viewer a Shapr3D-style floating layout, light studio look, cursor-driven face/edge selection and Shapr3D-style navigation, without changing any protocol.

**Architecture:** Testable logic (standard views, camera interpolation, zoom-to-cursor, framing, `auto` pick, hover, theme palette) goes into `web/renderer.js` and is exported on `CadRenderer.math` / the renderer API. Pure UI decisions (theme resolution, breakpoints, orientation-cube mapping) go into one new asset `web/shell.js`; its DOM glue and the rewritten `viewer.html`/`styles.css` re-parent the existing section elements with their IDs intact, so `app.js` logic and `state.js`/`bridge.js` are unchanged.

**Tech Stack:** Vanilla JS in `vm`-tested IIFEs, WebGL1/2, CSS custom properties, CMake asset embedding, Rust `desktop/build.rs`.

Spec: `docs/superpowers/specs/2026-10-08-viewer-shell-design.md`.

## Global Constraints

- No protocol change: `docs/PROTOCOL.md` untouched; `cad_context` selection references identical to before.
- Strict CSP: only inline CSS/JS and `data:` images; no web fonts, no network.
- The viewer asset list is hard-coded in `cmake/EmbedViewerApp.cmake`, `tests/embed_viewer_smoke.cmake`, `tests/app_protocol_tests.cpp` and `desktop/build.rs`; all four must list `shell.js` / `@VIEWER_SHELL@`.
- `tests/export_ui_tests.mjs` reads `web/app.js` as text; the export code must stay in `app.js`.
- Existing Node suites must keep passing without weakening: `viewer_renderer_tests.js`, `live_ui_tests.mjs`, `playback_ui_tests.mjs`, `export_ui_tests.mjs`, `artifact_retarget_ui_tests.mjs`, `desktop_bridge_tests.mjs`, `webgl_renderer_tests.mjs`.
- `setMode('face'|'edge')` stays in the renderer API; default mode becomes `auto`.
- Stored default model colour (`default_color`, `[.66,.75,.8]`) is user/agent data and is not changed.
- Edge tolerance: `auto` 6px, `edge` mode stays 9px. Click-vs-drag threshold stays 3px.
- Animation 250 ms ease-out; `prefers-reduced-motion` makes it instant; camera saved to view state once at the end.
- Light theme default; dark when host context `theme` is `dark`, else `prefers-color-scheme`.
- Secondary text contrast >= 4.5:1 (use `#5f6b78` on white, not `#76828f`).
- Dev-only harness; no new product dependency and no Playwright install without asking.

## File Structure

| File | Responsibility |
|---|---|
| `web/renderer.js` | Camera math (views, lerp, zoom-about, frame), `auto` pick + hover, theme uniforms + background pass, input mapping, animation |
| `web/shell.js` (new) | `CadShell`: `resolveTheme`, `layoutMode`, cube geometry, DOM glue (dock, popovers, Scene card, selection bar, cube, theme) |
| `web/viewer.html`, `web/styles.css` | Floating layout markup and light/dark tokens; section elements keep their IDs |
| `web/app.js` | Calls into `CadShell`; drops mode-toggle wiring; selection-bar/Details/Export-menu wiring |
| `tests/webgl_renderer_tests.mjs` | New renderer tests (mock canvas) |
| `tests/viewer_shell_tests.mjs` (new) | `CadShell` pure-function tests |
| `CMakeLists.txt` | Register `viewer_shell_tests.mjs` |
| `tests/viewer_shell_harness/` (new, dev-only) | Static page + mock bridge for screenshots |

Test commands (run from repo root):

```sh
node tests/webgl_renderer_tests.mjs build-package/agent-3d-cad
node tests/viewer_shell_tests.mjs
node tests/live_ui_tests.mjs && node tests/viewer_renderer_tests.js && node tests/playback_ui_tests.mjs && node tests/export_ui_tests.mjs && node tests/artifact_retarget_ui_tests.mjs
```

Baseline before any change (recorded 2026-10-08): `viewer renderer: 30`, `325 live UI`, `PASS 51 playback`, `PASS 110 export UI`, `PASS 34 artifact retarget`.

---

### Task 1: `shell.js` asset, embed plumbing and pure helpers

**Files:**
- Create: `web/shell.js`, `tests/viewer_shell_tests.mjs`
- Modify: `web/viewer.html` (script line), `cmake/EmbedViewerApp.cmake`, `tests/embed_viewer_smoke.cmake`, `tests/app_protocol_tests.cpp:46-49`, `desktop/build.rs`, `CMakeLists.txt` (register test near line 155)

**Interfaces:**
- Produces (`globalThis.CadShell`):
  - `resolveTheme(hostContext, prefersDark) -> 'light'|'dark'`
  - `layoutMode(width) -> {scene:'open'|'chip', dock:'icons'|'menu', compact:boolean}`
  - `CUBE_FACES: {name, normal, right, up}[]` (six entries)
  - `cubeDirection(faceName, u, v) -> [x,y,z]` (u,v in [0,1], v=0 is the top of the displayed face)
  - `cubeMatrix(camera) -> number[16]` (CSS `matrix3d`, column-major)
  - `cubeFaceMatrix(faceName, half) -> number[16]`

- [ ] **Step 1: Write the failing test** — create `tests/viewer_shell_tests.mjs`:

```js
// Pure CadShell tests; no DOM, browser or product runtime dependencies.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import vm from 'node:vm';
let checks = 0;
function test(name, action) { action(); checks++; console.log('PASS ' + name); }
const environment = { console };
vm.runInNewContext(fs.readFileSync(new URL('../web/shell.js', import.meta.url), 'utf8'), environment);
const Shell = environment.CadShell;
const near = (a, b, eps = 1e-9) => assert.ok(Math.abs(a - b) <= eps, `${a} != ${b}`);

test('theme: a host theme wins, otherwise the OS preference, otherwise light', () => {
  assert.equal(Shell.resolveTheme({ theme: 'dark' }, false), 'dark');
  assert.equal(Shell.resolveTheme({ theme: 'light' }, true), 'light');
  assert.equal(Shell.resolveTheme({}, true), 'dark');
  assert.equal(Shell.resolveTheme({ theme: 'sepia' }, false), 'light');
  assert.equal(Shell.resolveTheme(undefined, false), 'light');
  assert.equal(Shell.resolveTheme(null, true), 'dark');
});

test('layout breakpoints follow the spec table', () => {
  assert.deepEqual(Shell.layoutMode(1280), { scene: 'open', dock: 'icons', compact: false });
  assert.deepEqual(Shell.layoutMode(900), { scene: 'open', dock: 'icons', compact: false });
  assert.deepEqual(Shell.layoutMode(899), { scene: 'chip', dock: 'icons', compact: false });
  assert.deepEqual(Shell.layoutMode(560), { scene: 'chip', dock: 'icons', compact: false });
  assert.deepEqual(Shell.layoutMode(559), { scene: 'chip', dock: 'menu', compact: true });
  assert.deepEqual(Shell.layoutMode(360), { scene: 'chip', dock: 'menu', compact: true });
  assert.deepEqual(Shell.layoutMode(NaN), { scene: 'chip', dock: 'menu', compact: true });
});

console.log(`viewer shell: ${checks} checks passed`);
```

- [ ] **Step 2: Run it to verify it fails**

Run: `node tests/viewer_shell_tests.mjs`
Expected: FAIL — `ENOENT ... web/shell.js`.

- [ ] **Step 3: Create `web/shell.js` with the first two helpers**

```js
/* Viewer shell: pure UI decisions first, DOM glue added by later tasks. */
(() => {
  'use strict';
  const resolveTheme = (hostContext, prefersDark) => {
    const theme = hostContext && hostContext.theme;
    if (theme === 'light' || theme === 'dark') return theme;
    return prefersDark ? 'dark' : 'light';
  };
  const layoutMode = width => {
    if (!Number.isFinite(width) || width < 560) return { scene: 'chip', dock: 'menu', compact: true };
    if (width < 900) return { scene: 'chip', dock: 'icons', compact: false };
    return { scene: 'open', dock: 'icons', compact: false };
  };
  globalThis.CadShell = Object.freeze({ resolveTheme, layoutMode });
})();
```

- [ ] **Step 4: Run to verify it passes**

Run: `node tests/viewer_shell_tests.mjs`
Expected: `viewer shell: 2 checks passed`.

- [ ] **Step 5: Embed plumbing.** Apply all of:

`web/viewer.html` — change the last script line to include shell between state and app:
`<script>@VIEWER_BRIDGE@</script><script>@VIEWER_RENDERER@</script><script>@VIEWER_STATE@</script><script>@VIEWER_SHELL@</script><script>@VIEWER_APP@</script>`

`cmake/EmbedViewerApp.cmake` — line 2 becomes `set(viewer_assets viewer.html styles.css bridge.js renderer.js state.js shell.js app.js)` and the loop becomes `foreach(asset IN ITEMS styles bridge renderer state shell app)`.

`tests/embed_viewer_smoke.cmake` line 8 and any other asset list — add `shell.js` between `state.js` and `app.js`; any `${asset}.${extension}` loop at lines ~50-60 must include `shell` (read the file and mirror exactly how `state` is handled).

`tests/app_protocol_tests.cpp` — in the placeholder pair list (line ~48-49) add `{"@VIEWER_SHELL@", "shell.js"}` between the `state.js` and `app.js` pairs.

`desktop/build.rs` — the array becomes `[("styles", "css"), ("bridge", "js"), ("renderer", "js"), ("state", "js"), ("shell", "js"), ("app", "js")]`.

`CMakeLists.txt` — beside the other node tests (line ~155-162) add:

```cmake
    add_test(NAME viewer_shell COMMAND "${AGENTCAD_NODE_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/viewer_shell_tests.mjs")
    set_tests_properties(viewer_shell PROPERTIES TIMEOUT 30)
```

- [ ] **Step 6: Verify the embed still builds and the viewer is intact**

Run: `cmake -S . -B build-package >/dev/null && cmake --build build-package --target agent-3d-cad --parallel 4 2>&1 | tail -3 && ctest --test-dir build-package -R "embed|app_protocol|viewer_shell|live_ui|offline_renderer" --output-on-failure 2>&1 | tail -15`
Expected: build succeeds; the listed tests pass (names may differ; use `ctest --test-dir build-package -N | grep -iE "viewer|embed|protocol"` to list them first).

- [ ] **Step 7: Commit**

```bash
git add web/shell.js web/viewer.html cmake/EmbedViewerApp.cmake tests/embed_viewer_smoke.cmake tests/app_protocol_tests.cpp desktop/build.rs CMakeLists.txt tests/viewer_shell_tests.mjs
git commit -m "Add shell.js viewer asset with theme and layout helpers

Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 2: Camera math — views, interpolation, zoom-to-cursor, framing

**Files:**
- Modify: `web/renderer.js` (new pure functions near `camera()`, export on `CadRenderer.math`, `setView`, `reset`)
- Test: `tests/webgl_renderer_tests.mjs`

**Interfaces:**
- Produces on `CadRenderer.math`: `STANDARD_VIEWS` (frozen `{iso,top,bottom,front,back,right,left} -> [yaw,pitch]`), `viewFromDirection(d)`, `lerpCamera(a,b,t)`, `easeOut(t)`, `zoomAbout(camera,factor,x,y,width,height)`, `fitCamera(camera,bounds,width,height)`, `entityBounds(model,kind,id)`.
- `setView(name)` accepts all seven names.

- [ ] **Step 1: Write the failing tests** — append to `tests/webgl_renderer_tests.mjs` before the final `console.log` (add the new names to the destructuring of `Renderer.math` at line 17 if it destructures; otherwise read them as `Renderer.math.zoomAbout` etc.):

```js
const M=Renderer.math;
const near=(a,b,eps=1e-9)=>assert.ok(Math.abs(a-b)<=eps,`${a} != ${b}`);
test('standard views look along the documented axes',()=>{
  const eye=name=>{const [yaw,pitch]=M.STANDARD_VIEWS[name],b=M.basis({yaw,pitch,zoom:1,pan:[0,0]});return b[2].map(v=>Math.round(v*1e9)/1e9+0);};
  assert.deepEqual(eye('front'),[0,1,0]);assert.deepEqual(eye('back'),[0,-1,0]);
  assert.deepEqual(eye('right'),[-1,0,0]);assert.deepEqual(eye('left'),[1,0,0]);
  assert.deepEqual(eye('top'),[0,0,-1]);assert.deepEqual(eye('bottom'),[0,0,1]);
  assert.deepEqual(Object.keys(M.STANDARD_VIEWS).sort(),['back','bottom','front','iso','left','right','top']);
});
test('viewFromDirection reproduces the axis views and gives a true isometric corner',()=>{
  for(const [name,d] of [['front',[0,-1,0]],['back',[0,1,0]],['right',[1,0,0]],['left',[-1,0,0]],['top',[0,0,1]],['bottom',[0,0,-1]]]){
    const v=M.viewFromDirection(d),[yaw,pitch]=M.STANDARD_VIEWS[name];
    near(Math.cos(v.yaw),Math.cos(yaw),1e-9);near(Math.sin(v.yaw),Math.sin(yaw),1e-9);near(v.pitch,pitch,1e-9);
  }
  const corner=M.viewFromDirection([1,-1,1]);near(corner.yaw,-Math.PI/4);near(corner.pitch,Math.asin(1/Math.sqrt(3)));
  assert.throws(()=>M.viewFromDirection([0,0,0]));
});
test('lerpCamera hits both endpoints, takes the short way round and eases',()=>{
  const a={yaw:3,pitch:0,zoom:1,pan:[0,0]},b={yaw:-3,pitch:.5,zoom:4,pan:[.2,-.2]};
  assert.deepEqual(M.lerpCamera(a,b,0),a);
  const end=M.lerpCamera(a,b,1);near(Math.cos(end.yaw),Math.cos(b.yaw));near(Math.sin(end.yaw),Math.sin(b.yaw));near(end.zoom,4);near(end.pitch,.5);
  const mid=M.lerpCamera(a,b,.5);near(Math.abs(Math.cos(mid.yaw)),1,1e-2);near(mid.zoom,2);
  near(M.easeOut(0),0);near(M.easeOut(1),1);assert.ok(M.easeOut(.5)>.5);
});
test('zoomAbout keeps the world point under the cursor fixed',()=>{
  const c={yaw:.4,pitch:.7,zoom:1.3,pan:[.05,-.08]},p=[.12,-.2,.07],[x,y]=M.project(p,c,640,480);
  for(const factor of [1.7,.4,3]){const z=M.zoomAbout(c,factor,x,y,640,480),[x2,y2]=M.project(p,z,640,480);near(x2,x,1e-7);near(y2,y,1e-7);near(z.zoom,Math.min(50,Math.max(.05,c.zoom*factor)),1e-12);}
  const clamped=M.zoomAbout({...c,zoom:49},10,100,100,640,480);assert.equal(clamped.zoom,50);
});
test('fitCamera frames bounds inside the viewport and centred',()=>{
  const c={yaw:-.65,pitch:.6,zoom:1,pan:[0,0]},bounds={min:[-.5,-.5,-.5],max:[.5,.5,.5]},f=M.fitCamera(c,bounds,640,480);
  const corners=[];for(const x of [-.5,.5])for(const y of [-.5,.5])for(const z of [-.5,.5])corners.push(M.project([x,y,z],f,640,480));
  for(const [x,y] of corners){assert.ok(x>0&&x<640&&y>0&&y<480);}
  near((Math.min(...corners.map(p=>p[0]))+Math.max(...corners.map(p=>p[0])))/2,320,1e-6);
  near((Math.min(...corners.map(p=>p[1]))+Math.max(...corners.map(p=>p[1])))/2,240,1e-6);
});
test('entityBounds finds a face or edge extent and rejects unknown ids',()=>{
  const model=M.prepare(evaluation([[-1,-1,0],[1,-1,0],[-1,1,0]],[[0,1,2]],['face-1'],[{id:'edge-1',points:[[-.8,-.4,0],[.2,-.4,0]]}]));
  const face=M.entityBounds(model,'face','face-1'),edge=M.entityBounds(model,'edge','edge-1');
  assert.ok(face.max[0]-face.min[0]>0.4);assert.ok(edge.max[0]-edge.min[0]>0.2);
  assert.equal(M.entityBounds(model,'face','nope'),null);
});
test('setView accepts the new names and reset still frames the model',()=>{
  const {canvas}=mockCanvas(),r=new Renderer(canvas);r.load(base);
  for(const name of ['iso','top','bottom','front','back','right','left'])r.setView(name);
  near(r.getCamera().yaw,M.camera({yaw:Math.PI/2,pitch:0,zoom:1,pan:[0,0]}).yaw);
  assert.throws(()=>r.setView('diagonal'));r.reset();assert.ok(r.getCamera().zoom>0);r.destroy();
});
```

- [ ] **Step 2: Run to verify it fails**

Run: `node tests/webgl_renderer_tests.mjs build-package/agent-3d-cad 2>&1 | tail -8`
Expected: FAIL — `M.STANDARD_VIEWS` / `viewFromDirection` is undefined.

- [ ] **Step 3: Implement** in `web/renderer.js`, directly after `project`/`screenRay` (around line 160):

```js
  const STANDARD_VIEWS=Object.freeze({iso:[-.65,.6],top:[0,Math.PI/2],bottom:[0,-Math.PI/2],front:[0,0],back:[Math.PI,0],right:[-Math.PI/2,0],left:[Math.PI/2,0]});
  const wrapAngle=a=>Math.atan2(Math.sin(a),Math.cos(a));
  const easeOut=t=>1-Math.pow(1-Math.max(0,Math.min(1,t)),3);
  function viewFromDirection(d) {
    if(!Array.isArray(d)||d.length!==3||!d.every(Number.isFinite))fail('View direction must be three finite numbers.');
    const n=Math.hypot(...d);if(!(n>0))fail('View direction must be nonzero.');
    const c=d.map(v=>v/n),flat=Math.hypot(c[0],c[1]);
    return {yaw:flat<1e-9?0:Math.atan2(-c[0],-c[1]),pitch:Math.asin(Math.max(-1,Math.min(1,c[2])))};
  }
  function lerpCamera(a,b,t) {
    const k=Math.max(0,Math.min(1,t));
    return {yaw:a.yaw+wrapAngle(b.yaw-a.yaw)*k,pitch:a.pitch+(b.pitch-a.pitch)*k,zoom:a.zoom*Math.pow(b.zoom/a.zoom,k),pan:[a.pan[0]+(b.pan[0]-a.pan[0])*k,a.pan[1]+(b.pan[1]-a.pan[1])*k]};
  }
  function zoomAbout(c,factor,x,y,width,height) {
    const zoom=Math.max(.05,Math.min(50,c.zoom*factor)),k=zoom/c.zoom,size=Math.min(width,height);
    return {yaw:c.yaw,pitch:c.pitch,zoom,pan:[c.pan[0]*k+(x-width/2)/size*(1-k),c.pan[1]*k+(y-height/2)/size*(1-k)]};
  }
  function fitCamera(c,bounds,width,height) {
    const b=basis(c),corners=[];
    for(const x of [bounds.min[0],bounds.max[0]])for(const y of [bounds.min[1],bounds.max[1]])for(const z of [bounds.min[2],bounds.max[2]])corners.push([dot(b[0],[x,y,z]),dot(b[1],[x,y,z])]);
    const low=[0,1].map(a=>Math.min(...corners.map(p=>p[a]))),high=[0,1].map(a=>Math.max(...corners.map(p=>p[a]))),size=Math.min(width,height);
    const zoom=Math.max(.05,Math.min(50,Math.min(width*.82/(Math.max(1e-12,high[0]-low[0])*.68*size),height*.72/(Math.max(1e-12,high[1]-low[1])*.68*size))));
    return {yaw:c.yaw,pitch:c.pitch,zoom,pan:[-(low[0]+high[0])/2*.68*zoom,(low[1]+high[1])/2*.68*zoom]};
  }
  function entityBounds(model,kind,id) {
    if(!model||!model.geometries[kind]?.has(id))return null;
    const min=[Infinity,Infinity,Infinity],max=[-Infinity,-Infinity,-Infinity],add=(a,i)=>{for(let k=0;k<3;k++){const v=a[i+k];if(v<min[k])min[k]=v;if(v>max[k])max[k]=v;}};
    if(kind==='edge'){const edge=model.edges.find(e=>e.id===id);if(!edge)return null;for(let i=0;i<edge.points.length;i+=3)add(edge.points,i);}
    else{
      const owners=model.triangleFaces;
      for(let t=0;t<model.indices.length/3;t++){if(owners[t]!==id)continue;for(let j=0;j<3;j++)add(model.positions,3*model.indices[3*t+j]);}
    }
    if(min[0]===Infinity)return null;
    for(let k=0;k<3;k++){const pad=Math.max(0,.1-(max[k]-min[k]))/2;min[k]-=pad;max[k]+=pad;}
    return {min,max};
  }
```

Then export the new names: extend the `CadRenderer.math=Object.freeze({...})` list with `STANDARD_VIEWS,viewFromDirection,lerpCamera,easeOut,zoomAbout,fitCamera,entityBounds`.

Replace `setView` and `reset` (lines ~717-730):

```js
    reset(){return this._checked(()=>{
      const bounds=this.model?.bounds,home=cloneCamera(DEFAULT_CAMERA);
      if(bounds){const {width,height}=this._size();this.camera=camera(fitCamera(home,bounds,width,height));}else this.camera=home;
      this._cameraChanged();
    });}
    setView(view){return this._checked(()=>{if(!Object.hasOwn(STANDARD_VIEWS,view))fail('Unknown camera view.');[this.camera.yaw,this.camera.pitch]=STANDARD_VIEWS[view];this._cameraChanged();});}
```

- [ ] **Step 4: Run to verify the new and existing renderer tests pass**

Run: `node tests/webgl_renderer_tests.mjs build-package/agent-3d-cad 2>&1 | tail -5; node tests/live_ui_tests.mjs | tail -1; node tests/artifact_retarget_ui_tests.mjs | tail -1`
Expected: `webgl renderer: N checks passed`, `325 live UI bridge/state checks passed`, `PASS 34 ...`.

- [ ] **Step 5: Commit**

```bash
git add web/renderer.js tests/webgl_renderer_tests.mjs
git commit -m "Add camera views, interpolation, zoom-to-cursor and framing math

Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 3: Unified `auto` pick and hover

**Files:**
- Modify: `web/renderer.js` (`pick`, `_controls`, `_pick`, `_draw`, `setMode`, constructor, `destroy`)
- Test: `tests/webgl_renderer_tests.mjs`

**Interfaces:**
- `pick(model,c,w,h,x,y,mode)` with `mode==='auto'` returns `{id,kind,ambiguous,section?}` (`kind` present only for `auto`).
- `CadRenderer` constructor option `onHover(hover|null)`; `renderer.hover` is `{kind,entity_id}|null`; default `mode` is `'auto'`; `setMode` accepts `'auto'|'face'|'edge'`.
- Selection produced by a click in `auto` has `kind` of the entity actually hit.

- [ ] **Step 1: Write the failing tests**

```js
const withEdge=()=>evaluation([[-1,-1,0],[1,-1,0],[-1,1,0]],[[0,1,2]],['face-1'],[{id:'edge-1',points:[[-.8,-.4,0],[.2,-.4,0]]}]);
function edgeScreen(model){const p=model.edges[0].points,a=project([p[0],p[1],p[2]],defaultCamera,400,400),b=project([p[3],p[4],p[5]],defaultCamera,400,400);return [(a[0]+b[0])/2,(a[1]+b[1])/2];}
test('auto pick: an edge within 6px wins, a face wins beyond it, edge mode keeps 9px',()=>{
  const model=prepare(withEdge()),[x,y]=edgeScreen(model);
  let r=pick(model,defaultCamera,400,400,x,y+3,'auto');assert.equal(r.id,'edge-1');assert.equal(r.kind,'edge');
  r=pick(model,defaultCamera,400,400,x,y+8,'auto');assert.equal(r.id,'face-1');assert.equal(r.kind,'face');
  assert.equal(pick(model,defaultCamera,400,400,x,y+8,'edge').id,'edge-1');
  assert.equal(pick(model,defaultCamera,400,400,x,y+8,'face').id,'face-1');
  assert.equal('kind' in pick(model,defaultCamera,400,400,x,y+8,'face'),false);
});
test('auto pick: empty space selects nothing and ambiguous edges fall through to the face',()=>{
  const model=prepare(withEdge());
  assert.equal(pick(model,defaultCamera,400,400,5,5,'auto').id,null);
  const twin=withEdge();twin.mesh.edges.push({id:'edge-2',points:[[-.8,-.4,0],[.2,-.4,0]]});twin.topology.edges.push({id:'edge-2'});
  const m2=prepare(twin),[x,y]=edgeScreen(m2),r=pick(m2,defaultCamera,400,400,x,y,'auto');
  assert.equal(r.id,'face-1');assert.equal(r.kind,'face');
});
test('auto pick reports overlapping faces without selecting either',()=>{
  const d=evaluation([...flat,...flat],[[0,1,2],[3,4,5]],['face-1','face-2']),m=prepare(d),p=project([-.2,-.2,0],defaultCamera,400,400);
  const r=pick(m,defaultCamera,400,400,p[0],p[1],'auto');assert.equal(r.id,null);assert.equal(r.ambiguous,true);
});
test('a click selects whatever auto resolves and Escape clears; mode compatibility remains',()=>{
  const {canvas,stats}=mockCanvas(),picks=[],r=new Renderer(canvas,{onPick:(...v)=>picks.push(v)});
  assert.equal(r.mode,'auto');r.load(withEdge());r.setCamera(defaultCamera);
  const m=r.model,p=m.edges[0].points,a=project([p[0],p[1],p[2]],defaultCamera,640,480),b=project([p[3],p[4],p[5]],defaultCamera,640,480);
  r._pick((a[0]+b[0])/2,(a[1]+b[1])/2+2);assert.equal(picks.at(-1)[0].reference.kind,'edge');assert.equal(r.selection.kind,'edge');
  const f=project([-.2,-.2,0],defaultCamera,640,480);r._pick(f[0],f[1]);assert.equal(picks.at(-1)[0].reference.kind,'face');
  r._pick(2,2);assert.equal(picks.at(-1)[0],null);
  r.setMode('edge');assert.equal(r.mode,'edge');r.setMode('face');r.setMode('auto');assert.throws(()=>r.setMode('vertex'));
  r.destroy();
});
test('hover highlights the entity under an idle cursor without selecting, one pick per frame',()=>{
  const {canvas,stats}=mockCanvas(),hovers=[],picks=[],r=new Renderer(canvas,{onHover:h=>hovers.push(h),onPick:(...v)=>picks.push(v)});
  r.load(withEdge());r.setCamera(defaultCamera);flush();
  const p=r.model.edges[0].points,a=project([p[0],p[1],p[2]],defaultCamera,640,480),b=project([p[3],p[4],p[5]],defaultCamera,640,480),cx=(a[0]+b[0])/2,cy=(a[1]+b[1])/2;
  const move=(x,y)=>stats.listeners.get('pointermove')({pointerId:1,clientX:x,clientY:y,buttons:0});
  move(cx,cy+1);move(cx,cy+2);move(cx,cy+3);flush();
  assert.deepEqual(r.hover,{kind:'edge',entity_id:'edge-1'});assert.equal(hovers.length,1);assert.equal(r.selection,null);assert.equal(picks.length,0);
  const f=project([-.2,-.2,0],defaultCamera,640,480);move(f[0],f[1]);flush();assert.deepEqual(r.hover,{kind:'face',entity_id:'face-1'});
  stats.listeners.get('pointerleave')({});flush();assert.equal(r.hover,null);assert.equal(hovers.at(-1),null);
  r.destroy();assert.equal(stats.listeners.size,0);
});
test('hover is skipped while dragging and cleared when the model changes',()=>{
  const {canvas,stats}=mockCanvas(),r=new Renderer(canvas);r.load(withEdge());r.setCamera(defaultCamera);flush();
  const f=project([-.2,-.2,0],defaultCamera,640,480);
  stats.listeners.get('pointerdown')({pointerId:1,button:0,clientX:f[0],clientY:f[1],shiftKey:false,preventDefault(){}});
  stats.listeners.get('pointermove')({pointerId:1,clientX:f[0]+30,clientY:f[1]+30,buttons:1});flush();assert.equal(r.hover,null);
  stats.listeners.get('pointerup')({pointerId:1,clientX:f[0]+30,clientY:f[1]+30});
  stats.listeners.get('pointermove')({pointerId:1,clientX:f[0],clientY:f[1],buttons:0});flush();assert.ok(r.hover);
  r.load({...withEdge(),evaluation_id:'next'});assert.equal(r.hover,null);r.destroy();
});
```

- [ ] **Step 2: Run to verify they fail**

Run: `node tests/webgl_renderer_tests.mjs build-package/agent-3d-cad 2>&1 | grep -E "FAIL|Error|assert" | head -5`
Expected: FAIL — `pick(..., 'auto')` returns the face-mode shape (no `kind`) / `r.mode` is `'face'`.

- [ ] **Step 3: Implement.** In `web/renderer.js`:

1. Add constants beside `PICK_BUDGET`: `const EDGE_TOLERANCE=9, AUTO_EDGE_TOLERANCE=6;`.

2. Split `pick` (lines ~441-469) into helpers and add `auto`:

```js
  function pickFace(model,c,width,height,x,y,budget) {
    const hit=trace(model,screenRay(x,y,c,width,height,model.depthExtent??2),budget);
    return {id:hit.ids.size===1?[...hit.ids][0]:null,ambiguous:hit.ids.size>1,...(hit.section?{section:true}:{})};
  }
  function pickEdge(model,c,width,height,x,y,tolerance,budget) {
    const candidates=[];
    for(const edge of model.edges) {
      for(let i=3;i<edge.points.length;i+=3) {
        const segment=clipSegment(model,edge.points.subarray(i-3,i),edge.points.subarray(i,i+3));
        if(segment){const q=nearestSegment([x,y],...segment.map(p=>project(p,c,width,height)));if(q.distance<=tolerance&&q.point[0]>=0&&q.point[1]>=0&&q.point[0]<=width&&q.point[1]<=height)candidates.push({...q,id:edge.id});}
      }
    }
    candidates.sort((a,b)=>a.distance-b.distance);
    let best=Infinity,depth=Infinity;const ids=new Set();
    for(const candidate of candidates) {
      if(candidate.distance>best+.25)break;
      const q=candidate.point,extent=model.depthExtent??2,hit=trace(model,screenRay(q[0],q[1],c,width,height,extent),budget),z=q[2]+extent;
      // The deflection offset is normal to the surface; along the ray it grows
      // as 1/cos against the occluding triangle, capped at 4x for grazing views.
      if(z>hit.depth+DEPTH_EPS*4+(hit.section?0:(model.edgeAllowance||0)/Math.max(hit.facing,.25)))continue;
      if(best===Infinity){best=candidate.distance;depth=z;ids.add(candidate.id);}
      else if(z<depth-DEPTH_EPS) {depth=z;ids.clear();ids.add(candidate.id);}
      else if(Math.abs(candidate.distance-best)<=.25&&Math.abs(z-depth)<=DEPTH_EPS)ids.add(candidate.id);
    }
    const readonly=!ids.size&&model.section&&trace(model,screenRay(x,y,c,width,height,model.depthExtent??2),budget).section;
    return {id:ids.size===1?[...ids][0]:null,ambiguous:ids.size>1,...(readonly?{section:true}:{})};
  }
  function pick(model,c,width,height,x,y,mode='face') {
    if(!model||!Number.isFinite(width)||!Number.isFinite(height)||width<=0||height<=0||!Number.isFinite(x)||!Number.isFinite(y)||x<0||y<0||x>width||y>height)return {id:null,ambiguous:false};
    const budget={remaining:PICK_BUDGET};
    if(mode==='face')return pickFace(model,c,width,height,x,y,budget);
    if(mode==='edge')return pickEdge(model,c,width,height,x,y,EDGE_TOLERANCE,budget);
    const edge=pickEdge(model,c,width,height,x,y,AUTO_EDGE_TOLERANCE,budget);
    if(edge.id)return {...edge,kind:'edge'};
    const face=pickFace(model,c,width,height,x,y,budget);
    if(face.id)return {...face,kind:'face'};
    return {...face,ambiguous:face.ambiguous||edge.ambiguous};
  }
```

Keep `pick`'s previous 'face'/'edge' return objects byte-for-byte equal in shape (they are, by construction).

3. Renderer: constructor destructuring adds `onHover=()=>{}`; callbacks include it; initial `this.mode='auto'; this.hover=null; this.hoverPending=null; this.hoverPoint=null;`.

4. `_controls`: add listeners (these use the same `_listen`):

```js
      this._listen(canvas,'pointermove',event=>{ /* existing handler first, then: */
        if(!this.drag&&event.buttons===0){const rect=canvas.getBoundingClientRect();this.hoverPoint={x:event.clientX-rect.left,y:event.clientY-rect.top};this._scheduleHover();}
      });
      this._listen(canvas,'pointerleave',()=>{this.hoverPoint=null;this._setHover(null);});
```

Do not register a second `pointermove`; extend the existing handler (the mock keeps one callback per type). The existing handler begins `const drag=this.drag;if(!drag||drag.id!==event.pointerId)return;` — change to compute hover first, then run the drag logic.

5. Add methods:

```js
    _scheduleHover(){if(this.destroyed||this.hoverPending!==null)return;this.hoverPending=requestAnimationFrame(()=>{this.hoverPending=null;this._updateHover();});}
    _updateHover(){
      if(this.destroyed||this.lost||!this.model||this.drag||!this.hoverPoint)return;
      const {width,height}=this._size(),mode=this.mode,result=pick(this.model,this.camera,width,height,this.hoverPoint.x,this.hoverPoint.y,mode);
      this._setHover(result.id?{kind:mode==='auto'?result.kind:mode,entity_id:result.id}:null);
    }
    _setHover(value){
      const same=(a,b)=>a===b||(a&&b&&a.kind===b.kind&&a.entity_id===b.entity_id);
      if(same(this.hover,value))return;
      this.hover=value;this._schedule();this.callbacks.onHover(value?{...value}:null);
    }
```

6. `_pick`: derive `kind` — `const kind=this.mode==='auto'?result.kind:this.mode;` and replace every `this.mode` inside `_pick` with `kind` (selection `kind`, artifact reference kind mapping, `this.model.geometries[kind]`). For a null result `kind` may be undefined; guard: `result.id?...:null` already short-circuits before use of `kind` for geometry. Keep the `section`/`ambiguous` message logic.

7. `load()`, `setHiddenParts`, `setPresentation` set `this.hover=null` where they set `this.selection=null` (call `this._setHover(null)` inside `load` after the model replaced so the callback fires). `destroy()` cancels `hoverPending`, nulls `hover`.

8. `setMode`: allow `'auto'`: `if(!['auto','face','edge'].includes(mode))fail('Selection mode must be auto, face or edge.');`.

9. `_draw`: for now keep `uEdgeMode` as `this.mode==='edge'` (Task 4 reworks colours).

- [ ] **Step 4: Run all viewer Node tests**

Run: `node tests/webgl_renderer_tests.mjs build-package/agent-3d-cad 2>&1 | tail -4; node tests/live_ui_tests.mjs | tail -1; node tests/playback_ui_tests.mjs | tail -1; node tests/artifact_retarget_ui_tests.mjs | tail -1; node tests/desktop_bridge_tests.mjs 2>&1 | tail -1`
Expected: all pass. If `setMode('face')`-dependent existing assertions fail because the default is now `auto`, fix the production code, not the assertion — except tests that explicitly assert the default mode string, which are updated to `'auto'` with a one-line justification in the commit message.

- [ ] **Step 5: Commit**

```bash
git add web/renderer.js tests/webgl_renderer_tests.mjs
git commit -m "Unify face/edge picking and add hover resolution in the renderer

Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 4: Navigation input, animation, theme and highlights in the renderer

**Files:**
- Modify: `web/renderer.js` (`_controls`, `_schedule`, `_draw`, shader source, `_init`, `destroy`, new methods)
- Test: `tests/webgl_renderer_tests.mjs`

**Interfaces:**
- Constructor options: `now()` (default `performance.now`), `reducedMotion()` (default `matchMedia('(prefers-reduced-motion: reduce)')`).
- Methods: `animateTo(cameraTarget,{duration=250})`, `frameSelection()`, `fitAll()`, `setTheme(theme)`, `getTheme()`; `setView(name,{animate})`.
- `defaultTheme()` / `darkTheme()` exported on `CadRenderer.math`; theme shape: `{background:{top:[r,g,b],bottom:[r,g,b]},line:[r,g,b],select:[r,g,b],hover:[r,g,b]}`.
- Mouse: left-drag orbit, right-drag orbit, middle-drag pan, Shift+drag (any button) pan; wheel zooms about the cursor; double-click an entity frames it; double-click empty space fits; Space frames hover; digits 1-7 choose iso/front/back/top/bottom/right/left.

- [ ] **Step 1: Write the failing tests**

```js
const listener=(stats,type)=>stats.listeners.get(type);
const down=(stats,o)=>listener(stats,'pointerdown')({pointerId:1,button:0,shiftKey:false,preventDefault(){},...o});
test('mouse mapping: left and right orbit, middle and Shift pan',()=>{
  const {canvas,stats}=mockCanvas(),r=new Renderer(canvas);r.load(withEdge());r.setCamera({yaw:0,pitch:.3,zoom:1,pan:[0,0]});
  const drag=(o,dx,dy)=>{down(stats,{clientX:100,clientY:100,...o});listener(stats,'pointermove')({pointerId:1,clientX:100+dx,clientY:100+dy,buttons:1});listener(stats,'pointerup')({pointerId:1,clientX:100+dx,clientY:100+dy});};
  let c=r.getCamera();drag({button:0},40,0);assert.ok(r.getCamera().yaw!==c.yaw);assert.deepEqual(r.getCamera().pan,c.pan);
  c=r.getCamera();drag({button:2},0,30);assert.ok(r.getCamera().pitch!==c.pitch);assert.deepEqual(r.getCamera().pan,c.pan);
  c=r.getCamera();drag({button:1},40,10);assert.equal(r.getCamera().yaw,c.yaw);assert.ok(r.getCamera().pan[0]!==c.pan[0]);
  c=r.getCamera();drag({button:0,shiftKey:true},-40,0);assert.equal(r.getCamera().yaw,c.yaw);assert.ok(r.getCamera().pan[0]!==c.pan[0]);
  c=r.getCamera();drag({button:2,shiftKey:true},0,30);assert.equal(r.getCamera().pitch,c.pitch);assert.ok(r.getCamera().pan[1]!==c.pan[1]);
  r.destroy();
});
test('a short right-drag click does not select; a short left click does',()=>{
  const {canvas,stats}=mockCanvas(),picks=[],r=new Renderer(canvas,{onPick:(...v)=>picks.push(v)});r.load(withEdge());r.setCamera(defaultCamera);
  const f=project([-.2,-.2,0],defaultCamera,640,480);
  down(stats,{button:2,clientX:f[0],clientY:f[1]});listener(stats,'pointerup')({pointerId:1,clientX:f[0],clientY:f[1]});assert.equal(picks.length,0);
  down(stats,{button:0,clientX:f[0],clientY:f[1]});listener(stats,'pointerup')({pointerId:1,clientX:f[0],clientY:f[1]});assert.equal(picks.length,1);r.destroy();
});
test('wheel zooms about the cursor',()=>{
  const {canvas,stats}=mockCanvas(),r=new Renderer(canvas);r.load(withEdge());r.setCamera({yaw:.4,pitch:.5,zoom:1,pan:[.02,.03]});
  const c=r.getCamera(),p=[.1,-.1,0],[x,y]=project(p,c,640,480);
  listener(stats,'wheel')({preventDefault(){},deltaY:-240,deltaMode:0,clientX:x,clientY:y});
  const [x2,y2]=project(p,r.getCamera(),640,480);near(x2,x,1e-6);near(y2,y,1e-6);assert.ok(r.getCamera().zoom>c.zoom);r.destroy();
});
test('animateTo interpolates over 250ms, is cancelled by input and is instant for reduced motion',()=>{
  let t=0;const {canvas,stats}=mockCanvas(),moves=[],r=new Renderer(canvas,{now:()=>t,reducedMotion:()=>false,onCamera:c=>moves.push(c)});
  r.load(withEdge());r.setCamera({yaw:0,pitch:0,zoom:1,pan:[0,0]});moves.length=0;
  r.animateTo({yaw:1,pitch:.5,zoom:2,pan:[0,0]});assert.equal(moves.length,0);
  t=125;flush();const mid=r.getCamera();assert.ok(mid.yaw>0&&mid.yaw<1);assert.equal(moves.length,0,'camera is not saved mid-animation');
  t=250;flush();near(r.getCamera().yaw,1);near(r.getCamera().zoom,2);assert.equal(moves.length,1,'camera saved once at the end');
  r.animateTo({yaw:0,pitch:0,zoom:1,pan:[0,0]});t=300;flush();const held=r.getCamera();
  down(stats,{button:0,clientX:5,clientY:5});assert.equal(r.anim,null);t=600;flush();near(r.getCamera().yaw,held.yaw);
  listener(stats,'pointerup')({pointerId:1,clientX:5,clientY:5});
  const instant=new Renderer(mockCanvas().canvas,{reducedMotion:()=>true});instant.load(withEdge());instant.animateTo({yaw:1,pitch:.5,zoom:2,pan:[0,0]});near(instant.getCamera().yaw,1);
  r.destroy();instant.destroy();
});
test('double-click frames the entity under the cursor, or fits when empty; Space frames the hover',()=>{
  let t=0;const {canvas,stats}=mockCanvas(),r=new Renderer(canvas,{now:()=>t,reducedMotion:()=>true});r.load(withEdge());r.setCamera(defaultCamera);
  const f=project([-.2,-.2,0],defaultCamera,640,480),before=r.getCamera();
  listener(stats,'dblclick')({clientX:f[0],clientY:f[1],preventDefault(){}});assert.ok(r.getCamera().zoom>before.zoom,'framing a small face zooms in');
  r.setCamera({...defaultCamera,zoom:30});listener(stats,'dblclick')({clientX:3,clientY:3,preventDefault(){}});assert.ok(r.getCamera().zoom<30,'empty double-click fits all');
  r.setCamera(defaultCamera);listener(stats,'pointermove')({pointerId:1,clientX:f[0],clientY:f[1],buttons:0});flush();
  const z=r.getCamera().zoom;listener(stats,'keydown')({key:' ',preventDefault(){},shiftKey:false});assert.ok(r.getCamera().zoom>z);r.destroy();
});
test('digit keys select standard views and Escape clears selection',()=>{
  const {canvas,stats}=mockCanvas(),r=new Renderer(canvas,{reducedMotion:()=>true}),key=k=>listener(stats,'keydown')({key:k,preventDefault(){},shiftKey:false});
  r.load(withEdge());for(const [k,name] of [['1','iso'],['2','front'],['3','back'],['4','top'],['5','bottom'],['6','right'],['7','left']]){key(k);const [yaw,pitch]=M.STANDARD_VIEWS[name];near(Math.cos(r.getCamera().yaw),Math.cos(yaw),1e-9);near(r.getCamera().pitch,pitch,1e-9);}
  r.destroy();
});
test('theme: light by default, dark available, validated, drives the background pass and survives context loss',()=>{
  const {canvas,stats,gl}=mockCanvas(),r=new Renderer(canvas);r.load(withEdge());flush();
  assert.deepEqual(r.getTheme(),M.defaultTheme());assert.notDeepEqual(M.darkTheme(),M.defaultTheme());
  r.setTheme(M.darkTheme());assert.deepEqual(r.getTheme(),M.darkTheme());
  assert.throws(()=>r.setTheme({background:{top:[2,0,0],bottom:[0,0,0]},line:[0,0,0],select:[0,0,0],hover:[0,0,0]}));
  assert.throws(()=>r.setTheme({line:[0,0,0]}));
  stats.listeners.get('webglcontextlost')({preventDefault(){}});stats.listeners.get('webglcontextrestored')();flush();assert.deepEqual(r.getTheme(),M.darkTheme());
  const before=stats.createdPrograms;r.destroy();assert.equal(stats.createdPrograms,stats.deletedPrograms);assert.ok(before>=2,'main and backdrop programs');
});
```

Also in the existing test at line ~300, replace the absolute `assert.equal(stats.createdBuffers,4)` with a baseline taken right after `new Renderer(canvas)` (`const base0=stats.createdBuffers` captured before `renderer.load`; assert `stats.createdBuffers-base0===4`). Justification for the commit message: the renderer now owns one constant backdrop buffer created at init; the assertion's intent (four geometry buffers after a section) is preserved.

- [ ] **Step 2: Run to verify they fail**

Run: `node tests/webgl_renderer_tests.mjs build-package/agent-3d-cad 2>&1 | grep -E "FAIL|Error" | head -5`
Expected: FAIL — no `dblclick` listener / `animateTo` is not a function.

- [ ] **Step 3: Implement** in `web/renderer.js`.

*Themes (module level):*

```js
  const rgb=(r,g,b)=>[r/255,g/255,b/255];
  const defaultTheme=()=>({background:{top:rgb(241,244,248),bottom:rgb(217,223,231)},line:rgb(107,118,131),select:rgb(26,115,232),hover:rgb(110,173,255)});
  const darkTheme=()=>({background:{top:rgb(46,53,61),bottom:rgb(25,30,36)},line:rgb(140,152,166),select:rgb(90,162,255),hover:rgb(140,190,255)});
  function themeValue(value) {
    const unit=v=>Array.isArray(v)&&v.length===3&&v.every(n=>Number.isFinite(n)&&n>=0&&n<=1);
    if(!value||!value.background||![value.background.top,value.background.bottom,value.line,value.select,value.hover].every(unit))fail('Invalid viewer theme.');
    return {background:{top:[...value.background.top],bottom:[...value.background.bottom]},line:[...value.line],select:[...value.select],hover:[...value.hover]};
  }
```

Export `defaultTheme,darkTheme` on `CadRenderer.math`.

*Shader:* add uniforms `uHover`, `uLine`, `uSelectColor`, `uHoverColor` (vec3). Fragment changes:

```glsl
bool selected=!uSection&&abs(vEntity-uSelected)<.25;
bool hovered=!uSection&&!selected&&abs(vEntity-uHover)<.25;
if(uLines){color=uSection?vec3(.94,.65,.25):selected?uSelectColor:hovered?uHoverColor:uLine;}
else {... vec3 base=uSection?vec3(.94,.55,.18):selected?mix(vColor,uSelectColor,.55):hovered?mix(vColor,uHoverColor,.35):vColor;
      color=base*(.58+.40*diffuse)+vec3(.08)*rim+vec3(.10)*spec;}
```

(`uEdgeMode` darker edge colour is dropped: lines use `uLine` in all modes; keep the uniform declared and set so existing uniform-name lists keep working.) Add the four new names to the uniform-location list in `_init`.

*Backdrop pass:* in `_init`, compile a second tiny program (`backdrop`): vertex `attribute vec2 aCorner; varying float vT; void main(){vT=aCorner.y*.5+.5;gl_Position=vec4(aCorner,1.,1.);}`; fragment `uniform vec3 uTop,uBottom; varying float vT; void main(){gl_FragColor=vec4(mix(uBottom,uTop,vT),1.);}` (use the same `webgl2?`-conditional `in/out` pattern as `program()`), plus a buffer of the triangle `[-1,-1, 3,-1, -1,3]`, stored as `resources.backdrop={program,buffer,uTop,uBottom,aCorner}`. In `_draw`, after `gl.clear(...)`, draw it with depth test disabled and depth mask off, then re-enable depth before the model passes. `destroy()` deletes the backdrop program and buffer; `_deleteBuffers` default key list is unchanged (the backdrop buffer is released explicitly in `destroy` and on context loss like the main program). On `webglcontextrestored`, `_init()` recreates it.

*Theme API:* `this.theme=defaultTheme()` in the constructor; `setTheme(v){return this._checked(()=>{this.theme=themeValue(v);this._schedule();});}`; `getTheme(){return themeValue(this.theme);}`. In `_draw` set `uLine`, `uSelectColor`, `uHoverColor`, `uHover` (face pass: `this.hover?.kind==='face'?faceNumbers.get(entity)??-1:-1`; edge pass: hovered range number, `-1` otherwise) and draw the hovered edge range at line width 2 like the selected one at 3. Replace `gl.clearColor(.075,.105,.135,1)` with the theme's bottom colour (the backdrop covers it).

*Input mapping:* in `_controls`:
- `pointerdown`: `this._cancelAnimation()`; `pan:` becomes `event.button===1||event.shiftKey` (button 0 and 2 orbit); keep `event.button>2` guard.
- `pointerup`: select on a click for **button 0 only** — `if(!drag.moved&&!drag.pan&&drag.button===0)` — so store `button:event.button` in the drag record.
- `wheel`: `this._cancelAnimation();` then `this.camera=camera(zoomAbout(this.camera,Math.exp(-delta*.001),event.clientX-rect.left,event.clientY-rect.top,width,height));this._cameraChanged();` (use `canvas.getBoundingClientRect()`; the mock reports `left:0,top:0`).
- `dblclick`: `event.preventDefault();` pick at the cursor in `auto` mode; if it hits, `this._frame(kind,id)`; else `this.fitAll()`.
- `keydown`: `' '` → `frameSelection()` for the hover (fall back to the selection); `'1'..'7'` → `setView(name,{animate:true})` using `['iso','front','back','top','bottom','right','left']`; remove the old `'0'`-only reset conflict (keep Home/0 → `fitAll()`); Escape unchanged.

*Animation:*

```js
    _cancelAnimation(){if(!this.anim)return;this.anim=null;this.callbacks.onCamera(this.getCamera());}
    animateTo(target,{duration=250}={}){return this._checked(()=>{
      const to=camera(target);
      if(duration<=0||this.reducedMotion()){this.anim=null;this.camera=to;this._cameraChanged();return;}
      this.anim={from:cloneCamera(this.camera),to,start:this.now(),duration};this._schedule();
    });}
    _stepAnimation(){
      const a=this.anim;if(!a)return;
      const t=(this.now()-a.start)/a.duration;
      if(t>=1){this.camera=cloneCamera(a.to);this.anim=null;this.callbacks.onCamera(this.getCamera());return;}
      this.camera=camera(lerpCamera(a.from,a.to,easeOut(t)));this._schedule();
    }
```

`_schedule` callback becomes `this.pending=null;this._stepAnimation();this._draw();`. `setCamera` and `reset` call `this.anim=null` first. `fitAll()` = `animateTo(fitCamera(this.camera,this.model.bounds,w,h))`; `_frame(kind,id)` = `animateTo(fitCamera(this.camera,entityBounds(this.model,kind,id),w,h))`; `frameSelection()` uses `this.hover||this.selection`. `setView(name,{animate=false}={})` animates through `animateTo({...this.camera,yaw,pitch})` when `animate` is true. Constructor: `this.now=now;this.reducedMotion=reducedMotion;`.

Defaults in the constructor options: `now=()=>globalThis.performance?.now?.()??Date.now()`, `reducedMotion=()=>!!globalThis.matchMedia?.('(prefers-reduced-motion: reduce)')?.matches`.

- [ ] **Step 4: Run all viewer Node tests**

Run: `node tests/webgl_renderer_tests.mjs build-package/agent-3d-cad 2>&1 | tail -4; node tests/live_ui_tests.mjs | tail -1; node tests/playback_ui_tests.mjs | tail -1; node tests/export_ui_tests.mjs | tail -1; node tests/artifact_retarget_ui_tests.mjs | tail -1; node tests/desktop_bridge_tests.mjs 2>&1 | tail -1; node tests/viewer_renderer_tests.js`
Expected: all pass.

- [ ] **Step 5: Commit**

```bash
git add web/renderer.js tests/webgl_renderer_tests.mjs
git commit -m "Add Shapr3D navigation mapping, camera animation, theme and highlights

The renderer owns one constant backdrop buffer, so the section-buffer test now
asserts four geometry buffers relative to a baseline instead of an absolute
count; its intent is unchanged.

Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 5: Orientation cube geometry in `shell.js`

**Files:**
- Modify: `web/shell.js`, `tests/viewer_shell_tests.mjs`

**Interfaces:**
- Consumes: `CadRenderer.math.viewFromDirection`, `.basis` via `globalThis.CadRenderer` (load `renderer.js` first in the test).
- Produces: `CUBE_FACES`, `cubeDirection(faceName,u,v)`, `cubeMatrix(camera)`, `cubeFaceMatrix(faceName,half)`.

- [ ] **Step 1: Write the failing tests** — in `tests/viewer_shell_tests.mjs`, load the renderer into the same context first (`vm.runInNewContext(fs.readFileSync('../web/renderer.js'),environment)` before shell.js with `requestAnimationFrame` stubs: `environment.requestAnimationFrame=()=>0;environment.cancelAnimationFrame=()=>{};`), then:

```js
const mul=(m,v)=>{const out=[0,0,0];for(let r=0;r<3;r++)out[r]=m[0*4+r]*v[0]+m[1*4+r]*v[1]+m[2*4+r]*v[2];return out;};
const rounded=v=>v.map(n=>Math.round(n*1e9)/1e9+0);
test('six cube faces with unit normals along distinct axes',()=>{
  assert.equal(Shell.CUBE_FACES.length,6);
  assert.deepEqual(Shell.CUBE_FACES.map(f=>f.name).sort(),['back','bottom','front','left','right','top']);
  for(const f of Shell.CUBE_FACES){near(Math.hypot(...f.normal),1);assert.equal(f.right.reduce((s,v,i)=>s+v*f.normal[i],0),0);assert.equal(f.up.reduce((s,v,i)=>s+v*f.normal[i],0),0);}
});
test('clicking a face centre looks at that face; borders add edge and corner views',()=>{
  assert.deepEqual(Shell.cubeDirection('top',.5,.5),[0,0,1]);
  assert.deepEqual(Shell.cubeDirection('front',.5,.5),[0,-1,0]);
  assert.deepEqual(Shell.cubeDirection('front',.95,.5),[1,-1,0]);
  assert.deepEqual(Shell.cubeDirection('front',.5,.05),[0,-1,1]);
  assert.deepEqual(Shell.cubeDirection('front',.95,.05),[1,-1,1]);
  assert.deepEqual(Shell.cubeDirection('top',.05,.95),[-1,-1,1]);
  assert.throws(()=>Shell.cubeDirection('side',.5,.5));
});
test('the cube matrix puts each face toward the viewer in its standard view, upright',()=>{
  const views={front:[0,0],back:[Math.PI,0],right:[-Math.PI/2,0],left:[Math.PI/2,0],top:[0,Math.PI/2],bottom:[0,-Math.PI/2]};
  for(const f of Shell.CUBE_FACES){
    const [yaw,pitch]=views[f.name],view=Shell.cubeMatrix({yaw,pitch,zoom:1,pan:[0,0]}),face=Shell.cubeFaceMatrix(f.name,40);
    const normal=rounded(mul(view,mul(face,[0,0,1]))),right=rounded(mul(view,mul(face,[1,0,0]))),down=rounded(mul(view,mul(face,[0,1,0])));
    assert.deepEqual(normal,[0,0,1],f.name+' normal faces the viewer');
    assert.deepEqual(right,[1,0,0],f.name+' text runs left to right');
    assert.deepEqual(down,[0,1,0],f.name+' text is upright');
  }
});
```

(`mul` here multiplies only the 3×3 part of a `matrix3d`; translation is ignored on purpose because it only checks orientation.)

- [ ] **Step 2: Run to verify it fails**

Run: `node tests/viewer_shell_tests.mjs 2>&1 | tail -4`
Expected: FAIL — `Shell.CUBE_FACES` is undefined.

- [ ] **Step 3: Implement** in `web/shell.js` (add before the `CadShell` freeze, and add the new names to the exported object):

```js
  const CUBE_FACES = Object.freeze([
    { name: 'front',  normal: [0, -1, 0], right: [1, 0, 0],  up: [0, 0, 1] },
    { name: 'back',   normal: [0, 1, 0],  right: [-1, 0, 0], up: [0, 0, 1] },
    { name: 'right',  normal: [1, 0, 0],  right: [0, 1, 0],  up: [0, 0, 1] },
    { name: 'left',   normal: [-1, 0, 0], right: [0, -1, 0], up: [0, 0, 1] },
    { name: 'top',    normal: [0, 0, 1],  right: [1, 0, 0],  up: [0, 1, 0] },
    { name: 'bottom', normal: [0, 0, -1], right: [1, 0, 0],  up: [0, -1, 0] }
  ]);
  const face = name => {
    const found = CUBE_FACES.find(f => f.name === name);
    if (!found) throw new Error('Unknown cube face.');
    return found;
  };
  const zone = value => (value < -.56 ? -1 : value > .56 ? 1 : 0);
  const cubeDirection = (name, u, v) => {
    const f = face(name), r = zone(2 * u - 1), up = zone(1 - 2 * v);
    return f.normal.map((n, i) => n + r * f.right[i] + up * f.up[i]);
  };
  // CSS matrix3d is column-major; the cube's local frame is world-aligned, so the
  // container transform is world -> CSS (x right, y down, z toward the viewer).
  const cubeMatrix = c => {
    const b = CadRenderer.math.basis(c);
    return [b[0][0], -b[1][0], -b[2][0], 0, b[0][1], -b[1][1], -b[2][1], 0, b[0][2], -b[1][2], -b[2][2], 0, 0, 0, 0, 1];
  };
  // A face square (x right, y down, z out of the face) placed `half` along its normal.
  const cubeFaceMatrix = (name, half) => {
    const f = face(name), down = f.up.map(v => -v);
    return [...f.right, 0, ...down, 0, ...f.normal, 0, ...f.normal.map(n => n * half), 1];
  };
```

- [ ] **Step 4: Run to verify it passes**

Run: `node tests/viewer_shell_tests.mjs`
Expected: `viewer shell: 5 checks passed`. If the "upright" assertion fails for one face, fix `up`/`right` in `CUBE_FACES` (the test is the specification of reading orientation), not the test.

- [ ] **Step 5: Commit**

```bash
git add web/shell.js tests/viewer_shell_tests.mjs
git commit -m "Add orientation cube geometry helpers

Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 6: Floating layout — markup, tokens, shell glue

**Files:**
- Rewrite: `web/viewer.html`, `web/styles.css`
- Modify: `web/shell.js` (DOM glue), `web/app.js` (wiring)

**Interfaces:**
- Consumes: `CadShell.resolveTheme/layoutMode/cube*`, renderer `setTheme/setView/animateTo/onHover`.
- Produces `CadShell.mount({$, renderer, bridge, update})` -> `{refresh(), setTheme(name), dispose()}` called from `app.js`; DOM contract below.

**DOM contract (existing IDs preserved; new IDs listed).** The section elements named below keep their current IDs and inner markup and are only moved: `#documents`, `#project-search`, `#refresh-library`, `#workspace-label`, `#open-workspace`, `#recent-workspaces`, `#features`, `#feature-count`, `#feature-heading-title`, `#parameters`, `#parameters-panel`, `#parts-panel` (+ children), `#presentation-panel`, `#appearance-panel`, `#annotations-panel`, `#presets-panel`, `#section-panel`, `#measurement-panel`, `#sequence-panel`, `#motion-panel`, `#artifact-review-panel`, `#export-format`, `#export-model`, `#save-image`, `#measurements`, `#reference`, `#reference-details`, `#selection-name`, `#selection-help`, `#selection-badge`, `#clear-selection`, `#prompt`, `#include-capture`, `#capture-option`, `#send`, `#copy-request`, `#edit-status`, `#edit-help`, `#copy-fallback`, `#copy-text`, `#fullscreen`, `#connection`, `#document-title`, `#revision`, `#model-facts`, `#update-status`, `#dimensions`, `#viewport`, `#annotation-overlay`, `#empty-state`, `#all-hidden`, `#restore-parts`, `#artifact-semantic-empty`, `#loading`, `#view-error`, `#fit` (kept as the hidden fit action for tests), plus all `*-panel` children.

New IDs: `#project-pill`, `#models-popover`, `#scene-card`, `#scene-chip`, `#scene-tabs`, `#tool-dock`, `#tool-popover`, `#cube`, `#selection-bar`, `#selection-summary`, `#details-toggle`, `#details-panel`, `#export-menu-button`, `#export-menu`, `#toast`.

- [ ] **Step 1: `viewer.html`.** Keep the `<meta>` CSP line, the `<title>`, and the script line from Task 1. Structure (`<body data-theme="light">`):
  `<div id="app">` containing, in order: `<canvas id="viewport">`, `#annotation-overlay`, the overlay states (`#empty-state`, `#all-hidden`, `#artifact-semantic-empty`, `#loading`, `#view-error`), `<header class="chrome-top">` (`#project-pill` button with the diamond mark, `#document-title`, `#revision`, `#connection` dot, chevron; `#export-menu-button`; `#fullscreen`), `#scene-card` (tab buttons Features/Parts, `#features`, `#parts-panel`, `#parameters-panel`) with `#scene-chip`, `#models-popover` (library section: `#project-search`, `#documents`, workspace controls), `#tool-dock` (`<nav role="toolbar" aria-orientation="vertical">` one `<button data-tool="<panel id>">` per tool panel with inline-SVG icon and `aria-label`), `#tool-popover` (empty container the shell moves the active panel into), `<div id="cube">`, `#selection-bar` (summary, `#details-toggle`, `#prompt`, `#send`, `#copy-request`, `#clear-selection`, `#edit-status`, `#details-panel` holding `#measurements`, `#reference-details`, `#capture-option`, `#edit-help`, `#copy-fallback`), `#export-menu` (`#export-format`, `#export-model`, `#save-image`), `#toast`, hidden `#fit`, `#model-facts`, `#update-status`, `#dimensions`, `#selection-name`, `#selection-help`, `#selection-badge`.
  Tool panels are authored once inside `<div id="tool-panels" hidden-container>` in the existing order; the shell moves the active one into `#tool-popover`.

- [ ] **Step 2: `styles.css`.** Define `:root` light tokens and `:root[data-theme=dark]` overrides:

```css
:root{color-scheme:light;--bg:#e8ecf1;--surface:#fff;--surface-2:#f1f3f6;--hairline:#dde2e8;--text:#1d2630;--text-2:#5f6b78;--accent:#1a73e8;--accent-soft:#eaf2fe;--shadow:0 2px 10px rgba(28,39,51,.18);--radius:12px;--pill:999px;font:13px/1.45 -apple-system,BlinkMacSystemFont,"Segoe UI",sans-serif}
:root[data-theme=dark]{color-scheme:dark;--bg:#191e24;--surface:#2a3037;--surface-2:#343c45;--hairline:#3b434c;--text:#e9eef3;--text-2:#a3afbb;--accent:#6eadff;--accent-soft:#1f3a5e;--shadow:0 3px 12px rgba(0,0,0,.45)}
```

  Rules: `body{margin:0;background:var(--bg);color:var(--text);min-width:360px}`; `#app{position:relative;height:100vh;min-height:360px;overflow:hidden}`; `#viewport{position:absolute;inset:0;width:100%;height:100%}`; every floating surface uses `background:var(--surface);border:1px solid var(--hairline);box-shadow:var(--shadow)` with `backdrop-filter:blur(8px)` guarded by `@supports`; pills `border-radius:var(--pill)`, cards `var(--radius)`; focus ring `outline:2px solid var(--accent)`; secondary text uses `--text-2`; `@media(prefers-reduced-motion:reduce){*{transition:none!important;animation:none!important}}`; `[hidden]{display:none!important}`; layout regions per the spec table with `@container`-free media queries at 900px and 560px matching `layoutMode`; the cube is a 64px `perspective:240px` box with `transform-style:preserve-3d` faces; legacy `.muted`, `.fine-print`, `dl/dt/dd`, `.primary`, `.quiet`, `.count`, `.section-heading`, `.part-control`, `.joint-control`, `.annotation-pin`, `.color-row`, `.view-buttons`, `.note-actions` classes that the unchanged section markup and `app.js` generate keep working with the new tokens (carry over their rules, replacing literal dark colours by tokens).

- [ ] **Step 3: `shell.js` glue** — `mount(options)`:
  - sets `document.documentElement.dataset.theme` from `resolveTheme(bridge.hostContext, matchMedia('(prefers-color-scheme: dark)').matches)` at mount and on `bridge.on('ui/notifications/host-context-changed')` and on the media-query `change` event; calls `renderer.setTheme(name==='dark'?CadRenderer.math.darkTheme():CadRenderer.math.defaultTheme())` whenever the theme or the renderer changes;
  - dock: one button per `[data-tool]`; `refresh()` shows a button iff its panel is not `hidden`; clicking toggles `#tool-popover` (one at a time), moves the panel in, sets `aria-expanded`, Esc/outside-click closes and returns focus; arrow keys move between dock buttons; below 560px the dock collapses behind a single `Tools` button;
  - Scene card: tabs (Features/Parts; Parts hidden unless `#parts-panel` is not hidden), chip/open state from `layoutMode` on a `ResizeObserver` of `#app`;
  - Models popover from `#project-pill`; Export menu from `#export-menu-button`; toast mirrors `#update-status` changes (`aria-live` on the toast);
  - selection bar: expanded/idle state from `renderer.selection`, `Details` toggle (`aria-expanded`), summary text composed from the existing `#selection-name`/`#selection-badge`/first measurement row;
  - cube: builds six `<button>` faces (`aria-label="Top view"` etc.) as children of a preserve-3d wrapper; each frame (`requestAnimationFrame` loop only while the camera changes: subscribe via the renderer's `onCamera` chain from `app.js`) sets `wrapper.style.transform=matrix3d(cubeMatrix(camera))`; face button `click` computes `(u,v)` from the pointer position within the face rect and calls `renderer.animateTo({...camera,...viewFromDirection(cubeDirection(face,u,v))})`; `dblclick` on the cube animates to `STANDARD_VIEWS.iso`; dragging the cube orbits the camera (pointer events, `setCamera`);
  - returns `{refresh,setTheme,dispose}`; `dispose` removes every listener and observer.

- [ ] **Step 4: `app.js` wiring.** Remove the `mode-face`/`mode-edge` handlers and the `renderer?.setMode(mode)` call sites (lines ~53-54, ~221, ~588) and the `Meshes/Curves` text changes; pass `onHover` (no-op) to the renderer constructor; construct the shell after the renderer (`shell=CadShell.mount({$,renderer,bridge})`), call `shell.refresh()` at the end of `update()`; route the old Fit/Iso/Top/Front/Right button handlers (if present) to `renderer.fitAll()` / `setView(name,{animate:true})` via the hidden `#fit`; keep every export code path in this file untouched.

- [ ] **Step 5: Static checks**

Run: `node --check web/app.js && node --check web/shell.js && node --check web/renderer.js && node tests/export_ui_tests.mjs | tail -1 && node tests/viewer_shell_tests.mjs | tail -1`
Expected: no syntax errors; `PASS 110 ...`; `viewer shell: 5 checks passed`.

- [ ] **Step 6: Commit**

```bash
git add web/viewer.html web/styles.css web/shell.js web/app.js
git commit -m "Rebuild viewer layout as floating cards, tool dock, selection bar and cube

Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 7: Dev harness, visual verification and per-panel checklist

**Files:**
- Create: `tests/viewer_shell_harness/serve.mjs`, `tests/viewer_shell_harness/mock-bridge.js`, `tests/viewer_shell_harness/README.md`

**Interfaces:** the harness serves the **assembled** viewer (same placeholder substitution as `cmake/EmbedViewerApp.cmake`) at `http://127.0.0.1:<port>/` with a mock host that answers `ui/initialize`, `tools/call` (`cad_viewer` sync/mesh/context, `cad_list`) from a built-in box-with-hole payload and an assembly payload, and can switch `?theme=dark` and `?mode=assembly|artifact`.

- [ ] **Step 1: Build the harness.** `serve.mjs` (Node, no dependencies): reads `web/viewer.html`, substitutes `@VIEWER_*@` with the five/six asset files (CRLF -> LF), strips the CSP meta (as `desktop/build.rs` does), injects `mock-bridge.js` before the viewer scripts, and listens on `127.0.0.1` with port from `argv[2]` (default 0, print the chosen port). `mock-bridge.js` defines a fake parent that answers JSON-RPC over `postMessage` with the minimal payloads the viewer needs (copy the payload shape from `tests/live_ui_tests.mjs` `payload()`/`ready()` and extend the mesh with a unit box plus one hole polyline so there are faces and edges).

- [ ] **Step 2: Screenshots.** Start the server, open it in the built-in browser, and capture 360, 560, 900 and 1280 px wide in light and dark (`resize_window`), plus `?mode=assembly` at 1280. Save under `.superpowers/` (untracked) or the scratchpad; do not commit images.

- [ ] **Step 3: Per-panel checklist** (each verified in the harness; record pass/fail in `HANDOFF.md`): Models popover opens the library and search filters; Features and Parameters visible in the Scene card; Parts tab appears for the assembly and hide/isolate/show-all work; Visual inspection clip + explode; Colors; Review notes add/save/delete; Saved views; Exact section; Measurement; Sequences; Motion appears only for articulated payloads; Source appears only in `?mode=artifact`; Export menu lists all formats and the Export button enables on a ready revision; Save PNG enables; Expand shows only when the host offers fullscreen; selection bar idle -> selected -> Details -> Send/Copy request -> clear; hover highlight on face and edge; click selects the right kind with no toggle; right-drag orbit, middle/Shift pan, wheel zoom to cursor, double-click frame/fit, Space frame, digits 1-7; cube face/edge/corner clicks, drag, double-click; dark switch via `?theme=dark` and via a `host-context-changed` message; no horizontal scroll at 360px; keyboard-only path reaches dock, popovers, selection bar and cube buttons.

- [ ] **Step 4: Fix every defect found, re-run the Node suites, and commit**

```bash
node tests/webgl_renderer_tests.mjs build-package/agent-3d-cad | tail -1 && node tests/viewer_shell_tests.mjs | tail -1
git add tests/viewer_shell_harness web
git commit -m "Add dev-only viewer shell harness and fix issues found in review

Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 8: Hover timing, full test run, docs and handoff

**Files:**
- Modify: `docs/LIVE_VIEWER.md`, `docs/HANDOFF.md`
- Test: full CTest

- [ ] **Step 1: Measure hover pick time.** In a scratch script under the scratchpad (not the repo), load `web/renderer.js` with the largest example (`examples/` assembly evaluated through `build-package/agent-3d-cad` the way `webgl_renderer_tests.mjs`'s native section does) and time 200 `pick(...,'auto')` calls at spread cursor positions; print mean/p95/max in ms. If p95 exceeds 8 ms, change `_scheduleHover` to run after an 80 ms idle debounce (store the timer in `hoverPending`; cancel in `destroy`) and re-run the Task 3 hover tests with `flush()` plus a fake timer.

- [ ] **Step 2: Full verification**

Run: `cmake --build build-package --parallel 4 2>&1 | tail -3 && ctest --test-dir build-package --output-on-failure -j4 2>&1 | tail -25`
Expected: all tests pass (record the exact summary line). If a test unrelated to the viewer is already failing on `main`, confirm with `git stash`-free means (`git worktree`-less: run the same test on the base commit's build output) before attributing it.

- [ ] **Step 3: Docs.** `docs/LIVE_VIEWER.md`: replace "Drag to orbit, Shift/right drag to pan, wheel to zoom, and use the view buttons..." with the new mapping table, document unified selection (no Faces/Edges toggle, 6px edge priority, hover), the orientation cube, digit shortcuts, theme behaviour, and the dock/popover layout. `docs/HANDOFF.md`: add a section for this increment — what changed, exact test evidence (counts from Step 2), harness screenshots viewed and which widths/themes, hover timing numbers, the changed canonical viewer hash (record `build-package` value; no cross-platform claim), known limitations (perspective, multi-select, touch, real-host rendering not yet verified unless done), and next tasks (sub-projects B and C). `docs/PROTOCOL.md` is explicitly unchanged.

- [ ] **Step 4: Commit**

```bash
git add docs/LIVE_VIEWER.md docs/HANDOFF.md
git commit -m "Document the Shapr3D-style viewer shell and record verification

Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

## Self-Review

**Spec coverage:** layout mapping and dock/popovers/Scene card/selection bar/Models/Export (Task 6, checklist in 7); responsive table (Task 1 `layoutMode`, Task 6 CSS); accessibility (Task 6 glue, checked in 7); unified selection, hover, `kind`, artifact rule (Task 3, artifact path shares `pick` via `model.geometries`); navigation mapping, zoom-to-cursor, double-click, Space, digits (Task 4); orientation cube (Tasks 5-6); transitions and save-once (Task 4); theme tokens, host/OS resolution, GL gradient, uniforms, blended selected face, stored colour untouched (Tasks 1, 4, 6); embed pipeline files and hash (Task 1, Task 8); performance (Task 8); docs/HANDOFF (Task 8); done-when 1-6 map to Tasks 3/4, 7, 7, 4-6, 8, 8.

**Known gaps to resolve while building, not deferred:** the host-context `theme` field must be confirmed against the MCP Apps spec text (Task 6 Step 3 uses `bridge.hostContext.theme`; the OS fallback is kept either way).

**Type consistency:** `viewFromDirection`, `STANDARD_VIEWS`, `fitCamera`, `entityBounds`, `zoomAbout`, `lerpCamera`, `easeOut` are defined in Task 2 and consumed in Tasks 4-6 with the same names; `defaultTheme`/`darkTheme` defined in Task 4 and consumed in Task 6; `hover` shape `{kind,entity_id}` identical in Tasks 3 and 4; `anim` field named consistently.
