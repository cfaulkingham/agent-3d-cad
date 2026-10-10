#!/usr/bin/env python3
"""Developer-only independent validation of native curve protocol contracts."""
import copy
import json
import math
from pathlib import Path
import subprocess
import sys
import tempfile
from jsonschema import Draft202012Validator
exe=str(Path(sys.argv[1]).resolve())
raw=json.loads(subprocess.check_output([exe,"tools"],text=True))
catalog=raw["tools"] if isinstance(raw,dict) else raw
tools={t["name"]:t for t in catalog}
checks=0
for tool in catalog:
    for kind in ("inputSchema","outputSchema"):
        Draft202012Validator.check_schema(tool[kind]);checks+=1

def validate(tool,kind,value):
    global checks
    Draft202012Validator(tools[tool][kind]).validate(value);checks+=1

def run(tool,args,workspace,error=None):
    validate(tool,"inputSchema",args)
    p=subprocess.run([exe,"call",tool,"--workspace",str(workspace),"--input","-"],input=json.dumps(args),capture_output=True,text=True)
    if error:
        assert p.returncode and json.loads(p.stderr)["error"]["code"]==error,p.stderr
        return
    assert p.returncode==0,p.stderr
    value=json.loads(p.stdout);validate(tool,"outputSchema",value);return value

def model(features):
    return dict(schema_version=1,units="mm",parameters={},features=features,output=features[-1]["id"])
frame=dict(origin=[0,0,0],normal=[0,0,1],x_direction=[1,0,0])
curve=dict(id="curve",type="curve",path=dict(type="wire",segments=[dict(type="bezier",points=[[2,0,0],[2,2,0],[0,2,0]],weights=[1,math.sqrt(.5),1])]))
fixtures={
 "helix":model([dict(id="helix",type="curve_helix",frame=frame,radius=2,pitch=3,turns=1.5)]),
 "weighted":model([curve]),
 "trim":model([curve,dict(id="trim",type="curve_trim",input="curve",start=.2,end=.8)]),
 "tangent":model([curve,dict(id="line",type="curve_tangent_line",input="curve",position=.5,length=2)]),
 "arc":model([dict(id="curve",type="curve",path=dict(type="wire",segments=[dict(type="line",start=[0,0,0],end=[2,0,0])])),dict(id="arc",type="curve_tangent_arc",input="curve",position=1,end=[3,1,0])])
}
circle=dict(id="left",type="sketch",workplane=frame,profile=dict(type="circle",radius=1))
right=copy.deepcopy(circle);right["id"]="right";right["workplane"]["origin"]=[4,0,0]
rectangle=dict(id="rectangle",type="sketch",workplane=frame,profile=dict(type="rectangle",width=10,height=4))
selector=dict(type="geometric",feature_id="rectangle",curve_kind="line",expected_count=1,center=dict(point=[10,2,0],tolerance=1e-6))
extrude=lambda name:dict(id="body",type="extrude",input=name,distance=1)
sketches={
 "hull":model([circle,right,dict(id="hull",type="sketch_hull",inputs=["left","right"],workplane=frame),extrude("hull")]),
 "trace":model([curve,dict(id="trace",type="sketch_trace",input="curve",workplane=frame,width=.4),extrude("trace")]),
 "round":model([rectangle,dict(id="round",type="sketch_full_round",input="rectangle",edges=selector),extrude("round")])
}
with tempfile.TemporaryDirectory(prefix="cad-curves-schema-") as temp:
    for name,document in fixtures.items():
        run("cad_create",dict(document_id=name,model=document),temp)
        run("cad_query",dict(document_id=name,revision=1,kind="curve",curve=dict(stations=[0,.5,1])),temp)
    for name,document in sketches.items():
        run("cad_create",dict(document_id=name,model=document),temp)
        run("cad_query",dict(document_id=name,revision=1,kind="topology"),temp)
    before=run("cad_read",dict(document_id="arc"),temp)
    bad=copy.deepcopy(fixtures["arc"]["features"][-1]);bad["end"]=[3,0,0]
    run("cad_apply",dict(document_id="arc",expected_revision=1,operations=[dict(op="replace_feature",id="arc",feature=bad)]),temp,"invalid_model")
    assert run("cad_read",dict(document_id="arc"),temp)==before;checks+=1
for args in [dict(document_id="bad",revision=1,kind="curve",curve=dict(stations=[1.1])),dict(document_id="bad",revision=1,kind="curve",curve=dict(stations=[])),dict(document_id="bad",revision=1,kind="topology",curve=dict(stations=[0]))]:
    assert not Draft202012Validator(tools["cad_query"]["inputSchema"]).is_valid(args);checks+=1
bad=copy.deepcopy(fixtures["weighted"]);bad["features"][0]["path"]["segments"][0]["weights"]=[1]
assert not Draft202012Validator(tools["cad_create"]["inputSchema"]).is_valid(dict(document_id="bad",model=bad));checks+=1
print(f"{checks} independent curve schema checks; compact tools wrapper {len(json.dumps(dict(tools=catalog),separators=(',',':'),ensure_ascii=False).encode())} bytes")
