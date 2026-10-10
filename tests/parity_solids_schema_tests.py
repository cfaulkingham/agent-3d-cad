"""Independent Draft 2020-12 validation for native solid parity contracts.
Developer-only: python parity_solids_schema_tests.py /absolute/path/to/service.
"""
import copy
import json
from pathlib import Path
import subprocess
import sys
import tempfile
from jsonschema import Draft202012Validator

exe = str(Path(sys.argv[1]).resolve())
catalog = json.loads(subprocess.check_output([exe, "tools"], text=True))
tools = {tool["name"]: tool for tool in catalog}
checks = 0
for tool in catalog:
    for kind in ["inputSchema", "outputSchema"]:
        Draft202012Validator.check_schema(tool[kind])
        checks += 1

def validate(name, kind, value):
    global checks
    Draft202012Validator(tools[name][kind]).validate(value)
    checks += 1

def rejected(value):
    global checks
    assert not Draft202012Validator(tools["cad_create"]["inputSchema"]).is_valid(value)
    checks += 1

plane = {"origin": [0, 0, 0], "normal": [0, 0, 1], "x_direction": [1, 0, 0]}
base = {"id": "base", "type": "box", "size": [20, 30, 10]}
face = lambda n: {"type": "geometric", "feature_id": "base", "surface_kind": "plane", "expected_count": 1, "normal": {"vector": n, "tolerance": 1e-6}}
profile = {"id": "a", "type": "sketch", "workplane": plane, "profile": {"type": "rectangle", "width": 4, "height": 2}}
edge = {"type": "geometric", "feature_id": "base", "curve_kind": "line", "expected_count": 1, "center": {"point": [20, 15, 10], "tolerance": 1e-6}}
fixtures = [
    [base, {"id": "part", "type": "scale", "input": "base", "origin": [1, 2, 3], "factors": 2}],
    [base, {"id": "part", "type": "scale", "input": "base", "origin": [1, 2, 3], "factors": [2, 3, .5]}],
    [base, {"id": "part", "type": "draft", "input": "base", "faces": face([1, 0, 0]), "angle_deg": 5, "direction": [0, 0, 1], "neutral_plane": plane}],
    [base, {"id": "part", "type": "chamfer", "input": "base", "distance": 2, "distance2": 3, "reference_face": face([0, 0, 1]), "edges": edge}],
    [base, {"id": "part", "type": "chamfer", "input": "base", "distance": 2, "angle_deg": 60, "reference_face": face([0, 0, 1]), "edges": edge}],
    [base, {"id": "part", "type": "chamfer", "input": "base", "distance": 2, "edges": edge}],
    [profile, {"id": "part", "type": "twist_extrude", "input": "a", "distance": -10, "angle_deg": 90, "center": [1, 1, 0]}],
    [profile, {"id": "part", "type": "loft", "sections": ["a"], "end_vertex": [2, 1, 10], "ruled": True}],
    [profile, {"id": "part", "type": "loft", "sections": ["a"], "start_vertex": [2, 1, -10], "end_vertex": [2, 1, 10], "ruled": True}],
]
with tempfile.TemporaryDirectory() as workspace:
    for i, features in enumerate(fixtures):
        args = {"document_id": "solid" + str(i), "model": {"schema_version": 1, "units": "mm", "parameters": {}, "features": features, "output": "part"}}
        validate("cad_create", "inputSchema", args)
        result = subprocess.run([exe, "call", "cad_create", "--workspace", workspace, "--input", "-"], input=json.dumps(args), text=True, capture_output=True, timeout=30)
        assert result.returncode == 0, result.stderr
        validate("cad_create", "outputSchema", json.loads(result.stdout))
        invalid = copy.deepcopy(args)
        invalid["model"]["features"][-1]["unexpected"] = True
        rejected(invalid)
        last = features[-1]
        if "reference_face" in last:
            invalid = copy.deepcopy(args)
            del invalid["model"]["features"][-1]["reference_face"]
            rejected(invalid)
            invalid = copy.deepcopy(args)
            invalid["model"]["features"][-1].update(distance2=3, angle_deg=45)
            rejected(invalid)
        if last["type"] == "loft":
            invalid = copy.deepcopy(args)
            invalid["model"]["features"][-1].pop("start_vertex", None)
            invalid["model"]["features"][-1].pop("end_vertex", None)
            rejected(invalid)
print(f"{checks} solid parity schema checks passed / {len(fixtures)} native models")
