#!/usr/bin/env python3
"""Independent Draft 2020-12 validation of real native surface/curve calls.

Developer test only; the product has no Python or jsonschema dependency.
"""
import copy
import json
import math
from pathlib import Path
import subprocess
import sys
import tempfile
from jsonschema import Draft202012Validator

exe = str(Path(sys.argv[1]).resolve())
raw = subprocess.check_output([exe, "tools"], text=True)
catalog = json.loads(raw)
tools = {item["name"]: item for item in (catalog["tools"] if isinstance(catalog, dict) else catalog)}
checks = 0
for tool in tools.values():
    for kind in ("inputSchema", "outputSchema"):
        Draft202012Validator.check_schema(tool[kind])
        checks += 1

def validate(tool, kind, value):
    global checks
    Draft202012Validator(tools[tool][kind]).validate(value)
    checks += 1

def run(tool, args, root, error=None):
    validate(tool, "inputSchema", args)
    p = subprocess.run([exe, "call", tool, "--workspace", str(root), "--input", "-"],
                       input=json.dumps(args), text=True, capture_output=True)
    if error:
        assert p.returncode, p.stdout
        value = json.loads(p.stderr)
        assert value["error"]["code"] == error, value
    else:
        assert p.returncode == 0, p.stderr
        value = json.loads(p.stdout)
        validate(tool, "outputSchema", value)
    return value

def line(a, b):
    return dict(type="line", start=a, end=b)

def loop(points):
    return [line(p, points[(i+1) % len(points)]) for i, p in enumerate(points)]

def model(features):
    return dict(schema_version=1, units="mm", parameters={}, features=features, output=features[-1]["id"])

patch = dict(id="patch", type="surface_bezier", control_points=[[[0, 0, 0], [0, 20, 0]], [[10, 0, 0], [10, 20, 0]]])
cylinder = dict(id="patch", type="surface_bezier", control_points=[[[10,0,0],[10,0,5]],[[10,10,0],[10,10,5]],[[0,10,0],[0,10,5]]], weights=[[1,1],[math.sqrt(.5)]*2,[1,1]])
face = dict(type="geometric", feature_id="patch", surface_kind="bezier", expected_count=1)
fixtures = {
    "trim": model([patch, dict(id="trimmed", type="surface_trim", input="patch", boundary=loop([[.1,.1],[.9,.1],[.9,.9],[.1,.9]]), holes=[loop([[.4,.4],[.6,.4],[.6,.6],[.4,.6]])])]),
    "fill": model([dict(id="fill", type="surface_fill", tolerance=1e-5, boundaries=[dict(curve=c, continuity="C0") for c in loop([[0,0,0],[10,0,0],[10,10,0],[0,10,0]])])]),
    "network": model([dict(id="network", type="surface_gordon", u_curves=[line([0,y,0],[10,y,0]) for y in [0,10]], v_curves=[line([x,0,0],[x,10,0]) for x in [0,10]], u_parameters=[0,1], v_parameters=[0,1], tolerance=1e-6)]),
    "curve": model([dict(id="curve", type="curve", path=dict(type="wire", segments=[line([0,0,0],[3,4,0])]))]),
    "project": model([cylinder, dict(id="curve", type="curve", path=dict(type="wire", segments=[line([15,2,2],[15,8,2])])), dict(id="projected", type="curve_project", input="curve", target="patch", faces=face, direction=[-1,0,0])]),
}
# Geometric networks deliberately vary curve parameterization; callers may omit
# stations and let the native solver discover crossings on the original curves.
def network(profiles, guides, **options):
    return dict(id="network", type="surface_gordon", u_curves=profiles, v_curves=guides,
                tolerance=1e-6, **options)

def ring(radius, z=None):
    def p(x, y): return [x, y] if z is None else [x, y, z]
    return [dict(type="arc", start=p(radius,0), mid=p(0,radius), end=p(-radius,0)),
            dict(type="arc", start=p(-radius,0), mid=p(0,-radius), end=p(radius,0))]

fixtures["network_general"] = model([network(
    [line([0,0,0],[10,0,0]), dict(type="bezier", points=[[0,5,0],[2,5,0],[10,5,0]]), line([0,10,0],[10,10,0])],
    [line([x,0,0],[x,10,0]) for x in (0,5,10)], u_parameters=[0,.5,1], v_parameters=[0,.5,1])])
