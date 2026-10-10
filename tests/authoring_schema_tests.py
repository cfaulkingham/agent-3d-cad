"""Independent JSON Schema validation of actual native authoring calls.
Developer-only dependency: pinned jsonschema from the existing MCP test venv.
"""
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

with tempfile.TemporaryDirectory(prefix="cad-authoring-schema-") as directory:
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
    model = {"schema_version": 1, "units": "mm", "parameters": {"size": 20},
             "features": [{"id": "stock", "type": "box", "size": [30, 30, 3]}], "output": "stock"}
    call("cad_create", {"document_id": "authoring", "model": model})
    cases = {
        "svg": b"<svg><circle r='10'/></svg>",
        "dxf": b"0\nSECTION\n2\nENTITIES\n0\nCIRCLE\n10\n0\n20\n0\n40\n10\n0\nENDSEC\n0\nEOF\n",
        "text": (source / "tests" / "fixtures" / "authoring-test.ttf").read_bytes(),
    }
    revision = 1
    for format, raw in cases.items():
        path = root / ("asset." + format)
        path.write_bytes(raw)
        args = {"format": format, "path": str(path), "feature_id": "profile_" + format,
                "workplane": plane, "expected_sha256": hashlib.sha256(raw).hexdigest()}
        if format == "text":
            args.update(text="BO", height=20)
        captured = call("cad_capture_sketch", args)
        assert captured["source_sha256"] == args["expected_sha256"]
        imported = call("cad_import_sketch", {**args, "document_id": "authoring", "expected_revision": revision,
                                               "request_id": "schema_import_" + format})
        assert "model" not in imported and imported["feature_id"] == args["feature_id"]
        revision += 1
        assert imported["revision"] == revision
        path.unlink()
        assert call("cad_import_sketch", {**args, "document_id": "authoring", "expected_revision": revision - 1,
                                           "request_id": "schema_import_" + format}) == imported
        for name in ("cad_capture_sketch", "cad_import_sketch"):
            valid = args if name == "cad_capture_sketch" else {**args, "document_id": "authoring", "expected_revision": revision}
            for invalid in [{**valid, "shell": "bad"}, {**valid, "expected_sha256": "bad"},
                            {**valid, "workplane": {**plane, "unknown": True}},
                            {**valid, "text": "unexpected"} if format != "text" else {**valid, "scale": 2}]:
                assert not Draft202012Validator(tools[name]["inputSchema"]).is_valid(invalid)
                checks += 1
        bad_profile = {**captured["feature"]["profile"], "unknown": "bad"}
        bad = {**model, "features": [model["features"][0], {**captured["feature"], "profile": bad_profile}]}
        assert not Draft202012Validator(tools["cad_create"]["inputSchema"]).is_valid({"document_id": "invalid", "model": bad})
        checks += 1
    saved = call("cad_read", {"document_id": "authoring"})
    assert len(saved["model"]["features"]) == 4
    changed = call("cad_apply", {"document_id": "authoring", "expected_revision": revision,
                    "operations": [{"op": "add_feature", "feature": {"id": "label", "type": "extrude", "input": "profile_text", "distance": 2}},
                                   {"op": "set_output", "feature_id": "label"}]})
    assert changed["summary"]["solid_count"] == 2
    text_profile = saved["model"]["features"][-1]["profile"]
    text_profile["height"] = {"parameter": "size"}
    changed_model = {**saved["model"], "features": [*saved["model"]["features"][:-1], {**saved["model"]["features"][-1], "profile": text_profile}]}
    Draft202012Validator(tools["cad_create"]["inputSchema"]).validate({"document_id": "changed", "model": changed_model})
    for name in ("cad_capture_sketch", "cad_import_sketch"):
        valid = {"format": "text", "path": "font.ttf", "feature_id": "letters", "workplane": plane}
        if name == "cad_import_sketch":
            valid.update(document_id="authoring", expected_revision=revision)
        assert not Draft202012Validator(tools[name]["inputSchema"]).is_valid(valid)
        checks += 1
print(f"Authoring schemas: {checks} checks / {len(tools)} tools")
