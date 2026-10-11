#!/usr/bin/env python3
"""Independent Draft2020-12 actual-call proof for solved tangencies and native curved hulls."""
import copy
import json
import math
from pathlib import Path
import subprocess
import sys
import tempfile
import time
from jsonschema import Draft202012Validator
exe=str(Path(sys.argv[1]).resolve())
raw=json.loads(subprocess.check_output([exe,"tools"],text=True))
catalog=raw["tools"] if isinstance(raw,dict) else raw
tools={t["name"]:t for t in catalog}
checks=0
for tool in catalog:
    for kind in ("inputSchema","outputSchema"):
        Draft202012Validator.check_schema(tool[kind]);checks+=1

def run(tool,args,workspace,error=None):
    global checks
    Draft202012Validator(tools[tool]["inputSchema"]).validate(args);checks+=1
    result=subprocess.run([exe,"call",tool,"--workspace",str(workspace),"--input","-"],input=json.dumps(args),capture_output=True,text=True,timeout=40)
    if error:
        assert result.returncode and json.loads(result.stderr)["error"]["code"]==error,result.stderr
        checks+=1;return
    assert result.returncode==0,result.stderr
    value=json.loads(result.stdout);Draft202012Validator(tools[tool]["outputSchema"]).validate(value);checks+=1;return value

def near(a,b,tol=1e-6):
    global checks
    assert abs(a-b)<tol,(a,b);checks+=1

def model(features):return dict(schema_version=1,units="mm",parameters={},features=features,output=features[-1]["id"])
frame=dict(origin=[0,0,0],normal=[0,0,1],x_direction=[1,0,0])
def curve(name,segments):return dict(id=name,type="curve",path=dict(type="wire",segments=segments))
def line(name,a,b):return curve(name,[dict(type="line",start=a,end=b)])
def edge(name,kind):return dict(type="geometric",feature_id=name,curve_kind=kind,expected_count=1)
def solution(p,tolerance=1e-6):return dict(point=p,tolerance=tolerance)
circle=dict(id="circle",type="sketch",workplane=frame,profile=dict(type="circle",radius=1))
line_model=model([circle,dict(id="part",type="curve_constrained_line",workplane=frame,constraints=[dict(edge=edge("circle","circle")),dict(point=[2,0,0])],solution=solution([1.25,math.sqrt(3)/4,0]))])
arc_model=model([line("x",[-2,0,0],[2,0,0]),line("y",[0,-2,0],[0,2,0]),dict(id="part",type="curve_constrained_arc",workplane=frame,constraints=[dict(edge=edge("x","line")),dict(edge=edge("y","line"))],radius=.5,solution=solution([.5-.5/math.sqrt(2)]*2+[0]))])
weighted=curve("source",[dict(type="bezier",points=[[2,0,0],[2,2,0],[0,2,0]],weights=[1,math.sqrt(.5),1])])
hull_model=model([weighted,dict(id="hull",type="sketch_hull",inputs=["source"],workplane=frame,contact_tolerance=1e-5),dict(id="body",type="extrude",input="hull",distance=1)])
round_model=model([dict(id="profile",type="sketch",workplane=frame,profile=dict(type="rectangle",width=10,height=4)),dict(id="rounded",type="sketch_full_round",input="profile",edges={**edge("profile","line"),"center":dict(point=[10,2,0],tolerance=1e-6)},invert=True),dict(id="body",type="extrude",input="rounded",distance=1)])
fixtures=dict(line=line_model,arc=arc_model,hull=hull_model,invert=round_model)
with tempfile.TemporaryDirectory(prefix="cad-curve-completion-schema-") as temp:
    for name,document in fixtures.items():
        receipt=run("cad_create",dict(document_id=name,model=document),temp)
        assert set(receipt)=={"schema_version","document_id","revision","kernel_version","model_sha256","summary"};checks+=1
        saved=run("cad_read",dict(document_id=name,revision=receipt["revision"]),temp)
        assert saved["model"]==document;checks+=1
        report=run("cad_query",dict(document_id=name,revision=1,kind="curve" if name in ("line","arc") else "topology"),temp)
        if name in ("line","arc"):near(report["curve"]["length_mm"],math.sqrt(3) if name=="line" else math.pi/4)
        else:near(receipt["summary"]["volume_mm3"],math.pi-2 if name=="hull" else 32-2*math.pi)
    before=run("cad_read",dict(document_id="line"),temp)
    ambiguous=copy.deepcopy(line_model["features"][-1]);ambiguous["solution"]["tolerance"]=10
    run("cad_apply",dict(document_id="line",expected_revision=1,operations=[dict(op="replace_feature",id="part",feature=ambiguous)]),temp,"selection_ambiguous")
    assert run("cad_read",dict(document_id="line"),temp)==before;checks+=1
    lower=copy.deepcopy(line_model["features"][-1]);lower["solution"]["point"][1]*=-1
    edit=run("cad_apply",dict(document_id="line",expected_revision=1,operations=[dict(op="replace_feature",id="part",feature=lower)]),temp)
    lower_source=run("cad_read",dict(document_id="line",revision=edit["revision"]),temp)
    assert lower_source["model"]["features"][-1]==lower;checks+=1
    near(run("cad_query",dict(document_id="line",revision=2,kind="curve",curve=dict(stations=[0])),temp)["curve"]["samples"][0]["point_mm"][1],-math.sqrt(3)/2)
    run("cad_job",dict(action="submit",request_id="solved-curve",tool="cad_query",arguments=dict(document_id="line",revision=2,kind="curve")),temp)
    for attempt in range(200):
        job=run("cad_job",dict(action="get",job_id="solved-curve"),temp)
        if job["state"] in ("succeeded","failed"):break
        time.sleep(.025)
    assert job["state"]=="succeeded",job;checks+=1
    near(job["result"]["curve"]["length_mm"],math.sqrt(3))
    # Invalid finite-edge contacts reject atomically through the native worker.
    invalid=copy.deepcopy(arc_model["features"][0]);invalid["path"]["segments"][0]["end"]=[0,0,0]
    prior=run("cad_read",dict(document_id="arc"),temp)
    run("cad_apply",dict(document_id="arc",expected_revision=1,operations=[dict(op="replace_feature",id="x",feature=invalid)]),temp,"selection_missing")
    assert run("cad_read",dict(document_id="arc"),temp)==prior;checks+=1
# Closed schemas retain all feature and selector constraints.
for kind,field,value in [("line","unknown",1),("line","constraints",[]),("line","solution",dict(point=[0,0,0],tolerance=1e-6,index=1)),("arc","radius",None),("invert","invert","yes"),("hull","contact_tolerance",False)]:
    document=copy.deepcopy(fixtures[kind]);index=-1 if kind in ("line","arc") else 1
    document["features"][index][field]=value
    assert not Draft202012Validator(tools["cad_create"]["inputSchema"]).is_valid(dict(document_id="invalid",model=document)),(kind,field);checks+=1
size=len(json.dumps(dict(tools=catalog),separators=(',',':'),ensure_ascii=False).encode())
assert size<=476160;checks+=1
print(f"{checks} independent curve completion schema checks; compact tools wrapper {size} bytes")