quarter_guides = [line([10*math.cos(a),10*math.sin(a),0], [10*math.cos(a),10*math.sin(a),5]) for a in (0,math.pi/4,math.pi/2)]
fixtures["network_arc"] = model([network([dict(type="arc", start=[10,0,z], mid=[math.sqrt(50),math.sqrt(50),z], end=[0,10,z]) for z in (0,5)], quarter_guides)])
fixtures["network_rational"] = model([network([dict(type="bezier", points=[[10,0,z],[10,10,z],[0,10,z]], weights=[1,math.sqrt(.5),1]) for z in (0,2,5)], quarter_guides)])
closed_guides = [line([x,y,0],[x,y,5]) for x,y in ((10,0),(0,10),(-10,0),(0,-10))]
fixtures["network_closed"] = model([network([dict(type="wire", segments=ring(10,z)) for z in (0,5)], closed_guides)])
fixtures["network_closed_stations"] = copy.deepcopy(fixtures["network_closed"])
fixtures["network_closed_stations"]["features"][0].update(u_parameters=[0,.25,.5,.75,1], v_parameters=[0,1])
fixtures["network_periodic"] = model([network([dict(type="spline", points=[[10,0,z],[0,10,z],[-10,0,z],[0,-10,z]], periodic=True) for z in (0,5)], closed_guides)])
fixtures["network_collapse"] = model([network([dict(type="point", point=[0,0,0]), dict(type="wire", segments=ring(10,5))], [line([0,0,0],g["end"]) for g in closed_guides])])
for kind in ("cylinder", "sphere"):
    target = [dict(id="target", type="cylinder", radius=10, height=5)] if kind == "cylinder" else [
        dict(id="meridian", type="sketch", workplane=dict(origin=[0,0,0], normal=[0,0,1], x_direction=[1,0,0]),
             profile=dict(type="wire", segments=[dict(type="arc", start=[0,-10],mid=[10,0],end=[0,10]),line([0,10],[0,-10])])),
        dict(id="target", type="revolve", input="meridian", axis=dict(origin=[0,0,0],direction=[0,1,0]),angle_deg=360)]
    path = dict(id="path", type="sketch", workplane=dict(origin=[15,0,0] if kind == "sphere" else [15,2,1], normal=[1,0,0], x_direction=[0,1,0]),
                profile=dict(type="wire", segments=ring(3) if kind == "sphere" else loop([[0,0],[6,0],[6,3],[0,3]]),
                             holes=[ring(1) if kind == "sphere" else loop([[2,1],[4,1],[4,2],[2,2]])]))
    for branch in ("nearest", "farthest"):
        fixtures[f"{kind}_{branch}"] = model([*target,path,dict(id="projected",type="surface_project",input="path",target="target",
            faces=dict(type="geometric",feature_id="target",surface_kind=kind,expected_count=1),direction=[-1,0,0],branch=branch)])

