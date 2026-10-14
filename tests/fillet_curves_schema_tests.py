"""Independent Draft 2020-12 and CLI checks for fillets and saved-curve sweeps.

Developer-only: python tests/fillet_curves_schema_tests.py build/agent-3d-cad
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
catalog = json.loads(subprocess.check_output([exe, "tools"], text=True))
tools = {tool["name"]: tool for tool in catalog}
checks = 0
for tool in catalog:
    for kind in ("inputSchema", "outputSchema"):
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


base = {"id": "base", "type": "box", "size": [20, 30, 40]}
edge = {"type": "geometric", "feature_id": "base", "curve_kind": "line", "expected_count": 1,
        "center": {"point": [20, 30, 20], "tolerance": 1e-6}}
rounding = {"id": "part", "type": "fillet", "input": "base", "edges": edge, "radius": 2,
            "radius_end": 4, "start": {"point": [20, 30, 0], "tolerance": 1e-6}}
smooth = copy.deepcopy(rounding)
smooth["continuity"] = "G2"
speed = math.hypot(12 * math.pi, 4)
helix = {"id": "spine", "type": "curve_helix", "radius": 6, "pitch": 4, "turns": 1.25,
         "frame": {"origin": [0, 0, 0], "normal": [0, 0, 1], "x_direction": [1, 0, 0]}}
profile = {"id": "profile", "type": "sketch", "profile": {"type": "circle", "radius": .5},
           "workplane": {"origin": [6, 0, 0], "normal": [0, 12 * math.pi / speed, 4 / speed], "x_direction": [1, 0, 0]}}
sweep = {"id": "part", "type": "sweep", "input": "profile", "path": "spine", "orientation": "frenet"}
fixtures = [[base, rounding], [base, smooth], [helix, profile, sweep]]
with tempfile.TemporaryDirectory() as workspace:
    def call(name, args):
        validate(name, "inputSchema", args)
        result = subprocess.run([exe, "call", name, "--workspace", workspace, "--input", "-"],
                                input=json.dumps(args), text=True, capture_output=True, timeout=60)
        assert result.returncode == 0, result.stderr
        value = json.loads(result.stdout)
        validate(name, "outputSchema", value)
        return value

    for i, features in enumerate(fixtures):
        args = {"document_id": f"fixture{i}", "model": {"schema_version": 1, "units": "mm", "parameters": {}, "features": features, "output": "part"}}
        created = call("cad_create", args)
        call("cad_read", {"document_id": args["document_id"], "revision": 1})
        call("cad_query", {"document_id": args["document_id"], "revision": 1, "kind": "topology"})
        call("cad_export", {"document_id": args["document_id"], "revision": 1, "format": "step"})
        invalid = copy.deepcopy(args)
        invalid["model"]["features"][-1]["unexpected"] = True
        rejected(invalid)
        if i < 2:
            for key in ("start", "radius_end"):
                invalid = copy.deepcopy(args)
                del invalid["model"]["features"][-1][key]
                rejected(invalid)
            for edges in ("all", dict(edge, expected_count=2)):
                invalid = copy.deepcopy(args)
                invalid["model"]["features"][-1]["edges"] = edges
                rejected(invalid)
            invalid = copy.deepcopy(args)
            invalid["model"]["features"][-1]["continuity"] = "C2"
            rejected(invalid)
        else:
            expected = math.pi * .25 * speed * 1.25
            assert abs(created["summary"]["volume_mm3"] - expected) < 1e-3
            checks += 1
    for topic in ("feature/fillet", "feature/sweep"):
        result = call("cad_help", {"topic": topic})
        Draft202012Validator.check_schema(result["entry"]["schemas"]["input"])
        checks += 1
print(f"{checks} fillet/curve schema checks passed / {len(fixtures)} native models")
