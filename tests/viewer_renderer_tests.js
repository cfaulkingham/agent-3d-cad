// Pure renderer unit tests; no browser or product runtime dependencies.
const fs=require('fs'), vm=require('vm'), assert=require('assert'), path=require('path');
const source=fs.readFileSync(path.join(__dirname,'../src/viewer.cpp'),'utf8');
const pure=source.split('// BEGIN PURE RENDERER')[1].split('// END PURE RENDERER')[0];
const {rasterize,edgePick,renderEdges,valid,clipSegment}=vm.runInNewContext(pure+';({rasterize,edgePick,renderEdges,valid,clipSegment})');
let checks=0;
function test(ok,message){assert.ok(ok,message);checks++;}
function mesh(points,triangles,faces){return {positions:points,triangles,triangle_faces:faces};}
// The large triangle's average depth is far away but its depth at this pick
// is nearer than the small triangle. Average-depth painter order picks wrong.
const points=[[0,0,0],[20,0,30],[0,20,30],[1,1,8],[5,1,8],[1,5,8]];
const m=mesh(points,[[0,1,2],[3,4,5]],['near','far']);
let frame=rasterize(m,points,24,24,null);
test(frame.triangles[2*24+2]===0,'interpolated depth chooses actual nearest triangle');
const degenerate=[[0,0,0],[0,1,0],[0,2,0]];
frame=rasterize(mesh(degenerate,[[0,1,2]],['line']),degenerate,8,8,null);
test(frame.triangles.every(x=>x===-1),'zero projected area never becomes a selectable face');
const flat=[[0,0,5],[20,0,5],[0,20,5]];
frame=rasterize(mesh(flat,[[0,1,2]],['face']),flat,24,24,null);
let edges=[{id:'hidden',points:[[2,4,10],[10,4,10]]},{id:'visible',points:[[2,4,4],[10,4,4]]}];
test(edgePick([5,4],edges,frame,1e-6).id==='visible','coincident screen edges select nearest visible edge');
test(edgePick([5,4],edges.slice(0,1),frame,1e-6).id===null,'occluded edge is not selectable');
const before=frame.pixels.slice();renderEdges(edges.slice(0,1),frame,null,true,1e-6);
test(frame.pixels.every((x,i)=>x===before[i]),'occluded edge is not painted through a surface');
edges=[{id:'a',points:[[2,4,4],[10,4,4]]},{id:'b',points:[[2,4,4],[10,4,4]]}];
test(edgePick([5,4],edges,frame,1e-6).ambiguous,'indistinguishable coincident edges report ambiguity');
const overlap=flat.concat(flat);frame=rasterize(mesh(overlap,[[0,1,2],[3,4,5]],['a','b']),overlap,24,24,null);
test(frame.triangles[2*24+2]===-2,'coincident faces report ambiguity');
frame=rasterize(mesh(overlap,[[0,1,2],[3,4,5]],['a','a']),overlap,24,24,null);
test(frame.triangles[2*24+2]>=0,'triangles of the same B-rep face are not ambiguous');
let d={schema_version:1,summary:{bounds_mm:{min:[0,0,0],max:[20,20,20]}},mesh:{...mesh(flat,[[0,1,2]],['face-1']),edges:[{id:'edge-1',points:[[0,0,0],[1,0,0]]}]},topology:{faces:[{id:'face-1'}],edges:[{id:'edge-1'}]}};
test(valid(d)===d,'valid payload retains exact mesh identities');
let bad=JSON.parse(JSON.stringify(d));bad.mesh.edges[0].points[1]=[1e100,0,0];assert.throws(()=>valid(bad));checks++;
bad=JSON.parse(JSON.stringify(d));bad.mesh.triangle_faces[0]='face-999';assert.throws(()=>valid(bad));checks++;
bad=JSON.parse(JSON.stringify(d));bad.mesh.edges[0].points[1]=[100,0,0];assert.throws(()=>valid(bad));checks++;
const clipped=clipSegment([-1e6,4,0],[1e6,4,0],24,24);
test(clipped&&clipped[0][0]>=0&&clipped[1][0]<=23.00001,'edge samples are clipped to viewport');
renderEdges([{id:'huge',points:[[-1e6,4,0],[1e6,4,0]]}],frame,null,true,1e-7);checks++;
assert.throws(()=>rasterize(m,points,100000,100000,null));checks++;
console.log(`viewer renderer: ${checks} checks passed`);
