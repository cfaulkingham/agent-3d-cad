"""Independent JSON Schema validation of actual native authoring calls.
Developer-only dependency: pinned jsonschema from the existing MCP test venv.
"""
import base64
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import time
from jsonschema import Draft202012Validator

exe = str(Path(sys.argv[1]).resolve())
source = Path(__file__).resolve().parents[1]
tools = {tool["name"]: tool for tool in json.loads(subprocess.check_output([exe, "tools"], text=True))}
checks = 0
for tool in tools.values():
    for name in ("inputSchema", "outputSchema"):
        Draft202012Validator.check_schema(tool[name])
        checks += 1

with tempfile.TemporaryDirectory(prefix="cad-parity-authoring-schema-") as directory:
    root = Path(directory)
    def call(tool, args):
        global checks
        definition = tools[tool]
        Draft202012Validator(definition["inputSchema"]).validate(args)
        deadline = time.monotonic() + 5
        while True:
            run = subprocess.run([exe, "call", tool, "--workspace", str(root / "workspace"), "--input", "-"],
                                 input=json.dumps(args), text=True, capture_output=True, timeout=40)
            if not run.returncode:
                break
            if json.loads(run.stderr).get("error", {}).get("code") != "workspace_busy" or time.monotonic() >= deadline:
                raise AssertionError(run.stderr)
            time.sleep(.02)
        result = json.loads(run.stdout)
        Draft202012Validator(definition["outputSchema"]).validate(result)
        checks += 2
        return result

    plane = {"origin": [0, 0, 0], "normal": [0, 0, 1], "x_direction": [1, 0, 0]}
    font_path = root / "captured.ttf"
    font_bytes = (source / "tests/fixtures/authoring-test.ttf").read_bytes()
    font_path.write_bytes(font_bytes)
    font_hash = hashlib.sha256(font_bytes).hexdigest()
    dxf_bytes = b"0\nSECTION\n2\nENTITIES\n0\nTEXT\n1\nB\n40\n7\n0\nENDSEC\n0\nEOF\n"
    dxf_path = root / "captured.dxf"
    dxf_path.write_bytes(dxf_bytes)
    args = {"format": "dxf", "path": str(dxf_path), "feature_id": "profile", "workplane": plane,
            "expected_sha256": hashlib.sha256(dxf_bytes).hexdigest(),
            "fonts": {"STANDARD": {"path": str(font_path), "expected_sha256": font_hash}}}
    captured = call("cad_capture_sketch", args)
    model = {"schema_version": 1, "units": "mm", "parameters": {},
             "features": [captured["feature"], {"id": "part", "type": "extrude", "input": "profile", "distance": 3}],
             "output": "part"}
    created = call("cad_create", {"document_id": "captured", "model": model})
    assert abs(created["summary"]["volume_mm3"] - 78) < 1e-5
    topology = call("cad_query", {"document_id": "captured", "revision": 1, "feature_id": "profile", "kind": "topology"})
    assert topology["topology"]["provenance"]["font_sha256_by_style"] == {"STANDARD": font_hash}
    imported = call("cad_import_sketch", {**args, "document_id": "captured", "expected_revision": 1, "feature_id": "second", "request_id": "font_dxf"})
    assert imported["revision"] == 2
    font_path.unlink()
    dxf_path.unlink()
    assert call("cad_import_sketch", {**args, "document_id": "captured", "expected_revision": 1, "feature_id": "second", "request_id": "font_dxf"}) == imported
    call("cad_read", {"document_id": "captured", "revision": 2})
    for invalid in [{**args, "fonts": {}}, {**args, "format": "svg"},
                    {**args, "fonts": {"STANDARD": {"path": "font.ttf"}}},
                    {**args, "fonts": {"STANDARD": {"path": "font.ttf", "expected_sha256": "bad"}}},
                    {**args, "fonts": {"STANDARD": {**args["fonts"]["STANDARD"], "unknown": True}}}]:
        assert not Draft202012Validator(tools["cad_capture_sketch"]["inputSchema"]).is_valid(invalid)
        checks += 1
    for field, value in [("sha256", "bad"), ("face_index", 32), ("unknown", True)]:
        bad = json.loads(json.dumps(model))
        bad["features"][0]["profile"]["fonts"]["STANDARD"][field] = value
        assert not Draft202012Validator(tools["cad_create"]["inputSchema"]).is_valid({"document_id": "bad", "model": bad})
        checks += 1

    path_model = {"schema_version": 1, "units": "mm", "parameters": {"station": 5},
        "features": [
            {"id": "letters", "type": "sketch", "workplane": plane, "profile": {
                "type": "text", "text": "BB", "height": 10,
                "font": {"content_base64": base64.b64encode(font_bytes).decode(), "sha256": font_hash}}},
            {"id": "path", "type": "curve", "path": {"type": "wire", "segments": [
                {"type": "line", "start": [10, 20, 0], "end": [110, 20, 0]}]}},
            {"id": "placed", "type": "text_on_path", "input": "letters", "path": "path",
             "start": {"parameter": "station"}, "offset": 2, "reverse": False},
            {"id": "part", "type": "extrude", "input": "placed", "distance": 3}], "output": "part"}
    path_created = call("cad_create", {"document_id": "text_path", "model": path_model})
    assert abs(path_created["summary"]["volume_mm3"] - 156) < 1e-5
    path_topology = call("cad_query", {"document_id": "text_path", "revision": 1,
                                      "feature_id": "placed", "kind": "topology"})
    assert path_topology["topology"]["provenance"]["dependencies"] == ["letters", "path"]
    path_edit = call("cad_apply", {"document_id": "text_path", "expected_revision": 1,
        "operations": [{"op": "set_parameter", "name": "station", "value": 15}]})
    assert abs(path_edit["summary"]["center_of_mass_mm"][0] - 32) < 1e-5
    call("cad_read", {"document_id": "text_path", "revision": 1})
    for field, value in [("reverse", 1), ("path", {}), ("unknown", True), ("start", "5mm")]:
        bad = json.loads(json.dumps(path_model))
        bad["features"][2][field] = value
        assert not Draft202012Validator(tools["cad_create"]["inputSchema"]).is_valid({"document_id": "bad", "model": bad})
        checks += 1

print(f"Parity authoring schemas: {checks} checks / {len(tools)} tools")
