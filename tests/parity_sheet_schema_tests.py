"""Independent live Draft 2020-12 contract checks for expanded sheet intent.

Development only: uses jsonschema from tests/mcp_sdk_requirements.txt.
Usage: python tests/parity_sheet_schema_tests.py /absolute/path/to/agent-3d-cad
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
tools = {t["name"]: t for t in catalog}
checks = 0
for tool in tools.values():
    for key in ("inputSchema", "outputSchema"):
        Draft202012Validator.check_schema(tool[key])
        checks += 1

def validate(name, direction, data):
    global checks
    Draft202012Validator(tools[name][direction]).validate(data)
    checks += 1

with tempfile.TemporaryDirectory(prefix="cad-parity-sheet-schema-") as directory:
    def call(name, args):
        validate(name, "inputSchema", args)
        run = subprocess.run([exe, "call", name, "--workspace", directory, "--input", "-"], input=json.dumps(args), text=True, capture_output=True, timeout=60)
        assert run.returncode == 0, run.stderr
        result = json.loads(run.stdout)
        validate(name, "outputSchema", result)
        return result

    request = json.loads((Path(__file__).resolve().parent.parent / "examples/sheet-enclosure.create.json").read_text())
    made = call("cad_create", request)
    assert made["summary"]["sheet_metal"]["bends"][2]["bend_deduction_mm"] is None
    checks += 1
    call("cad_query", {"document_id": request["document_id"], "revision": 1, "feature_id": "formed"})
    call("cad_read", {"document_id": request["document_id"]})
    for field, value in (("edge", request["model"]["features"][1]["flanges"][0]["edge"]), ("unknown", True)):
        bad = copy.deepcopy(request)
        bad["model"]["features"][1]["flanges"][1][field] = value
        assert not Draft202012Validator(tools["cad_create"]["inputSchema"]).is_valid(bad)
        checks += 1
    bad = copy.deepcopy(request)
    del bad["model"]["features"][1]["flanges"][0]["edge"]
    assert not Draft202012Validator(tools["cad_create"]["inputSchema"]).is_valid(bad)
    checks += 1
    cut = copy.deepcopy(request)
    cut["document_id"] = "mappedCut"
    cut["model"]["features"][1]["flanges"] = [cut["model"]["features"][1]["flanges"][0]]
    cut["model"]["features"][1]["flanges"][0]["cuts"] = [{"offset": 10, "width": 5, "from": 1, "to": 10}]
    cut["model"]["features"][1]["flanges"][0]["miter"] = {"start_deg": 15, "end_deg": 15}
    call("cad_create", cut)
    fold = copy.deepcopy(request)
    fold["document_id"] = "internalFold"
    fold["model"]["features"][1]["flanges"] = [{"id":"fold", "fold_line":[[80,40,0],[0,40,0]], "inside_radius":2, "angle_deg":90, "length":15}]
    # Fold removes an existing region; 19.32 mm fits the original 20 mm margin.
    # This leaves a thin separate strip, so use the exact developed remaining leg.
    import math
    fold["model"]["features"][1]["flanges"][0]["length"] = 20 - 2.75 * math.pi / 2
    call("cad_create", fold)
    call("cad_apply", {"document_id": request["document_id"], "expected_revision": 1, "operations":[{"op":"set_parameter", "name":"lip", "value":9}]})
    call("cad_export", {"document_id": request["document_id"], "revision":2, "feature_id":"formed", "format":"step"})
print(f"{checks} sheet parity schema checks / {len(tools)} tools")
print(f"Discovery bytes with tools wrapper: {len(json.dumps({'tools':catalog}, separators=(',',':')).encode())}")
