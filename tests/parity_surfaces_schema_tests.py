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
with tempfile.TemporaryDirectory(prefix="cad-surface-schema-") as temp:
    root = Path(temp)
    for name, document in fixtures.items():
        run("cad_create", dict(document_id=name, model=document), root)
        run("cad_query", dict(document_id=name, revision=1, kind="topology"), root)
        run("cad_query", dict(document_id=name, revision=1, kind="mesh"), root)
    exported = run("cad_export", dict(document_id="trim", revision=1, format="step"), root)
    run("cad_import", dict(document_id="imported", path=exported["path"], geometry="surface"), root)
    run("cad_import", dict(document_id="wrong", path=exported["path"]), root, "invalid_shape")
    before = run("cad_read", dict(document_id="project"), root)
    failed = copy.deepcopy(fixtures["project"]["features"][-1])
    failed["direction"] = [1,0,0]
    run("cad_apply", dict(document_id="project", expected_revision=1, operations=[dict(op="replace_feature", id="projected", feature=failed)]), root, "selection_missing")
    assert run("cad_read", dict(document_id="project"), root) == before
    checks += 1

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