with tempfile.TemporaryDirectory(prefix="cad-surface-schema-") as temp:
    root = Path(temp)
    for name, document in fixtures.items():
        created = run("cad_create", dict(document_id=name, model=document), root)
        if name == "network_general":
            assert abs(created["summary"]["area_mm2"] - 100) < 1e-6
            checks += 1
        if name in ("network_arc", "network_rational", "network_closed", "network_collapse"):
            expected = {"network_arc":25*math.pi, "network_rational":25*math.pi, "network_closed":100*math.pi, "network_collapse":10*math.pi*math.sqrt(125)}[name]
            assert abs(created["summary"]["area_mm2"] - expected) < 1e-4
            checks += 1
        if name.startswith(("cylinder_", "sphere_")):
            sign = 1 if name.endswith("nearest") else -1
            assert created["summary"]["center_of_mass_mm"][0] * sign > 5
            expected = 20*math.pi*(math.sqrt(99)-math.sqrt(91)) if name.startswith("sphere") else 30*(math.asin(.8)-math.asin(.2))-10*(math.asin(.6)-math.asin(.4))
            assert abs(created["summary"]["area_mm2"] - expected) < 1e-4
            checks += 2
        run("cad_query", dict(document_id=name, revision=1, kind="topology"), root)
        run("cad_query", dict(document_id=name, revision=1, kind="mesh"), root)
    for name in ("network_general", "network_rational", "network_closed", "network_periodic", "network_collapse", "sphere_nearest", "sphere_farthest"):
        step = run("cad_export", dict(document_id=name, revision=1, format="step"), root)
        imported = run("cad_import", dict(document_id=name+"_step", path=step["path"], geometry="surface"), root)
        assert imported["summary"]["solid_count"] == 0
        checks += 1
    for kind in ("cylinder", "sphere"):
        name = kind+"_nearest"
        before_branch = run("cad_read", dict(document_id=name, revision=1), root)
        ambiguous = copy.deepcopy(fixtures[name]["features"][-1])
        ambiguous.pop("branch")
        run("cad_apply", dict(document_id=name, expected_revision=1, operations=[dict(op="replace_feature", id="projected", feature=ambiguous)]), root, "selection_ambiguous")
        assert run("cad_read", dict(document_id=name), root) == before_branch
        changed = copy.deepcopy(fixtures[name]["features"][-1])
        changed["branch"] = "farthest"
        receipt = run("cad_apply", dict(document_id=name, expected_revision=1, operations=[dict(op="replace_feature", id="projected", feature=changed)]), root)
        assert receipt["revision"] == 2 and receipt["summary"]["center_of_mass_mm"][0] < -5
        assert run("cad_read", dict(document_id=name, revision=1), root) == before_branch
        checks += 3
    ambiguous_grid = model([network(
        [line([0,0,0],[10,0,0]),dict(type="bezier",points=[[0,4,0],[20,4,0],[-10,6,0],[10,6,0]]),line([0,8,0],[10,12,0])],
        [line([0,0,0],[0,8,0]),line([5,0,0],[5,10,0]),line([10,0,0],[10,12,0])],u_parameters=[0,.5,1],v_parameters=[0,.5,1])])
    before_network = run("cad_read", dict(document_id="network_general",revision=1), root)
    run("cad_apply", dict(document_id="network_general",expected_revision=1,operations=[dict(op="replace_feature",id="network",feature=ambiguous_grid["features"][0])]),root,"selection_ambiguous")
    assert run("cad_read",dict(document_id="network_general"),root) == before_network
    checks += 1
    exported = run("cad_export", dict(document_id="trim", revision=1, format="step"), root)
    run("cad_import", dict(document_id="imported", path=exported["path"], geometry="surface"), root)
    run("cad_import", dict(document_id="wrong", path=exported["path"]), root, "invalid_shape")
    before = run("cad_read", dict(document_id="project"), root)
    failed = copy.deepcopy(fixtures["project"]["features"][-1])
    failed["direction"] = [1,0,0]
    run("cad_apply", dict(document_id="project", expected_revision=1, operations=[dict(op="replace_feature", id="projected", feature=failed)]), root, "selection_missing")
    assert run("cad_read", dict(document_id="project"), root) == before
    checks += 1

bad_branch = copy.deepcopy(fixtures["sphere_nearest"])
bad_branch["features"][-1]["branch"] = "automatic"
assert not Draft202012Validator(tools["cad_create"]["inputSchema"]).is_valid(dict(document_id="invalid",model=bad_branch))
bad_point = copy.deepcopy(fixtures["network_collapse"])
bad_point["features"][0]["u_curves"][0]["radius"] = 1
assert not Draft202012Validator(tools["cad_create"]["inputSchema"]).is_valid(dict(document_id="invalid",model=bad_point))
checks += 2
for feature in [
    dict(id="trimmed", type="surface_trim", input="patch", u_range=[0,1], v_range=[0,1], boundary=loop([[0,0],[1,0],[0,1]])),
    dict(id="fill", type="surface_fill", tolerance=1e-5, boundaries=[dict(curve=line([0,0,0],[1,0,0]), continuity="G2")]*2),
]:
    args = dict(document_id="invalid", model=model([patch, feature]))
    assert not Draft202012Validator(tools["cad_create"]["inputSchema"]).is_valid(args)
    checks += 1
assert not Draft202012Validator(tools["cad_import"]["inputSchema"]).is_valid(dict(document_id="invalid", path="unused.step", geometry="surface", solid_indices=[1], expected_sha256="0"*64))
checks += 1
compact = len(json.dumps(dict(tools=list(tools.values())), separators=(",", ":"), ensure_ascii=False).encode())
print(f"{checks} independent surface schema checks; {len(tools)} tools; compact tools wrapper {compact} bytes")
